#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace http {

// Thrown when work is submitted to a pool that is shutting down.
class ThreadPoolStopped : public std::runtime_error {
public:
    ThreadPoolStopped() : std::runtime_error("thread pool is stopped") {}
};

// Fixed-size pool of worker threads consuming a FIFO task queue.
//
//   http::ThreadPool pool(4);
//   pool.post([] { do_work(); });                 // fire and forget
//   auto sum = pool.submit([](int a, int b) { return a + b; }, 2, 3);
//   sum.get();                                    // 5
//
// shutdown() (also run by the destructor) stops accepting new tasks, lets the
// workers finish everything already queued, and joins them.
class ThreadPool {
public:
    using Task = std::function<void()>;

    // Starts `thread_count` workers; 0 means one per hardware thread.
    explicit ThreadPool(std::size_t thread_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Queues a task without a result. Exceptions escaping the task are
    // logged and swallowed so a worker never dies. Throws ThreadPoolStopped
    // after shutdown().
    void post(Task task);

    // Queues a callable and returns a future for its result. Exceptions
    // thrown by the callable are delivered through the future.
    template <typename F, typename... Args>
    auto submit(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using Result = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        // packaged_task is move-only but std::function needs a copyable
        // callable, so it lives behind a shared_ptr.
        auto task = std::make_shared<std::packaged_task<Result()>>(
            [function = std::forward<F>(function),
             arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                return std::apply(std::move(function), std::move(arguments));
            });
        std::future<Result> result = task->get_future();
        post([task] { (*task)(); });
        return result;
    }

    // Stops accepting tasks, drains the queue and joins all workers.
    // Safe to call more than once; must not be called from a worker thread.
    void shutdown();

    std::size_t size() const noexcept { return workers_.size(); }

    // Tasks waiting in the queue (not counting the ones currently running).
    std::size_t pending() const;

    bool stopped() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    bool stopping_ = false;
};

} // namespace http
