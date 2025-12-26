/*
 * 该文件实现了一层线程管理层，为关键组件创建并管理专用线程，同时确保每个线程在特定的CPU核心上执行。
 * 这种做法消除了由操作系统引发的线程迁移，减少了缓存失效，并提供了对于低延迟交易应用至关重要的确定性性能特征。
 */

#pragma once

#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <thread>
#include <atomic>

#include <sys/syscall.h>

namespace Common {

    // 将当前线程绑定到指定 CPU 核心的内联函数
    inline auto setThreadCore(int core_id) noexcept {
        cpu_set_t cpuset;

        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        return (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0);
    }

    //creates a new thread with CPU affinity and name assignment
    template <class T, class...A>
    inline auto creatAndStartThread(int core_id, const std::string &name, T &&func, A &&... args) noexcept {
        auto t = new std::thread(
            [core_id, name](T &&f, A &&... a) -> void {
            if (core_id >= 0 && !setThreadCore(core_id)) {
                // 失败被视为致命的配置错误
                std::cerr << "Failed to set core affinity for " << name << " " << pthread_self() << " to " << core_id << std::endl;
                exit(EXIT_FAILURE);
            }

            std::cerr << "Succeeded to set core affinity for " << name << " " << pthread_self() << " to " << core_id << std::endl;

            // 执行真正的任务
            // 使用 std::forward 完美转发参数给目标函数
            std::forward<T>(f)(std::forward<A>(a)...);
        },
        // 这里是 std::thread 的构造参数区域
        // std::thread 会负责把 func 和 args move/copy 到新线程的栈空间里
        std::forward<T>(func),
        std::forward<A>(args)...);
        return t;
    }
}
