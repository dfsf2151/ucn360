#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "render/world.h"

namespace ucn::render {

// render_world's Parallel on `workers` threads plus the caller, which works too and returns once every job ran.
// `start(i)` runs first on worker i (the console places each worker on a hardware thread with it).
class BandPool {
public:
    explicit BandPool(int workers, std::function<void(int)> start = nullptr) {
        for (int i = 0; i < workers; i++)
            threads_.emplace_back([this, i, start] {
                if (start) start(i);
                loop();
            });
    }
    ~BandPool() {
        {
            std::lock_guard<std::mutex> l(m_);
            quit_ = true;
            gen_++;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    void run(int n, const std::function<void(int)>& job) {
        std::unique_lock<std::mutex> l(m_);
        job_ = &job;
        n_ = n;
        next_ = 0;
        left_ = n;
        gen_++;
        cv_.notify_all();
        work(l);
        done_.wait(l, [&] { return left_ == 0; });
        job_ = nullptr;
    }
    Parallel parallel() {
        return [this](int n, const std::function<void(int)>& job) { run(n, job); };
    }

private:
    void work(std::unique_lock<std::mutex>& l) {
        while (job_ && next_ < n_) {
            const int i = next_++;
            const auto* job = job_;
            l.unlock();
            (*job)(i);
            l.lock();
            if (--left_ == 0) done_.notify_all();
        }
    }
    void loop() {
        std::unique_lock<std::mutex> l(m_);
        for (uint64_t seen = gen_;;) {
            cv_.wait(l, [&] { return gen_ != seen; });
            seen = gen_;
            if (quit_) return;
            work(l);
        }
    }

    std::mutex m_;
    std::condition_variable cv_, done_;
    std::vector<std::thread> threads_;
    const std::function<void(int)>* job_ = nullptr;
    int n_ = 0, next_ = 0, left_ = 0;
    uint64_t gen_ = 0;
    bool quit_ = false;
};

} // namespace ucn::render
