#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "macros.h"

namespace common {

// 固定容量的对象内存池，使用侵入式空闲链表和按需提交内存。
//
// 构造时一次性预留一块连续的原始存储空间；之后内存池不再申请内存，
// 因此分配路径不会触发新的内存分配。内存池不会预先构造所有对象，只有
// allocate() 发放某个槽位时才首次写入该槽位，所以常驻内存由历史最高使用
// 量决定，而不是由配置容量决定。如果希望在启动阶段承担首次触页开销，
// 可以调用 Warmup() 预先触碰已预留区域的前缀。
//
// 空闲链表指针复用已释放槽位的存储空间，因此无需额外的槽位状态表，
// 每个槽位占用的空间恰好为 max(sizeof(T), sizeof(void*))。
//
// 内存池不会调用 T 的析构函数，详见下面的 static_assert。
template <class T>
class MemPool final {
  static_assert(std::is_trivially_destructible_v<T>,
                "MemPool never runs ~T(); T must be trivially destructible.");

 public:
  explicit MemPool(std::size_t capacity)
      : storage_(capacity == 0
                     ? nullptr
                     : std::allocator<ObjectBlock>{}.allocate(capacity)),
        capacity_(capacity) {}

  ~MemPool() {
    if (storage_ != nullptr) {
      std::allocator<ObjectBlock>{}.deallocate(storage_, capacity_);
    }
  }

  template <class... Args>
  T* allocate(Args&&... args) noexcept {
    // 优先从空闲链表取出回收槽位，只有没有可复用槽位时才推进 bump 游标：
    // 回收槽位通常已经位于缓存中，并且对应页面已经触发过缺页处理。
    // 这种顺序可以稳定工作集，避免持续向新的内存页扩展。
    ObjectBlock* block = nullptr;
    if (free_head_ != nullptr) {
      block = free_head_;
      free_head_ = free_head_->next_free_;
    } else {
      ASSERT(next_unused_ < capacity_, "Memory pool exhausted");
      block = &storage_[next_unused_++];
    }

    MarkSlot(block, true);
    ++in_use_;
    return new (&block->object_) T(std::forward<Args>(args)...);
  }

  // 将对象归还内存池，但不调用其析构函数。
  void deallocate(T* elem) noexcept {
    // 标准布局联合体与其成员指针可互相转换，因此可以从 T* 得到槽位地址。
    auto* block = reinterpret_cast<ObjectBlock*>(elem);
    MarkSlot(block, false);
    block->next_free_ = free_head_;
    free_head_ = block;
    --in_use_;
  }

  // 预先触碰前 count 个槽位对应的内存页，将首次触页开销从低延迟路径
  // 转移到这里。这里只写入尚未发放过的区域，因此可以在任意时刻调用：
  // 正在使用的槽位保存对象，已回收的槽位保存空闲链表指针。
  void Warmup(std::size_t count) noexcept {
    const std::size_t limit = std::min(count, capacity_);
    if (storage_ == nullptr || limit <= next_unused_) {
      return;
    }

    // 必须执行写操作。读取尚未触碰的匿名页时，系统可能只映射共享的只读
    // 零页，并不会真正提交物理内存。
    auto* bytes = reinterpret_cast<volatile unsigned char*>(storage_);
    const std::size_t begin = next_unused_ * sizeof(ObjectBlock);
    const std::size_t end = limit * sizeof(ObjectBlock);
    for (std::size_t offset = begin; offset < end; offset += kPageStride) {
      bytes[offset] = 0;
    }
    bytes[end - 1] = 0;
  }

  std::size_t capacity() const noexcept { return capacity_; }
  std::size_t in_use() const noexcept { return in_use_; }
  std::size_t available() const noexcept { return capacity_ - in_use_; }

  // 历史最高并发占用量。由于只有在空闲链表为空时才推进 bump 游标，
  // 从未发放区域取槽位时，之前发放的槽位必然全部仍在使用，因此
  // next_unused_ 正好等于 in_use() 的历史最大值。
  std::size_t high_water() const noexcept { return next_unused_; }

  MemPool() = delete;
  MemPool(const MemPool&) = delete;
  MemPool& operator=(const MemPool&) = delete;
  MemPool(MemPool&&) = delete;
  MemPool& operator=(MemPool&&) = delete;

 private:
  union ObjectBlock {
    T object_;
    ObjectBlock* next_free_;
  };

  // Warmup() 使用的保守页大小步长。步长小于实际页大小只会造成重复写入；
  // 如果步长大于实际页大小，则可能跳过某些内存页。
  static constexpr std::size_t kPageStride = 4096;

  // 仅调试构建启用的槽位状态记录，用于同时检查指针合法性以及空闲/占用
  // 状态转换。侵入式空闲链表没有额外的状态位来检测重复释放；如果不检测，
  // 槽位可能链接到自身，随后同一块存储会被发放给两个仍存活的对象。发布
  // 构建会连同状态表和这些检查一起删除，因此不同 NDEBUG 配置的对象不能
  // 混用。
  void MarkSlot([[maybe_unused]] const ObjectBlock* block,
                [[maybe_unused]] bool in_use) noexcept {
#ifndef NDEBUG
    ASSERT(storage_ != nullptr && block != nullptr, "Invalid element pointer");

    const auto address = reinterpret_cast<std::uintptr_t>(block);
    const auto begin = reinterpret_cast<std::uintptr_t>(storage_);
    const auto offset = address - begin;
    ASSERT(address >= begin && offset < next_unused_ * sizeof(ObjectBlock) &&
               offset % sizeof(ObjectBlock) == 0,
           "Element pointer does not belong to this pool");

    const std::size_t index = offset / sizeof(ObjectBlock);
    ASSERT(in_use_flags_[index] != in_use,
           (in_use ? "Free-list slot already in use at index: "
                   : "Double free at index: ") +
               std::to_string(index));
    in_use_flags_[index] = in_use;
#endif
  }

  ObjectBlock* storage_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t next_unused_ = 0;
  ObjectBlock* free_head_ = nullptr;
  std::size_t in_use_ = 0;
#ifndef NDEBUG
  std::vector<bool> in_use_flags_ = std::vector<bool>(capacity_, false);
#endif
};
}  // namespace common
