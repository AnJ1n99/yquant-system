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
      : store_(RoundUpToPowerOf2(num_elems), T()),
        mask_(store_.size() - 1),
        capacity_(store_.size()) {}

  T* TryGetNextToWriteTo() noexcept {
    auto currentWrite = next_write_index.load(std::memory_order_relaxed);
    auto currentRead = next_read_index.load(std::memory_order_acquire);

    if (UNLIKELY(((currentWrite + 1) & mask_) == (currentRead & mask_))) {
      return nullptr;
    }

    return &store_[currentWrite & mask_];
  }

  // * 获取队列中下一个可用于写入的槽位地址。
  auto GetNextToWriteTo() noexcept -> T* {
    while (true) {
      auto slot = TryGetNextToWriteTo();
      if (LIKELY(slot != nullptr)) {
        return slot;
      }
      CPU_PAUSE();
    }
  }

  auto UpdateWriteIndex() noexcept {
    auto currentWriteIndex = next_write_index.load(std::memory_order_relaxed);
    next_write_index.store(currentWriteIndex + 1, std::memory_order_release);
  }

  // consumer operation
  auto GetNextToRead() const noexcept -> const T* {
    auto currentReadIndex = next_read_index.load(std::memory_order_relaxed);
    auto currentWriteIndex = next_write_index.load(std::memory_order_acquire);

    if (UNLIKELY(currentReadIndex == currentWriteIndex)) {
      return nullptr;
    }

    return &store_[currentReadIndex & mask_];
  }

  auto UpdateReadIndex() noexcept {
    auto currentReadIndex = next_read_index.load(std::memory_order_relaxed);
    next_read_index.store(currentReadIndex + 1, std::memory_order_release);
  }

  auto size() const noexcept -> std::size_t {
    // 必须先读 read 再读 write：索引单调递增，此顺序保证 read <= write，
    // 差值不会下溢。反过来读则可能得到巨大的伪值。
    auto currentReadIndex = next_read_index.load(std::memory_order_acquire);
    auto currentWriteIndex = next_write_index.load(std::memory_order_acquire);
    return currentWriteIndex - currentReadIndex;
  }

  auto IsFull() const noexcept -> bool {
    auto current_write = next_write_index.load(std::memory_order_relaxed);
    auto current_read = next_read_index.load(std::memory_order_relaxed);
    return ((current_write + 1) & mask_) == (current_read & mask_);
  }

  auto capacity() const noexcept -> std::size_t { return capacity_; }

  LFQueue() = delete;
  LFQueue(const LFQueue&) = delete;
  LFQueue(const LFQueue&&) = delete;
  LFQueue& operator=(const LFQueue&) = delete;
  LFQueue& operator=(const LFQueue&&) = delete;

 private:
  static std::size_t RoundUpToPowerOf2(std::size_t num) {
    if (UNLIKELY(num == 0)) {
      return 1;
    }

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
  alignas(64) std::atomic<std::size_t> next_write_index{0};
  alignas(64) std::atomic<std::size_t> next_read_index{0};
};
}  // namespace common
