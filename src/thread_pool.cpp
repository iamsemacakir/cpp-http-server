#include "thread_pool.hpp"

#include <exception>

#include "logger.hpp"

namespace http {

namespace {

std::size_t resolve_thread_count(std::size_t requested) noexcept {
    if (requested != 0) {
        return requested;
    }
    const unsigned int hardware = std::thread::hardware_concurrency();
    return hardware != 0 ? hardware : 4;
}

} // namespace

ThreadPool::ThreadPool(std::size_t thread_count) {
    const std::size_t count = resolve_thread_count(thread_count);
    workers_.reserve(count);
    try {
        for (std::size_t i = 0; i < count; ++i) {
            workers_.emplace_back(&ThreadPool::worker_loop, this);
        }
    } catch (...) {
        // Thread creation failed part-way: join the ones that did start.
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::post(Task task) {
    if (!task) {
        throw std::invalid_argument("ThreadPool::post: empty task");
    }
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            throw ThreadPoolStopped();
        }
        tasks_.push(std::move(task));
    }
    available_.notify_one();
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    available_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t ThreadPool::pending() const {
    std::lock_guard lock(mutex_);
    return tasks_.size();
}

bool ThreadPool::stopped() const {
    std::lock_guard lock(mutex_);
    return stopping_;
}

void ThreadPool::worker_loop() {
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) {
                return; // stopping and fully drained
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (const std::exception& error) {
            log_error("thread pool task failed: ", error.what());
        } catch (...) {
            log_error("thread pool task failed with an unknown exception");
        }
    }
}

} // namespace http
