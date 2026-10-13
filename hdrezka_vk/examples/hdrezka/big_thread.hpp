#pragma once
// A std::thread look-alike with a large stack.
//
// On the PS5 a std::thread gets the console's small default pthread stack.
// libcurl + OpenSSL (TLS handshake), std::regex and the image decoders need
// far more than that: the worker overflowed its stack in the first
// curl_easy_perform() and the whole title vanished without any message
// (the log ended at "worker_thread pick_mirror").

#include <pthread.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "logger.hpp"

namespace rezka
{

class BigThread
{
  public:
    static constexpr std::size_t kDefaultStack = 8u * 1024u * 1024u; // 8 MiB

    BigThread() = default;

    template <class F>
    explicit BigThread(F &&fn, std::size_t stack = kDefaultStack)
    {
        start(std::function<void()>(std::forward<F>(fn)), stack);
    }

    BigThread(BigThread &&other) noexcept : tid_(other.tid_), running_(other.running_)
    {
        other.running_ = false;
    }

    BigThread &operator=(BigThread &&other) noexcept
    {
        if (this != &other) {
            if (running_)
                pthread_detach(tid_);
            tid_ = other.tid_;
            running_ = other.running_;
            other.running_ = false;
        }
        return *this;
    }

    BigThread(const BigThread &) = delete;
    BigThread &operator=(const BigThread &) = delete;

    // Unlike std::thread, never terminates the program: a forgotten thread is detached.
    ~BigThread()
    {
        if (running_)
            pthread_detach(tid_);
    }

    bool joinable() const { return running_; }

    void join()
    {
        if (running_) {
            pthread_join(tid_, nullptr);
            running_ = false;
        }
    }

    void detach()
    {
        if (running_) {
            pthread_detach(tid_);
            running_ = false;
        }
    }

  private:
    static void *trampoline(void *arg)
    {
        std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(arg));
        (*fn)();
        return nullptr;
    }

    void start(std::function<void()> fn, std::size_t stack)
    {
        auto *heap_fn = new std::function<void()>(std::move(fn));

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        int rc = pthread_attr_setstacksize(&attr, stack);
        if (rc != 0)
            write_log("[DEBUG] BigThread: setstacksize(%zu) failed rc=%d\n", stack, rc);
        rc = pthread_create(&tid_, &attr, &BigThread::trampoline, heap_fn);
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            write_log("[DEBUG] BigThread: pthread_create with %zu stack failed rc=%d, retry default\n",
                      stack, rc);
            rc = pthread_create(&tid_, nullptr, &BigThread::trampoline, heap_fn);
        }
        if (rc != 0) {
            write_log("[DEBUG] BigThread: pthread_create failed rc=%d\n", rc);
            delete heap_fn;
            running_ = false;
            return;
        }
        running_ = true;
    }

    pthread_t tid_{};
    bool running_ = false;
};

} // namespace rezka
