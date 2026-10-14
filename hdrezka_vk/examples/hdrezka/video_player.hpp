#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include "big_thread.hpp"
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace hui::audio {
class StreamRing;
}

namespace rezka
{

class VideoPlayer
{
public:
    VideoPlayer();
    ~VideoPlayer();

    // Starts the decode thread and returns at once. Network open happens there.
    bool open(const std::string &url, double seek_seconds = -1.0);
    void close();
    // True once, after the worker failed to open the URL.
    bool consume_open_error();

    void play();
    void pause();
    void toggle_pause();
    void seek(double target_seconds);

    double get_position() const { return current_position_.load(); }
    double get_duration() const { return duration_.load(); }
    bool is_playing() const { return playing_.load(); }
    bool is_open() const { return is_open_.load(); }

    // Presented RGBA size (source, or scaled down to a 1920-wide frame).
    uint32_t get_width() const { return width_; }
    uint32_t get_height() const { return height_; }
    uint32_t get_source_width() const { return src_width_; }
    uint32_t get_source_height() const { return src_height_; }

    void set_audio_ring(hui::audio::StreamRing *ring) { audio_ring_ = ring; }

    // Returns true if a new decoded frame is available for uploading to GPU.
    // out_w/out_h match the pixel buffer; do not use get_width/get_height with it.
    bool get_frame(std::vector<uint8_t> &out_rgba, uint32_t &out_w, uint32_t &out_h);

private:
    struct PacketQueue {
        explicit PacketQueue(std::size_t max_packets) : max(max_packets) {}
        std::mutex mu;
        std::condition_variable cv;
        std::deque<AVPacket *> q;
        bool aborted = false;
        const std::size_t max;
    };

    void decode_loop();
    void demux_loop();
    // Audio has its own thread: a full ring blocks it, never the video decoder.
    void audio_loop();
    void reset_clock(double pts);
    void ensure_clock(double pts);
    double master_clock() const;
    bool clock_running() const;
    void write_audio_blocking(const float *samples, int frames);
    bool wait_interruptible(double seconds);
    double packet_pts_seconds(const AVFrame *frame, int stream_idx) const;
    bool demux_apply_seek();
    bool prepare_decoder();
    void free_codec_state();
    void clear_frame_buffer();
    void queue_abort(PacketQueue &queue);
    void queue_reset(PacketQueue &queue);
    void queue_clear(PacketQueue &queue);
    bool queue_push(PacketQueue &queue, AVPacket *packet);
    AVPacket *queue_try_pop(PacketQueue &queue);
    AVPacket *queue_pop_wait(PacketQueue &queue, int timeout_ms);
    static int ffmpeg_interrupt(void *opaque);

    std::atomic<bool> is_open_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> buffering_{false};
    std::atomic<bool> stop_thread_{false};
    std::atomic<bool> open_error_{false};
    std::string open_url_;
    double open_seek_ = -1.0;
    std::atomic<bool> seeking_{false};
    std::atomic<double> current_position_{0.0};
    std::atomic<double> duration_{0.0};
    std::atomic<double> seek_target_{-1.0};
    std::atomic<double> seek_discard_until_{-1.0};
    // Network open gives up at this steady_clock time (ns since epoch, 0 = none).
    std::atomic<int64_t> open_deadline_ns_{0};

    uint32_t src_width_ = 0;
    uint32_t src_height_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    double frame_duration_ = 1.0 / 25.0;
    int audio_out_rate_ = 48000;

    BigThread worker_thread_;
    BigThread demux_thread_;
    BigThread audio_thread_;
    // Video: ~6.5 s at 24 fps. Audio packets are small and the ring keeps
    // ~5.5 s ahead of the clock, so its queue is deeper than the video one.
    PacketQueue video_packets_{160};
    PacketQueue audio_packets_{640};
    std::atomic<bool> video_need_flush_{false};
    std::atomic<bool> audio_need_flush_{false};

    std::mutex frame_mutex_;
    std::vector<uint8_t> front_buffer_;
    std::vector<uint8_t> back_buffer_;
    uint32_t frame_w_ = 0;
    uint32_t frame_h_ = 0;
    bool new_frame_ready_ = false;

    // Wall-clock master synced to media PTS (frozen while paused).
    mutable std::mutex clock_mutex_;
    std::chrono::steady_clock::time_point clock_wall_{};
    double clock_pts_ = 0.0;
    bool clock_started_ = false;

    AVFormatContext *format_ctx_ = nullptr;
    AVCodecContext *video_codec_ctx_ = nullptr;
    AVCodecContext *audio_codec_ctx_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;
    SwrContext *swr_ctx_ = nullptr;
    int video_stream_idx_ = -1;
    int audio_stream_idx_ = -1;
    hui::audio::StreamRing *audio_ring_ = nullptr;
};

} // namespace rezka
