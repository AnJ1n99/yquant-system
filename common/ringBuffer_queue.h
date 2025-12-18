#pragma once

#include <atomic>
#include <iostream>
#include <immintrin.h>
#include <vector>

#include "macros.h"

namespace Common {
    // * 任意类型  FIFO  SPSC 场景线程安全及优化（内存对齐避免伪共享）
    // * 固定大小和首尾相连
    template <typename T>
    class LFQueue final { // !final 防止被继承
    public:
        // !explicit 防止隐式转换(对单参数构造函数)
        explicit LFQueue(std::size_t num_elems) : 
            store_(round_up_to_power_of_2(num_elems), T()),
            mask_(store_.size() - 1),
            capacity_(store_.size()) {
        }


        auto tryGetNextToWriteTo() noexcept {

        }
    private:
        static std::size_t round_up_to_power_of_2(std::size_t num) {
            if (UNLIKELY(num == 0)) return 1;

            --num;
            num |= num >> 1;
            num |= num >> 2;
            num |= num >> 4;
            num |= num >> 8;
            num |= num >> 16;
            num |= num >> 32;
            ++num;

            return num;
        }

    private:
        std::vector<T> store_{};    // ? 是否需要对齐，分配内存呢？
        const std::size_t capacity_{};
        const std::size_t mask_{};
        // 防止伪共享
        alignas(64) std::atomic<std::size_t> nextWriteIndex {0};
        alignas(64) std::atomic<std::size_t> nextReadIndex {0};
        alignas(64) std::atomic<std::size_t> numElements {0};
    };
}