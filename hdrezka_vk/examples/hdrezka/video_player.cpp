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
// Opening a stream (TLS, redirect, the mp4's moov: several MB for a long film)
// gives up after this, so the spinner ends in an error instead of hanging.
constexpr double kOpenTimeoutSeconds = 45.0;

int64_t steady_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
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

bool VideoPlayer::clock_running() const
{
    std::lock_guard<std::mutex> lock(clock_mutex_);
    return clock_started_;
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

bool VideoPlayer::demux_apply_seek()
{
    double target = seek_target_.exchange(-1.0, std::memory_order_acq_rel);
    if (target < 0.0)
        return false;
    for (;;) {
        const double newer = seek_target_.exchange(-1.0, std::memory_order_acq_rel);
        if (newer < 0.0)
            break;
        target = newer;
    }
    seeking_ = true;
    queue_clear(video_packets_);
    queue_clear(audio_packets_);
    if (format_ctx_) {
        const int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);
        av_seek_frame(format_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
    }
    if (audio_ring_)
        audio_ring_->drain();
    current_position_ = target;
    seek_discard_until_ = std::max(0.0, target - 0.15);
    reset_clock(target);
    video_need_flush_.store(true, std::memory_order_release);
    audio_need_flush_.store(true, std::memory_order_release);
    return true;
}

int VideoPlayer::ffmpeg_interrupt(void *opaque)
{
    auto *self = static_cast<VideoPlayer *>(opaque);
    if (!self)
        return 0;
    if (self->stop_thread_.load(std::memory_order_acquire))
        return 1;
    const int64_t deadline = self->open_deadline_ns_.load(std::memory_order_acquire);
    if (deadline != 0 && steady_ns() > deadline)
        return 1;
    return 0;
}

void VideoPlayer::clear_frame_buffer()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    front_buffer_.clear();
    front_buffer_.shrink_to_fit();
    back_buffer_.clear();
    back_buffer_.shrink_to_fit();
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

bool VideoPlayer::consume_open_error()
{
    return open_error_.exchange(false, std::memory_order_acq_rel);
}

bool VideoPlayer::open(const std::string &url, double seek_seconds)
{
    close();
    if (url.empty())
        return false;

    open_url_ = url;
    open_seek_ = seek_seconds;
    stop_thread_ = false;
    open_error_ = false;
    is_open_ = false;
    playing_ = true;
    buffering_ = true;
    seeking_ = false;
    current_position_ = 0.0;
    seek_target_ = -1.0;
    seek_discard_until_ = -1.0;
    video_need_flush_ = false;
    audio_need_flush_ = false;
    open_deadline_ns_ = 0;
    duration_ = 0.0;
    {
        std::lock_guard<std::mutex> lock(clock_mutex_);
        clock_started_ = false;
        clock_pts_ = 0.0;
    }
    clear_frame_buffer();
    write_log("[DEBUG] VideoPlayer::open queued url=%s seek=%.1f\n", url.c_str(), seek_seconds);
    worker_thread_ = BigThread([this]() { decode_loop(); });
    return true;
}

bool VideoPlayer::prepare_decoder()
{
    const std::string url = open_url_;
    write_log("[DEBUG] VideoPlayer::prepare url=%s\n", url.c_str());
    if (url.empty() || stop_thread_.load(std::memory_order_acquire))
        return false;

    format_ctx_ = avformat_alloc_context();
    if (!format_ctx_)
        return false;
    format_ctx_->interrupt_callback.callback = &VideoPlayer::ffmpeg_interrupt;
    format_ctx_->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "tls_verify", "0", 0);
    av_dict_set(&opts, "rw_timeout", "8000000", 0);
    // TCP's receive window (the tcp protocol's option; "buffer_size" is UDP's and
    // was ignored): a 4K stream is ~12 Mbit/s and the moov alone is several MB.
    av_dict_set(&opts, "recv_buffer_size", "1048576", 0);
    av_dict_set(&opts, "user_agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
                0);
    av_dict_set(&opts, "headers",
                "Referer: https://hdrezka.ag/\r\nOrigin: https://hdrezka.ag\r\n", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);

    if (stop_thread_.load(std::memory_order_acquire)) {
        av_dict_free(&opts);
        free_codec_state();
        return false;
    }
    write_log("[DEBUG] VideoPlayer::prepare avformat_open_input\n");
    const auto open_start = std::chrono::steady_clock::now();
    auto open_ms = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - open_start)
            .count();
    };
    open_deadline_ns_.store(steady_ns() + static_cast<int64_t>(kOpenTimeoutSeconds * 1e9),
                            std::memory_order_release);
    int ret = avformat_open_input(&format_ctx_, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret != 0) {
        open_deadline_ns_.store(0, std::memory_order_release);
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        const double ms = open_ms();
        const char *why = stop_thread_.load() ? " (stopped)"
                          : ms >= kOpenTimeoutSeconds * 1000.0 - 200.0 ? " (timed out)"
                                                                        : "";
        write_log("[DEBUG] avformat_open_input failed ret=%d err='%s' after %.0f ms%s\n", ret, errbuf, ms, why);
        if (format_ctx_) {
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
        }
        return false;
    }
    if (stop_thread_.load(std::memory_order_acquire)) {
        open_deadline_ns_.store(0, std::memory_order_release);
        free_codec_state();
        return false;
    }
    write_log("[DEBUG] avformat_open_input success in %.0f ms (%lld bytes read)\n", open_ms(),
              format_ctx_->pb ? (long long)format_ctx_->pb->bytes_read : -1LL);

    const int info_ret = avformat_find_stream_info(format_ctx_, nullptr);
    open_deadline_ns_.store(0, std::memory_order_release);
    if (info_ret < 0) {
        write_log("[DEBUG] avformat_find_stream_info failed after %.0f ms\n", open_ms());
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
    // PS5 software decode: frame threads plus slice threads.
    video_codec_ctx_->thread_count = 6;
    video_codec_ctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    video_codec_ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
    video_codec_ctx_->skip_loop_filter = AVDISCARD_DEFAULT;
    if (avcodec_open2(video_codec_ctx_, v_codec, nullptr) < 0) {
        free_codec_state();
        return false;
    }

    src_width_ = static_cast<uint32_t>(video_codec_ctx_->width);
    src_height_ = static_cast<uint32_t>(video_codec_ctx_->height);
    if (src_width_ == 0 || src_height_ == 0) {
        write_log("[DEBUG] VideoPlayer::open invalid size %ux%u\n", src_width_, src_height_);
        free_codec_state();
        src_width_ = 0;
        src_height_ = 0;
        width_ = 0;
        height_ = 0;
        return false;
    }
    // The UI is 1920x1080. Scale wider sources (2K/4K) down before RGBA so
    // sws_scale and the Vulkan upload stay on a 1080p-sized buffer.
    width_ = src_width_;
    height_ = src_height_;
    if (src_width_ > 1920) {
        width_ = 1920;
        const uint32_t scaled =
            static_cast<uint32_t>((static_cast<uint64_t>(src_height_) * 1920u + src_width_ / 2) /
                                  src_width_);
        height_ = std::max(2u, scaled);
        if (height_ & 1u)
            ++height_;
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
        write_log("[DEBUG] video src=%ux%u rgba=%ux%u codec=%s threads=%d frame_duration=%.4f (%.2f fps)\n",
                  src_width_, src_height_, width_, height_, v_codec->name ? v_codec->name : "?",
                  video_codec_ctx_->thread_count, frame_duration_, 1.0 / frame_duration_);
    }

    sws_ctx_ = sws_getContext(static_cast<int>(src_width_), static_cast<int>(src_height_),
                              video_codec_ctx_->pix_fmt, static_cast<int>(width_),
                              static_cast<int>(height_), AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR,
                              nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        write_log("[DEBUG] sws_getContext failed\n");
        free_codec_state();
        src_width_ = 0;
        src_height_ = 0;
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

    if (open_seek_ > 0.5)
        seek_target_.store(open_seek_, std::memory_order_release);
    is_open_.store(true, std::memory_order_release);
    write_log("[DEBUG] VideoPlayer::prepare ready %ux%u\n", width_, height_);
    return true;
}

void VideoPlayer::close()
{
    stop_thread_ = true;
    playing_ = false;
    buffering_ = false;
    queue_abort(video_packets_);
    queue_abort(audio_packets_);
    if (worker_thread_.joinable())
        worker_thread_.join();
    if (demux_thread_.joinable())
        demux_thread_.join();
    if (audio_thread_.joinable())
        audio_thread_.join();
    queue_clear(video_packets_);
    queue_clear(audio_packets_);

    free_codec_state();

    if (audio_ring_)
        audio_ring_->drain();

    is_open_ = false;
    seeking_ = false;
    seek_target_ = -1.0;
    seek_discard_until_ = -1.0;
    duration_ = 0.0;
    current_position_ = 0.0;
    src_width_ = 0;
    src_height_ = 0;
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
    if (!new_frame_ready_ || front_buffer_.empty() || frame_w_ == 0 || frame_h_ == 0 ||
        front_buffer_.size() != need) {
        return false;
    }
    std::swap(out_rgba, front_buffer_);
    out_w = frame_w_;
    out_h = frame_h_;
    new_frame_ready_ = false;
    return true;
}

void VideoPlayer::queue_abort(PacketQueue &queue)
{
    std::lock_guard<std::mutex> lock(queue.mu);
    queue.aborted = true;
    queue.cv.notify_all();
}

void VideoPlayer::queue_clear(PacketQueue &queue)
{
    std::lock_guard<std::mutex> lock(queue.mu);
    for (AVPacket *packet : queue.q)
        av_packet_free(&packet);
    queue.q.clear();
    queue.cv.notify_all();
}

void VideoPlayer::queue_reset(PacketQueue &queue)
{
    queue_clear(queue);
    std::lock_guard<std::mutex> lock(queue.mu);
    queue.aborted = false;
}

bool VideoPlayer::queue_push(PacketQueue &queue, AVPacket *packet)
{
    std::unique_lock<std::mutex> lock(queue.mu);
    queue.cv.wait(lock, [&] {
        return queue.aborted || stop_thread_.load(std::memory_order_acquire) ||
               seek_target_.load(std::memory_order_acquire) >= 0.0 || queue.q.size() < queue.max;
    });
    if (queue.aborted || stop_thread_.load(std::memory_order_acquire) ||
        seek_target_.load(std::memory_order_acquire) >= 0.0) {
        av_packet_free(&packet);
        return false;
    }
    queue.q.push_back(packet);
    queue.cv.notify_one();
    return true;
}

AVPacket *VideoPlayer::queue_try_pop(PacketQueue &queue)
{
    std::lock_guard<std::mutex> lock(queue.mu);
    if (queue.q.empty())
        return nullptr;
    AVPacket *packet = queue.q.front();
    queue.q.pop_front();
    queue.cv.notify_one();
    return packet;
}

AVPacket *VideoPlayer::queue_pop_wait(PacketQueue &queue, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(queue.mu);
    queue.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        return queue.aborted || stop_thread_.load(std::memory_order_acquire) || !queue.q.empty();
    });
    if (queue.q.empty())
        return nullptr;
    AVPacket *packet = queue.q.front();
    queue.q.pop_front();
    queue.cv.notify_one();
    return packet;
}

