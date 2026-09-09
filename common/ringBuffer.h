#pragma once

#include <atomic>
#include <iostream>
#include <vector>

#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()

#include "macros.h"

namespace common {
// * 任意类型  FIFO  SPSC 场景线程安全及优化（内存对齐避免伪共享）
// * 固定大小和首尾相连
template <typename T>
class LFQueue final {
 public:
  //
  explicit LFQueue(std::size_t num_elems)
      : store_(round_up_to_power_of_2(num_elems), T()),
        mask_(store_.size() - 1),
        capacity_(store_.size()) {}

  T* tryGetNextToWriteTo() noexcept {
    auto currentWrite = nextWriteIndex.load(std::memory_order_relaxed);
    auto currentRead = nextReadIndex.load(std::memory_order_acquire);

    if (UNLIKELY(((currentWrite + 1) & mask_) == (currentRead & mask_))) {
      return nullptr;
    }

    return &store_[currentWrite & mask_];
  }

  // * 获取队列中下一个可用于写入的槽位地址。
  auto getNextToWriteTo() noexcept -> T* {
    while (true) {
      auto slot = tryGetNextToWriteTo();
      if (LIKELY(slot != nullptr)) {
        return slot;
      }
      CPU_PAUSE();
    }
  }

  auto updateWriteIndex() noexcept {
    auto currentWriteIndex = nextWriteIndex.load(std::memory_order_relaxed);
    nextWriteIndex.store(currentWriteIndex + 1, std::memory_order_release);
    numElements.fetch_add(1, std::memory_order_release);
  }

  // consumer operation
  auto getNextToRead() const noexcept -> const T* {
    auto currentReadIndex = nextReadIndex.load(std::memory_order_relaxed);
    auto currentElementCount = numElements.load(std::memory_order_acquire);

    if (LIKELY(currentElementCount > 0)) {
      std::size_t targetIndex = currentReadIndex & mask_;
      return &store_[targetIndex];
    } else {
      return nullptr;
    }
  }

  auto updateReadIndex() noexcept {
    auto currentReadIndex = nextReadIndex.load(std::memory_order_relaxed);
    nextReadIndex.store(currentReadIndex + 1, std::memory_order_release);
    numElements.fetch_sub(1, std::memory_order_release);
  }

  auto size() const noexcept {
    return numElements.load(std::memory_order_acquire);
  }

  auto is_full() const noexcept -> bool {
    auto current_write = nextWriteIndex.load(std::memory_order_relaxed);
    auto current_read = nextReadIndex.load(std::memory_order_relaxed);
    return ((current_write + 1) & mask_) == (current_read & mask_);
  }

  auto capacity() const noexcept -> std::size_t { return capacity_; }

  LFQueue() = delete;
  LFQueue(const LFQueue&) = delete;
  LFQueue(const LFQueue&&) = delete;
  LFQueue& operator=(const LFQueue&) = delete;
  LFQueue& operator=(const LFQueue&&) = delete;

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
  std::vector<T> store_{};
  const std::size_t mask_{};
  const std::size_t capacity_{};
  // 防止伪共享
  alignas(64) std::atomic<std::size_t> nextWriteIndex{0};
  alignas(64) std::atomic<std::size_t> nextReadIndex{0};
  alignas(64) std::atomic<std::size_t> numElements{0};
};
}  // namespace common
