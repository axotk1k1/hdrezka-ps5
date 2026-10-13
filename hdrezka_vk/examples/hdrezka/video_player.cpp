#include "video_player.hpp"
#include "audio/stream_ring.hpp"
#include "logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace rezka
{

namespace
{
constexpr int kAudioRate = 48000;
}

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer()
{
    close();
}

void VideoPlayer::reset_clock(double pts)
{
    std::lock_guard<std::mutex> lock(clock_mutex_);
    clock_pts_ = pts;
    clock_wall_ = std::chrono::steady_clock::now();
    clock_started_ = true;
}

void VideoPlayer::ensure_clock(double pts)
{
    std::lock_guard<std::mutex> lock(clock_mutex_);
    if (clock_started_)
        return;
    clock_pts_ = pts;
    clock_wall_ = std::chrono::steady_clock::now();
    clock_started_ = true;
}

double VideoPlayer::master_clock() const
{
    std::lock_guard<std::mutex> lock(clock_mutex_);
    if (!clock_started_)
        return 0.0;
    if (!playing_.load())
        return clock_pts_;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - clock_wall_).count();
    return clock_pts_ + elapsed;
}

double VideoPlayer::packet_pts_seconds(const AVFrame *frame, int stream_idx) const
{
    if (!format_ctx_ || stream_idx < 0 || !frame)
        return AV_NOPTS_VALUE;
    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE)
        ts = frame->pts;
    if (ts == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;
    const AVRational tb = format_ctx_->streams[stream_idx]->time_base;
    return ts * av_q2d(tb);
}

bool VideoPlayer::wait_interruptible(double seconds)
{
    if (seconds <= 0.0)
        return true;
    const auto start = std::chrono::steady_clock::now();
    const double budget_ms = seconds * 1000.0;
    while (!stop_thread_) {
        if (seek_target_.load(std::memory_order_acquire) >= 0.0)
            return false;
        if (!playing_.load())
            return false;
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();
        if (elapsed_ms >= budget_ms)
            return true;
        double left = budget_ms - elapsed_ms;
        if (left > 8.0)
            left = 8.0;
        if (left < 1.0)
            left = 1.0;
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(left)));
    }
    return false;
}

void VideoPlayer::write_audio_blocking(const float *samples, int frames)
{
    if (!audio_ring_ || frames <= 0)
        return;
    // During / right after a seek, drop audio instead of stalling the decoder
    // on a full ring -- otherwise back-to-back seeks freeze the player.
    if (seeking_.load(std::memory_order_acquire))
        return;
    int written = 0;
    int spins = 0;
    while (written < frames && !stop_thread_) {
        if (seek_target_.load(std::memory_order_acquire) >= 0.0)
            return;
        if (!playing_.load()) {
            if (!wait_interruptible(0.01))
                return;
            continue;
        }
        const std::size_t n =
            audio_ring_->write(samples + written * 2,
                               static_cast<std::size_t>(frames - written));
        written += static_cast<int>(n);
        if (n == 0) {
            if (++spins > 250) // ~1s max wait -- never block seeks forever
                return;
            if (!wait_interruptible(0.004))
                return;
        } else {
            spins = 0;
        }
    }
}

bool VideoPlayer::apply_pending_seek(double *last_video_pts)
{
    double target = seek_target_.exchange(-1.0, std::memory_order_acq_rel);
    if (target < 0.0)
        return false;
    // Coalesce rapid scrub updates into the latest target.
    for (;;) {
        const double newer = seek_target_.exchange(-1.0, std::memory_order_acq_rel);
        if (newer < 0.0)
            break;
        target = newer;
    }
    seeking_ = true;
    const int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);
    av_seek_frame(format_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (video_codec_ctx_)
        avcodec_flush_buffers(video_codec_ctx_);
    if (audio_codec_ctx_)
        avcodec_flush_buffers(audio_codec_ctx_);
    if (audio_ring_)
        audio_ring_->drain();
    current_position_ = target;
    if (last_video_pts)
        *last_video_pts = AV_NOPTS_VALUE;
    seek_discard_until_ = std::max(0.0, target - 0.15);
    reset_clock(target);
    return true;
}

int VideoPlayer::ffmpeg_interrupt(void *opaque)
{
    auto *self = static_cast<VideoPlayer *>(opaque);
    return (self && self->stop_thread_.load(std::memory_order_acquire)) ? 1 : 0;
}

void VideoPlayer::clear_frame_buffer()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_buffer_.clear();
    frame_buffer_.shrink_to_fit();
    frame_w_ = 0;
    frame_h_ = 0;
    new_frame_ready_ = false;
}

