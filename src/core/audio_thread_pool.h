#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tpanar_ns {

// Worker thread pool for parallel track rendering.
//
// Key design: m_done counts *completed items*, not workers that woke up.
// parallel_for() only blocks until all count items are done, then returns
// immediately — it does NOT wait for idle workers to acknowledge the epoch.
//
// This means: if the calling RT thread drains all items itself before any
// worker thread even wakes, parallel_for returns with zero wait time and
// zero mutex operations in the fast path.

class AudioThreadPool {
public:
    explicit AudioThreadPool(size_t num_workers = 0) {
        if (num_workers > 0)
            start_workers(num_workers);
    }

    ~AudioThreadPool() { stop_all(); }

    AudioThreadPool(const AudioThreadPool&) = delete;
    AudioThreadPool& operator=(const AudioThreadPool&) = delete;

    size_t size() const { return m_workers.size(); }

    void resize(size_t num_workers) {
        stop_all();
        if (num_workers > 0)
            start_workers(num_workers);
    }

    // Execute fn(i) for i in [0, count) in parallel.
    // Blocks until all items are processed, then returns.
    void parallel_for(size_t count, const std::function<void(size_t)>& fn) {
        if (count == 0) return;
        if (m_workers.empty()) {
            for (size_t i = 0; i < count; ++i) fn(i);
            return;
        }

        // Publish new work under the mutex so workers see consistent state.
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_fn    = &fn;
            m_total = count;
            m_next.store(0, std::memory_order_relaxed);
            m_done.store(0, std::memory_order_relaxed);
            ++m_epoch;
        }
        m_work_cv.notify_all();

        // RT thread drains as many items as it can.
        drain(count, fn);

        // Fast path: all items done by the RT thread, no wait needed.
        if (m_done.load(std::memory_order_acquire) >= count)
            return;

        // Slow path: some items were taken by workers; wait for completion.
        // Short timeout so a stuck worker never freezes the JACK callback.
        std::unique_lock<std::mutex> lk(m_mu);
        m_done_cv.wait_for(lk, std::chrono::milliseconds(20), [&] {
            return m_done.load(std::memory_order_relaxed) >= count;
        });
    }

private:
    // Process items from the shared queue and increment m_done per item.
    void drain(size_t total, const std::function<void(size_t)>& fn) {
        for (;;) {
            size_t idx = m_next.fetch_add(1, std::memory_order_relaxed);
            if (idx >= total) break;
            fn(idx);
            // Signal if this was the last item.
            if (m_done.fetch_add(1, std::memory_order_acq_rel) + 1 >= total)
                m_done_cv.notify_one();
        }
    }

    void start_workers(size_t n) {
        m_stop = false;
        m_workers.reserve(n);
        for (size_t i = 0; i < n; ++i)
            m_workers.emplace_back([this] { worker_loop(); });
    }

    void stop_all() {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_stop = true;
            ++m_epoch;
        }
        m_work_cv.notify_all();
        for (auto& t : m_workers)
            if (t.joinable()) t.join();
        m_workers.clear();
        m_stop = false;
    }

    void worker_loop() {
        int seen_epoch;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            seen_epoch = m_epoch;
        }

        while (true) {
            // Wait for new work (epoch change) or shutdown.
            {
                std::unique_lock<std::mutex> lk(m_mu);
                m_work_cv.wait(lk, [&] {
                    return m_stop || m_epoch != seen_epoch;
                });
                if (m_stop) return;
                seen_epoch = m_epoch;
            }
            // m_total and m_fn are stable for this epoch (written before
            // ++m_epoch under the same lock, visible after acquiring above).
            const size_t total = m_total;
            const auto*  fn    = m_fn;

            // Drain: process whatever items remain, one m_done++ per item.
            for (;;) {
                size_t idx = m_next.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total) break;
                (*fn)(idx);
                if (m_done.fetch_add(1, std::memory_order_acq_rel) + 1 >= total)
                    m_done_cv.notify_one();
            }
            // No per-worker done signal needed — only per-item counts.
        }
    }

    std::vector<std::thread>            m_workers;
    std::mutex                          m_mu;
    std::condition_variable             m_work_cv;
    std::condition_variable             m_done_cv;
    bool                                m_stop{false};
    int                                 m_epoch{0};
    const std::function<void(size_t)>*  m_fn{nullptr};
    size_t                              m_total{0};
    std::atomic<size_t>                 m_next{0};
    std::atomic<size_t>                 m_done{0};
};

} // namespace tpanar_ns