void VideoPlayer::demux_loop()
{
    while (!stop_thread_.load(std::memory_order_acquire)) {
        if (demux_apply_seek())
            continue;
        if (!playing_.load()) {
            wait_interruptible(0.02);
            continue;
        }
        AVPacket *packet = av_packet_alloc();
        if (!packet)
            break;
        const int ret = av_read_frame(format_ctx_, packet);
        if (ret < 0) {
            av_packet_free(&packet);
            if (stop_thread_.load(std::memory_order_acquire))
                break;
            wait_interruptible(0.02);
            continue;
        }
        if (packet->stream_index == video_stream_idx_) {
            if (!queue_push(video_packets_, packet))
                continue;
        } else if (packet->stream_index == audio_stream_idx_ && audio_thread_.joinable()) {
            if (!queue_push(audio_packets_, packet))
                continue;
        } else {
            av_packet_free(&packet);
        }
    }
}

void VideoPlayer::decode_loop()
{
    if (!prepare_decoder()) {
        buffering_ = false;
        playing_ = false;
        is_open_ = false;
        if (!stop_thread_.load(std::memory_order_acquire))
            open_error_.store(true, std::memory_order_release);
        write_log("[DEBUG] VideoPlayer::prepare failed stop=%d\n",
                  (int)stop_thread_.load(std::memory_order_acquire));
        return;
    }

    const int src_w = static_cast<int>(src_width_);
    const int src_h = static_cast<int>(src_height_);
    const int dec_w = static_cast<int>(width_);
    const int dec_h = static_cast<int>(height_);
    if (src_w <= 0 || src_h <= 0 || dec_w <= 0 || dec_h <= 0 || !format_ctx_ || !video_codec_ctx_ ||
        !sws_ctx_) {
        buffering_ = false;
        playing_ = false;
        is_open_ = false;
        if (!stop_thread_.load(std::memory_order_acquire))
            open_error_.store(true, std::memory_order_release);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    AVFrame *rgba_frame = av_frame_alloc();
    if (!frame || !rgba_frame) {
        av_frame_free(&rgba_frame);
        av_frame_free(&frame);
        buffering_ = false;
        playing_ = false;
        is_open_ = false;
        if (!stop_thread_.load(std::memory_order_acquire))
            open_error_.store(true, std::memory_order_release);
        return;
    }

    queue_reset(video_packets_);
    queue_reset(audio_packets_);
    if (audio_codec_ctx_ && swr_ctx_ && audio_ring_)
        audio_thread_ = BigThread([this]() { audio_loop(); });
    demux_thread_ = BigThread([this]() { demux_loop(); });

    const size_t rgba_need = static_cast<size_t>(dec_w) * static_cast<size_t>(dec_h) * 4;
    double last_video_pts = AV_NOPTS_VALUE;
    // Late frames are dropped without conversion, but never all of them: a
    // decoder slower than the film still shows a picture this often.
    constexpr double kMaxHoldSeconds = 0.25;
    auto last_shown = std::chrono::steady_clock::now();
    long dropped = 0;
    long shown = 0;
    auto stats_at = std::chrono::steady_clock::now();

    // Seconds behind the clock past which the decoder drops work to catch up.
    auto set_catch_up = [&](double late) {
        const AVDiscard filter = late > 0.15 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
        const AVDiscard frames = late > 0.5 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
        if (video_codec_ctx_->skip_loop_filter != filter)
            video_codec_ctx_->skip_loop_filter = filter;
        if (video_codec_ctx_->skip_frame != frames)
            video_codec_ctx_->skip_frame = frames;
    };

    while (!stop_thread_.load(std::memory_order_acquire)) {
        if (video_need_flush_.exchange(false, std::memory_order_acq_rel)) {
            avcodec_flush_buffers(video_codec_ctx_);
            // Frames before the seek target are only decoded to be thrown
            // away: skip the ones nothing else references.
            video_codec_ctx_->skip_loop_filter = AVDISCARD_NONREF;
            video_codec_ctx_->skip_frame =
                seek_discard_until_.load() >= 0.0 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
            last_video_pts = AV_NOPTS_VALUE;
        }

        if (!playing_.load()) {
            wait_interruptible(0.02);
            continue;
        }

        AVPacket *packet = queue_pop_wait(video_packets_, 20);
        if (!packet)
            continue;
        if (avcodec_send_packet(video_codec_ctx_, packet) == 0) {
            while (avcodec_receive_frame(video_codec_ctx_, frame) == 0) {
                if (video_need_flush_.load(std::memory_order_acquire) ||
                    stop_thread_.load(std::memory_order_acquire))
                    break;
                double pts = packet_pts_seconds(frame, video_stream_idx_);
                if (pts == AV_NOPTS_VALUE) {
                    pts = (last_video_pts == AV_NOPTS_VALUE) ? 0.0 : last_video_pts + frame_duration_;
                }
                const double discard_until = seek_discard_until_.load();
                if (discard_until >= 0.0) {
                    if (pts + 0.01 < discard_until)
                        continue;
                    seek_discard_until_ = -1.0;
                    reset_clock(pts);
                    seeking_ = false;
                    set_catch_up(0.0);
                }
                last_video_pts = pts;
                current_position_ = pts;
                ensure_clock(pts);

                const auto now = std::chrono::steady_clock::now();
                const double late = master_clock() - pts;
                set_catch_up(late);
                const bool overdue =
                    std::chrono::duration<double>(now - last_shown).count() >= kMaxHoldSeconds;
                if (late > 0.04 && !overdue) {
                    ++dropped;
                    continue;
                }

                // Convert while waiting for the frame's time, not after it.
                if (back_buffer_.size() != rgba_need)
                    back_buffer_.resize(rgba_need);
                av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, back_buffer_.data(),
                                     AV_PIX_FMT_RGBA, dec_w, dec_h, 1);
                sws_scale(sws_ctx_, frame->data, frame->linesize, 0, src_h, rgba_frame->data,
                          rgba_frame->linesize);

                bool present = true;
                for (double ahead = pts - master_clock(); ahead > 0.002; ahead = pts - master_clock()) {
                    if (!wait_interruptible(std::min(ahead, 0.05))) {
                        present = false;
                        break;
                    }
                }
                if (!present)
                    break;
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    std::swap(front_buffer_, back_buffer_);
                    frame_w_ = static_cast<uint32_t>(dec_w);
                    frame_h_ = static_cast<uint32_t>(dec_h);
                    new_frame_ready_ = true;
                    buffering_ = false;
                }
                last_shown = std::chrono::steady_clock::now();
                ++shown;
            }
        }
        av_packet_free(&packet);

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - stats_at).count() >= 10.0) {
            write_log("[DEBUG] video stats shown=%ld dropped=%ld pos=%.1f late=%.3f\n", shown, dropped,
                      current_position_.load(), master_clock() - current_position_.load());
            shown = 0;
            dropped = 0;
            stats_at = now;
        }
    }

    av_frame_free(&rgba_frame);
    av_frame_free(&frame);
}