void VideoPlayer::free_codec_state()
{
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    if (video_codec_ctx_) {
        avcodec_free_context(&video_codec_ctx_);
        video_codec_ctx_ = nullptr;
    }
    if (audio_codec_ctx_) {
        avcodec_free_context(&audio_codec_ctx_);
        audio_codec_ctx_ = nullptr;
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
}

bool VideoPlayer::open(const std::string &url)
{
    close();
    stop_thread_ = false;
    playing_ = false;
    is_open_ = false;

    write_log("[DEBUG] VideoPlayer::open url=%s\n", url.c_str());

    format_ctx_ = avformat_alloc_context();
    if (!format_ctx_)
        return false;
    format_ctx_->interrupt_callback.callback = &VideoPlayer::ffmpeg_interrupt;
    format_ctx_->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "tls_verify", "0", 0);
    av_dict_set(&opts, "timeout", "10000000", 0);
    av_dict_set(&opts, "rw_timeout", "10000000", 0);
    av_dict_set(&opts, "user_agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
                0);
    av_dict_set(&opts, "headers",
                "Referer: https://hdrezka.ag/\r\nOrigin: https://hdrezka.ag\r\n", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);

    int ret = avformat_open_input(&format_ctx_, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret != 0) {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        write_log("[DEBUG] avformat_open_input failed ret=%d err='%s'\n", ret, errbuf);
        if (format_ctx_) {
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
        }
        return false;
    }
    write_log("[DEBUG] avformat_open_input success\n");

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        write_log("[DEBUG] avformat_find_stream_info failed\n");
        free_codec_state();
        return false;
    }

    if (format_ctx_->duration != AV_NOPTS_VALUE) {
        duration_ = static_cast<double>(format_ctx_->duration) / AV_TIME_BASE;
    } else {
        duration_ = 0.0;
    }

    video_stream_idx_ = -1;
    audio_stream_idx_ = -1;

    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            video_stream_idx_ < 0) {
            video_stream_idx_ = static_cast<int>(i);
        } else if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                   audio_stream_idx_ < 0) {
            audio_stream_idx_ = static_cast<int>(i);
        }
    }

    if (video_stream_idx_ < 0) {
        free_codec_state();
        return false;
    }

    AVCodecParameters *v_par = format_ctx_->streams[video_stream_idx_]->codecpar;
    const AVCodec *v_codec = avcodec_find_decoder(v_par->codec_id);
    if (!v_codec) {
        free_codec_state();
        return false;
    }

    video_codec_ctx_ = avcodec_alloc_context3(v_codec);
    avcodec_parameters_to_context(video_codec_ctx_, v_par);
    // Prefer more threads for software decode on PS5 CPU.
    video_codec_ctx_->thread_count = 2;
    if (avcodec_open2(video_codec_ctx_, v_codec, nullptr) < 0) {
        free_codec_state();
        return false;
    }

    width_ = static_cast<uint32_t>(video_codec_ctx_->width);
    height_ = static_cast<uint32_t>(video_codec_ctx_->height);
    if (width_ == 0 || height_ == 0) {
        write_log("[DEBUG] VideoPlayer::open invalid size %ux%u\n", width_, height_);
        free_codec_state();
        width_ = 0;
        height_ = 0;
        return false;
    }

    {
        const AVRational fr = format_ctx_->streams[video_stream_idx_]->avg_frame_rate;
        if (fr.num > 0 && fr.den > 0) {
            frame_duration_ = av_q2d(av_inv_q(fr));
        } else {
            const AVRational rfr = format_ctx_->streams[video_stream_idx_]->r_frame_rate;
            if (rfr.num > 0 && rfr.den > 0)
                frame_duration_ = av_q2d(av_inv_q(rfr));
            else
                frame_duration_ = 1.0 / 25.0;
        }
        // Clamp nonsense rates.
        if (frame_duration_ < 0.008 || frame_duration_ > 0.1)
            frame_duration_ = 1.0 / 25.0;
        write_log("[DEBUG] video %dx%d codec=%s frame_duration=%.4f (%.2f fps)\n",
                  width_, height_, v_codec->name ? v_codec->name : "?", frame_duration_,
                  1.0 / frame_duration_);
    }

    sws_ctx_ = sws_getContext(static_cast<int>(width_), static_cast<int>(height_),
                              video_codec_ctx_->pix_fmt, static_cast<int>(width_),
                              static_cast<int>(height_), AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR,
                              nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        write_log("[DEBUG] sws_getContext failed\n");
        free_codec_state();
        width_ = 0;
        height_ = 0;
        return false;
    }

    audio_out_rate_ = kAudioRate;
    if (audio_stream_idx_ >= 0) {
        AVCodecParameters *a_par = format_ctx_->streams[audio_stream_idx_]->codecpar;
        const AVCodec *a_codec = avcodec_find_decoder(a_par->codec_id);
        if (a_codec) {
            audio_codec_ctx_ = avcodec_alloc_context3(a_codec);
            avcodec_parameters_to_context(audio_codec_ctx_, a_par);
            if (avcodec_open2(audio_codec_ctx_, a_codec, nullptr) >= 0) {
                AVChannelLayout out_layout;
                av_channel_layout_default(&out_layout, 2);
                swr_alloc_set_opts2(&swr_ctx_, &out_layout, AV_SAMPLE_FMT_FLT, audio_out_rate_,
                                    &audio_codec_ctx_->ch_layout, audio_codec_ctx_->sample_fmt,
                                    audio_codec_ctx_->sample_rate, 0, nullptr);
                if (swr_ctx_) {
                    swr_init(swr_ctx_);
                    write_log("[DEBUG] audio codec=%s src_rate=%d ch=%d -> %d Hz stereo float\n",
                              a_codec->name ? a_codec->name : "?", audio_codec_ctx_->sample_rate,
                              audio_codec_ctx_->ch_layout.nb_channels, audio_out_rate_);
                }
            }
        }
    }

    if (audio_ring_)
        audio_ring_->drain();

    clear_frame_buffer();
    is_open_ = true;
    playing_ = true;
    stop_thread_ = false;
    seeking_ = false;
    current_position_ = 0.0;
    seek_target_ = -1.0;
    seek_discard_until_ = -1.0;
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        clock_started_ = false;
        clock_pts_ = 0.0;
    }

    worker_thread_ = BigThread([this]() { decode_loop(); });
    return true;
}

