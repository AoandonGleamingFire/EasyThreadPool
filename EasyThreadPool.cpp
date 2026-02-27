#include <iostream>
#include <vector>
#include "ThreadPool.hpp"

// 模拟一个可能会崩溃的业务逻辑
void risky_business(int id) {
    if (id == 3) {
        throw std::runtime_error("数据库连接超时！");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "      [OK] 任务 " << id << " 处理成功" << std::endl;
}

int main() {
    // 设置：核心 2 人，最大 3 人，队列上限 5 个
    // 这样一共能容纳 3(干活中) + 5(排队中) = 8 个任务
    ThreadPool pool(2, 3, 5, 5);

    std::cout << "===== 场景 1：测试异常捕获 (任务可以死，工人不能死) =====" << std::endl;

    // 提交一个会崩溃的任务
    pool.submit(3, risky_business, 3);

    // 提交一个正常的任务看工人还在不在
    pool.submit(4, risky_business, 4);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "当前存活线程数: " << pool.get_cur_threads() << " (应为 2)" << std::endl;


    std::cout << "\n===== 场景 2：测试拒绝策略 (背压保护) =====" << std::endl;

    try {
        // 我们连续提交 20 个慢任务，但池子只能装 8 个
        for (int i = 10; i < 30; ++i) {
            std::cout << "尝试提交任务 " << i << "... ";
            pool.submit(i, [] {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                });
            std::cout << "成功" << std::endl;
        }
    }
    catch (const std::runtime_error& e) {
        // 这里就是拒绝处理逻辑：
        std::cerr << "\n[警告] 触发拒绝策略: " << e.what() << std::endl;
        std::cout << "提示：现在业务量太大，建议稍后重试或增加服务器节点。" << std::endl;
    }

    // 监控一波
    std::cout << "\n--- 最终池状态 ---" << std::endl;
    std::cout << "排队中任务: " << pool.get_pending_tasks() << std::endl;
    std::cout << "正在运行任务: " << pool.get_running_tasks() << std::endl;

    // 优雅退出
    std::cout << "\n主线程结束，等待剩余任务完成..." << std::endl;
    return 0;
}