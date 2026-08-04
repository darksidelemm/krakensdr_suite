#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include "concurrentqueue.h"

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop(false), pending_tasks(0) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] {
                            return stop || pending_tasks.load() > 0;
                        });

                        if (stop && pending_tasks.load() == 0)
                            return;

                        // Try to dequeue - if successful, decrement pending count
                        if (tasks.try_dequeue(task)) {
                            pending_tasks.fetch_sub(1);
                        } else {
                            continue;
                        }
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using return_type = decltype(f());

        auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("submit on stopped ThreadPool");

            tasks.enqueue([task]() { (*task)(); });
            pending_tasks.fetch_add(1);
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    moodycamel::ConcurrentQueue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::atomic<size_t> pending_tasks;  // Track pending tasks atomically
};