void VideoPlayer::close()
{
    stop_thread_ = true;
    playing_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    free_codec_state();

    if (audio_ring_)
        audio_ring_->drain();

    is_open_ = false;
    seeking_ = false;
    seek_target_ = -1.0;
    seek_discard_until_ = -1.0;
    duration_ = 0.0;
    current_position_ = 0.0;
    width_ = 0;
    height_ = 0;
    clear_frame_buffer();
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        clock_started_ = false;
    }
}

void VideoPlayer::play()
{
    if (playing_.load())
        return;
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        // Resume wall clock from the frozen media PTS.
        clock_wall_ = std::chrono::steady_clock::now();
    }
    playing_ = true;
}

void VideoPlayer::pause()
{
    if (!playing_.load())
        return;
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        if (clock_started_) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                std::chrono::duration<double>(now - clock_wall_).count();
            clock_pts_ += elapsed;
            clock_wall_ = now;
        }
    }
    playing_ = false;
}

void VideoPlayer::toggle_pause()
{
    if (playing_.load())
        pause();
    else
        play();
}

void VideoPlayer::seek(double target_seconds)
{
    if (target_seconds < 0.0)
        target_seconds = 0.0;
    const double dur = duration_.load();
    if (dur > 0.0 && target_seconds > dur)
        target_seconds = dur;
    seeking_ = true;
    seek_target_.store(target_seconds, std::memory_order_release);
}

bool VideoPlayer::get_frame(std::vector<uint8_t> &out_rgba, uint32_t &out_w, uint32_t &out_h)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    const size_t need = static_cast<size_t>(frame_w_) * static_cast<size_t>(frame_h_) * 4;
    if (!new_frame_ready_ || frame_buffer_.empty() || frame_w_ == 0 || frame_h_ == 0 ||
        frame_buffer_.size() != need) {
        return false;
    }
    out_rgba = frame_buffer_;
    out_w = frame_w_;
    out_h = frame_h_;
    new_frame_ready_ = false;
    return true;
}

