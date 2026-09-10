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
    const auto current_write = next_write_index.load(std::memory_order_relaxed);
    const auto next_slot = (current_write + 1) & mask_;

    // 快路径只读生产者私有的缓存，不碰消费者每次出队都写脏的缓存行。
    // 缓存值不会超过消费者的真实读进度，因此“未满”的结论必然成立；
    // 只有缓存显示满时才重新读真实读索引，确认后才返回 nullptr。
    // 该检查同时把 write 与 cached_read_index_ 的差限制在
    // capacity - 1 以内，所以按掩码比较与直接比较差值等价，
    // 不会因缓存陈旧而误判未满。
    if (UNLIKELY(next_slot == (cached_read_index_ & mask_))) {
      // acquire 与消费者 UpdateReadIndex 的 release 配对：确认槽位已被读完，
      // 覆盖它不会破坏消费者仍在使用的数据。
      cached_read_index_ = next_read_index.load(std::memory_order_acquire);
      if (next_slot == (cached_read_index_ & mask_)) {
        return nullptr;
      }
    }

    return &store_[current_write & mask_];
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
  // 会更新消费者私有的写进度缓存，故不再是 const；SPSC 下只有唯一消费者调用。
  auto GetNextToRead() noexcept -> const T* {
    const auto current_read = next_read_index.load(std::memory_order_relaxed);

    // 对称地，快路径只读消费者私有的缓存。缓存值不会超过生产者
    // 的真实写进度，所以“有数据”的结论必然成立；只有缓存显示空时
    // 才重新读真实写索引，确认后才返回 nullptr。
    if (UNLIKELY(current_read == cached_write_index_)) {
      // acquire 与生产者 UpdateWriteIndex 的 release 配对，保证
      // cached_write_index_ 之前发布的槽位数据对本线程可见。快路径命中时，
      // 可见性由上一次重新加载的 acquire 覆盖，无需再次同步。
      cached_write_index_ = next_write_index.load(std::memory_order_acquire);
      if (current_read == cached_write_index_) {
        return nullptr;
      }
    }

    return &store_[current_read & mask_];
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
  // 防止伪共享：索引与只由它的写者访问的对端缓存同处一个缓存行，
  // 生产者热路径只碰第一组，消费者热路径只碰第二组。
  alignas(64) std::atomic<std::size_t> next_write_index{0};
  std::size_t cached_read_index_{0};

  alignas(64) std::atomic<std::size_t> next_read_index{0};
  std::size_t cached_write_index_{0};
};
}  // namespace common
