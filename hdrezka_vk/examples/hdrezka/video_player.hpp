#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
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

    bool open(const std::string &url);
    void close();

    void play();
    void pause();
    void toggle_pause();
    void seek(double target_seconds);

    double get_position() const { return current_position_.load(); }
    double get_duration() const { return duration_.load(); }
    bool is_playing() const { return playing_.load(); }
    bool is_open() const { return is_open_.load(); }

    uint32_t get_width() const { return width_; }
    uint32_t get_height() const { return height_; }

    void set_audio_ring(hui::audio::StreamRing *ring) { audio_ring_ = ring; }

    // Returns true if a new decoded frame is available for uploading to GPU
    bool get_frame(std::vector<uint8_t> &out_rgba);

private:
    void decode_loop();
    void reset_clock(double pts);
    void ensure_clock(double pts);
    double master_clock() const;
    void write_audio_blocking(const float *samples, int frames);
    bool wait_interruptible(double seconds);
    double packet_pts_seconds(const AVFrame *frame, int stream_idx) const;
    bool apply_pending_seek(double *last_video_pts);

    std::atomic<bool> is_open_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> stop_thread_{false};
    std::atomic<bool> seeking_{false};
    std::atomic<double> current_position_{0.0};
    std::atomic<double> duration_{0.0};
    std::atomic<double> seek_target_{-1.0};
    double seek_discard_until_ = -1.0;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    double frame_duration_ = 1.0 / 25.0;
    int audio_out_rate_ = 48000;

    BigThread worker_thread_;
    std::mutex frame_mutex_;
    std::vector<uint8_t> frame_buffer_;
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