void VideoPlayer::decode_loop()
{
    const int dec_w = static_cast<int>(width_);
    const int dec_h = static_cast<int>(height_);
    if (dec_w <= 0 || dec_h <= 0 || !format_ctx_ || !video_codec_ctx_ || !sws_ctx_)
        return;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *audio_frame = av_frame_alloc();
    AVFrame *rgba_frame = av_frame_alloc();
    if (!packet || !frame || !audio_frame || !rgba_frame) {
        av_frame_free(&rgba_frame);
        av_frame_free(&audio_frame);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return;
    }

    const int rgba_buf_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, dec_w, dec_h, 1);
    std::vector<uint8_t> rgba_raw(static_cast<size_t>(rgba_buf_size));

    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_raw.data(), AV_PIX_FMT_RGBA,
                         dec_w, dec_h, 1);

    double last_video_pts = AV_NOPTS_VALUE;

    while (!stop_thread_) {
        if (apply_pending_seek(&last_video_pts))
            continue;

        if (!playing_.load()) {
            wait_interruptible(0.02);
            continue;
        }

        if (av_read_frame(format_ctx_, packet) < 0) {
            wait_interruptible(0.02);
            continue;
        }

        if (packet->stream_index == video_stream_idx_) {
            if (avcodec_send_packet(video_codec_ctx_, packet) == 0) {
                while (avcodec_receive_frame(video_codec_ctx_, frame) == 0) {
                    if (apply_pending_seek(&last_video_pts))
                        break;
                    double pts = packet_pts_seconds(frame, video_stream_idx_);
                    if (pts == AV_NOPTS_VALUE) {
                        pts = (last_video_pts == AV_NOPTS_VALUE)
                                  ? 0.0
                                  : last_video_pts + frame_duration_;
                    }
                    // Drop pre-roll after a seek until we reach the target.
                    if (seek_discard_until_ >= 0.0 && pts + 0.01 < seek_discard_until_)
                        continue;
                    if (seek_discard_until_ >= 0.0 && pts + 0.01 >= seek_discard_until_) {
                        seek_discard_until_ = -1.0;
                        reset_clock(pts);
                        seeking_ = false;
                    }
                    last_video_pts = pts;
                    current_position_ = pts;

                    ensure_clock(pts);

                    // Sync video to wall/master clock (audio is paced by the ring).
                    double delay = pts - master_clock();
                    if (delay > 0.002) {
                        const double sleep_s = std::min(delay, 0.08);
                        if (!wait_interruptible(sleep_s))
                            break;
                        delay = pts - master_clock();
                    }

                    // Far behind: drop this frame without presenting.
                    if (delay < -0.25)
                        continue;

                    sws_scale(sws_ctx_, frame->data, frame->linesize, 0, dec_h,
                              rgba_frame->data, rgba_frame->linesize);

                    {
                        std::lock_guard<std::mutex> lock(frame_mutex_);
                        const size_t need = static_cast<size_t>(dec_w) * static_cast<size_t>(dec_h) * 4;
                        if (frame_buffer_.size() != need)
                            frame_buffer_.resize(need);
                        for (int y = 0; y < dec_h; ++y) {
                            std::memcpy(frame_buffer_.data() + static_cast<size_t>(y) * static_cast<size_t>(dec_w) * 4,
                                        rgba_frame->data[0] + y * rgba_frame->linesize[0],
                                        static_cast<size_t>(dec_w) * 4);
                        }
                        frame_w_ = static_cast<uint32_t>(dec_w);
                        frame_h_ = static_cast<uint32_t>(dec_h);
                        new_frame_ready_ = true;
                    }
                }
            }
        } else if (packet->stream_index == audio_stream_idx_ && audio_codec_ctx_ && swr_ctx_ &&
                   audio_ring_) {
            if (seeking_.load(std::memory_order_acquire) || seek_discard_until_ >= 0.0) {
                av_packet_unref(packet);
                continue;
            }
            if (avcodec_send_packet(audio_codec_ctx_, packet) == 0) {
                while (avcodec_receive_frame(audio_codec_ctx_, audio_frame) == 0) {
                    if (seek_target_.load(std::memory_order_acquire) >= 0.0)
                        break;
                    const double a_pts = packet_pts_seconds(audio_frame, audio_stream_idx_);
                    if (a_pts != AV_NOPTS_VALUE)
                        ensure_clock(a_pts);

                    const int max_dst_nb_samples = static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swr_ctx_, audio_codec_ctx_->sample_rate) +
                            audio_frame->nb_samples,
                        audio_out_rate_, audio_codec_ctx_->sample_rate, AV_ROUND_UP));
                    if (max_dst_nb_samples <= 0)
                        continue;

                    std::vector<float> resampled(static_cast<size_t>(max_dst_nb_samples) * 2);
                    uint8_t *out_data[1] = {reinterpret_cast<uint8_t *>(resampled.data())};
                    const int converted =
                        swr_convert(swr_ctx_, out_data, max_dst_nb_samples,
                                    (const uint8_t **)audio_frame->data, audio_frame->nb_samples);
                    if (converted > 0)
                        write_audio_blocking(resampled.data(), converted);
                }
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&rgba_frame);
    av_frame_free(&audio_frame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

} // namespace rezka
