#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace eng {

// Tiny fixed-size thread pool with a blocking parallel_for. Enough to fan out
// culling / behaviour work across cores; not a full job graph.
class JobSystem {
public:
    explicit JobSystem(unsigned threads = 0) {
        if (threads == 0)
            threads = std::max(1u, std::thread::hardware_concurrency() - 1);
        for (unsigned i = 0; i < threads; ++i)
            workers_.emplace_back([this] { worker(); });
    }
    ~JobSystem() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    unsigned thread_count() const { return (unsigned)workers_.size(); }

    // Runs fn(i) for i in [0, count). Blocks until all complete.
    void parallel_for(size_t count, const std::function<void(size_t)>& fn, size_t grain = 256) {
        if (count == 0) return;
        if (count <= grain || workers_.empty()) {
            for (size_t i = 0; i < count; ++i) fn(i);
            return;
        }
        size_t chunks = (count + grain - 1) / grain;
        std::atomic<size_t> remaining{chunks};
        std::mutex done_m;
        std::condition_variable done_cv;
        for (size_t c = 0; c < chunks; ++c) {
            size_t begin = c * grain;
            size_t end = std::min(begin + grain, count);
            {
                std::lock_guard<std::mutex> lk(m_);
                tasks_.push([&, begin, end] {
                    for (size_t i = begin; i < end; ++i) fn(i);
                    if (--remaining == 0) {
                        std::lock_guard<std::mutex> dl(done_m);
                        done_cv.notify_one();
                    }
                });
            }
            cv_.notify_one();
        }
        std::unique_lock<std::mutex> lk(done_m);
        done_cv.wait(lk, [&] { return remaining.load() == 0; });
    }

private:
    void worker() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

} // namespace eng
