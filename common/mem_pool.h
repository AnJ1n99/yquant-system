#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "macros.h"

namespace common {
template <class T>
class MemPool final {
 public:
  explicit MemPool(std::size_t numElems) : store_(numElems, {T(), true}) {
    ASSERT(reinterpret_cast<const ObjectBlock*>(&(store_[0].object_)) ==
               &(store_[0]),
           "T object should be first member of ObjectBlock.");
  }

  // allocate a new object of type T, use placement new to init the object, mark
  // the block as in-use and return the object
  template <class... Args>
  T* allocate(Args&&... args) noexcept {  // use perfect farword
    auto objBlock = &(store_[nextFreeIndex]);
    ASSERT(objBlock->isFree, "Expected free ObjectBlock at in index: " +
                                 std::to_string(nextFreeIndex));
    T* ret = &(objBlock->object_);
    ret = new (ret) T(std::forward<Args>(args)...);  // placement new
    objBlock->isFree = false;
    updateNextFreeIndex();
    return ret;
  }

  // Return the object back to the pool by making the block as free again
  // Destructor is not called for the object
  auto deallocate(const T* elem) noexcept {
    // elem is the address of the object to be deallocated, we need to find the
    // index of the block
    const auto elemIndex =
        (reinterpret_cast<const ObjectBlock*>(elem) - &store_[0]);
    ASSERT(elemIndex >= 0 && static_cast<size_t>(elemIndex) < store_.size(),
           "Invalid element pointer");
    ASSERT(!store_[elemIndex].isFree, "Expected in-use ObjectBlock at index: " +
                                          std::to_string(elemIndex));
    store_[elemIndex].isFree = true;
  }

 private:
  // find the next free block to be used for the next allocate
  auto updateNextFreeIndex() noexcept {
    const auto initialFreeIndex = nextFreeIndex;
    while (!store_[nextFreeIndex].isFree) {
      ++nextFreeIndex;
      if (UNLIKELY(nextFreeIndex == store_.size())) {
        // hardware branch predictor should almost always predict this to be
        // false any ways ringQueue
        nextFreeIndex = 0;
      }
      if (UNLIKELY(initialFreeIndex == nextFreeIndex)) {
        ASSERT(initialFreeIndex != nextFreeIndex, "No free block found");
      }
    }
  }

 private:
  // It is better to have one vector of structs with two objects than two
  // vectors of one object. Consider how these are accessed and cache
  // performance. ? 减少了缓存未命中的概率，因为相关数据被组织在一起
  struct ObjectBlock {
    T object_;
    bool isFree = true;
  };

  std::vector<ObjectBlock> store_;

  size_t nextFreeIndex = 0;
};
}  // namespace common
