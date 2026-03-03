#include <iostream>
#include <vector>
#include <memory>   // 必须：智能指针头文件
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>   // 必须：原子变量，用于优雅退出

// 模拟一个任务类
class Task {
public:
    int id;
    Task(int i) : id(i) {}
    void do_work() {
        std::cout << "  [工作线程] 正在处理任务 " << id << "..." << std::endl;
    }
    ~Task() { std::cout << "  [内存安全] 任务 " << id << " 被销毁了" << std::endl; }
};

class TaskDispatcher {
private:
    // 1. 内存安全：不再存 Task*，而是存 std::unique_ptr<Task>
    // 这样当任务从队列弹出或处理完，内存会自动释放，根本不需要写 delete
    std::queue<std::unique_ptr<Task>> task_queue;
    
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> stop_flag{false}; // 2. 优雅退出的开关

public:
    // 发送任务（生产者）
    void add_task(std::unique_ptr<Task> t) {
        std::lock_guard<std::mutex> lock(mtx);
        task_queue.push(std::move(t)); // unique_ptr 只能用 move 转移所有权
        cv.notify_one();
    }

    // 处理任务（消费者）
    void run() {
        while (true) {
            std::unique_ptr<Task> t;
            {
                std::unique_lock<std::mutex> lock(mtx);
                // 3. 优雅退出的逻辑：如果要停了且队列空了，就撤
                cv.wait(lock, [this] { return stop_flag || !task_queue.empty(); });
                
                if (stop_flag && task_queue.empty()) return;

                t = std::move(task_queue.front());
                task_queue.pop();
            }
            t->do_work();
        }
    }

    void stop() {
        stop_flag = true;
        cv.notify_all(); // 唤醒所有睡着的线程去检查 stop_flag
    }
};