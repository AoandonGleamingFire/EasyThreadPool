#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <iostream>
#include <chrono>
#include <stdexcept> // 引入异常头文件

class ThreadPool {
public:
    /**
     * @param max_queue_size 队列最大容量，防止内存爆炸
     */
    explicit ThreadPool(size_t core_size, size_t max_size = 10, int timeout = 60, size_t max_queue_size = 1000)
        : stop(false), core_threads(core_size), max_threads(max_size),
        keep_alive_time(timeout), max_queue_capacity(max_queue_size),
        cur_threads(0), idle_threads(0), tasks_processed(0), tasks_running(0)
    {
        for (size_t i = 0; i < core_threads; ++i) { addWorker(true); }
    }

    template<class F, class... Args>
    auto submit(int task_id, F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // --- [1. 拒绝策略：背压保护] ---
            if (tasks.size() >= max_queue_capacity) {
                // 如果队列满了，直接抛出异常，防止内存继续膨胀
                throw std::runtime_error("拒绝提交：任务队列已满 (容量: " + std::to_string(max_queue_capacity) + ")");
            }

            if (idle_threads == 0 && cur_threads < max_threads) { addWorker(false); }
            if (stop) throw std::runtime_error("ThreadPool 已停止");

            tasks.emplace([task]() { (*task)(); }, task_id);
        }
        condition.notify_one();
        return res;
    }

    // 监控接口
    size_t get_cur_threads() const { return cur_threads.load(); }
    size_t get_running_tasks() const { return tasks_running.load(); }
    size_t get_pending_tasks() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

    ~ThreadPool() {
        stop = true;
        condition.notify_all();
        for (std::thread& worker : workers) { if (worker.joinable()) worker.join(); }
    }

private:
    void addWorker(bool is_core) {
        cur_threads++;
        workers.emplace_back([this, is_core] {
            for (;;) {
                std::function<void()> task_func;
                int current_task_id = -1;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    idle_threads++;
                    bool has_signal = this->condition.wait_for(lock,
                        std::chrono::seconds(keep_alive_time), [this] {
                            return this->stop || !this->tasks.empty();
                        });
                    idle_threads--;

                    if (!has_signal && !is_core && cur_threads > core_threads && this->tasks.empty()) {
                        cur_threads--;
                        return;
                    }
                    if (this->stop && this->tasks.empty()) { cur_threads--; return; }
                    if (this->tasks.empty()) continue;

                    task_func = std::move(this->tasks.front().first);
                    current_task_id = this->tasks.front().second;
                    this->tasks.pop();
                }

                tasks_running++;

                // --- [2. 异常捕获：保护工人线程] ---
                try {
                    task_func();
                }
                catch (const std::exception& e) {
                    // 实际项目中这里通常会写日志文件，现在打印到错误流
                    std::cerr << "!!! [任务崩溃] 任务ID " << current_task_id
                        << " 抛出异常: " << e.what() << std::endl;
                }
                catch (...) {
                    std::cerr << "!!! [任务崩溃] 任务ID " << current_task_id
                        << " 抛出未知错误" << std::endl;
                }

                tasks_running--;
                tasks_processed++;
            }
            });
    }

    std::queue<std::pair<std::function<void()>, int>> tasks;
    std::vector<std::thread> workers;
    std::mutex queue_mutex;
    std::condition_variable condition;

    bool stop;
    size_t core_threads;
    size_t max_threads;
    int keep_alive_time;
    size_t max_queue_capacity; // 最大队列长度

    std::atomic<size_t> cur_threads;
    std::atomic<size_t> idle_threads;
    std::atomic<size_t> tasks_running;
    std::atomic<size_t> tasks_processed;
};

#endif