void VideoPlayer::audio_loop()
{
    AVFrame *audio_frame = av_frame_alloc();
    if (!audio_frame)
        return;
    std::vector<float> resampled;
    const double ring_rate = static_cast<double>(audio_out_rate_);

    while (!stop_thread_.load(std::memory_order_acquire)) {
        if (audio_need_flush_.exchange(false, std::memory_order_acq_rel))
            avcodec_flush_buffers(audio_codec_ctx_);
        // Paused, seeking (until the video shows the new position) or before the
        // first picture: keep the packets, the clock is not where they belong yet.
        if (!playing_.load() || seeking_.load(std::memory_order_acquire) ||
            seek_discard_until_.load() >= 0.0 || !clock_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        AVPacket *packet = queue_pop_wait(audio_packets_, 20);
        if (!packet)
            continue;
        if (avcodec_send_packet(audio_codec_ctx_, packet) == 0) {
            while (avcodec_receive_frame(audio_codec_ctx_, audio_frame) == 0) {
                if (seek_target_.load(std::memory_order_acquire) >= 0.0 ||
                    audio_need_flush_.load(std::memory_order_acquire) ||
                    stop_thread_.load(std::memory_order_acquire))
                    break;
                const int max_dst_nb_samples = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(swr_ctx_, audio_codec_ctx_->sample_rate) + audio_frame->nb_samples,
                    audio_out_rate_, audio_codec_ctx_->sample_rate, AV_ROUND_UP));
                if (max_dst_nb_samples <= 0)
                    continue;
                if (resampled.size() < static_cast<size_t>(max_dst_nb_samples) * 2)
                    resampled.resize(static_cast<size_t>(max_dst_nb_samples) * 2);
                uint8_t *out_data[1] = {reinterpret_cast<uint8_t *>(resampled.data())};
                const int converted =
                    swr_convert(swr_ctx_, out_data, max_dst_nb_samples,
                                (const uint8_t **)audio_frame->data, audio_frame->nb_samples);
                if (converted <= 0)
                    continue;

                // What is written now is heard after what the ring holds: keep
                // that moment on the clock. Drop audio the picture has passed,
                // hold audio that is early (after a pause or a stall).
                const double a_pts = packet_pts_seconds(audio_frame, audio_stream_idx_);
                if (a_pts != AV_NOPTS_VALUE) {
                    const double dur = converted / ring_rate;
                    double heard_at = master_clock() + audio_ring_->available() / ring_rate;
                    if (a_pts + dur < heard_at - 0.03)
                        continue;
                    bool ok = true;
                    while (a_pts > heard_at + 0.03) {
                        if (!wait_interruptible(std::min(a_pts - heard_at, 0.05))) {
                            ok = false;
                            break;
                        }
                        heard_at = master_clock() + audio_ring_->available() / ring_rate;
                    }
                    if (!ok)
                        break;
                }
                write_audio_blocking(resampled.data(), converted);
            }
        }
        av_packet_free(&packet);
    }
    av_frame_free(&audio_frame);
}

} // namespace rezka
