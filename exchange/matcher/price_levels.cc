#include "price_levels.h"

#include <bit>
#include <sstream>
#include <string>

#include "../../common/macros.h"

namespace exchange {

// 仅供调试输出。挂单不再保存自己的标的、方向或价格信息：
// 这些信息需要向持有它的价位（level）和订单簿查询。
auto OrderNode::toString() const -> std::string {
  std::ostringstream oss;
  oss << "OrderNode"
      << " ["
      << " clientId:" << client_id << " clientOId:" << client_order_id
      << " marketOId:" << market_order_id
      << " remaining_quantity:" << remaining_quantity
      << " priority:" << priority
      << " inBook:" << (level != nullptr ? "yes" : "no") << "]";
  return oss.str();
}

auto FIFOLevel::toString() const -> std::string {
  std::ostringstream oss;
  oss << "FIFOLevel"
      << " ["
      << " total_quantity:" << total_quantity
      << " next_priority:" << next_priority
      << " firstOrder:" << (first_order != nullptr ? "exists" : "nullptr")
      << "]";
  return oss.str();
}

PriceLevels::PriceLevels(common::Side side,
                         common::MemPool<OrderNode>* order_pool)
    : side_(side), order_pool_(order_pool) {
  ASSERT(order_pool_ != nullptr, "PriceLevels requires an order pool");
  ASSERT(side == common::Side::BUY || side == common::Side::SELL,
         "PriceLevels requires a directional side");

  // 价位是槽位，而非按需分发的对象：网格位置即数组下标，方向即持有
  // 它的本实例，因此无需在构造时写入任何身份信息。
}

OrderNode* PriceLevels::AddOrder(Tick tick, common::ClientId client_id,
                                 common::OrderId client_order_id,
                                 common::OrderId market_order_id,
                                 common::Quantity quantity) noexcept {
  ASSERT(tick >= 0 && tick < kTickCount, "Tick outside the price band");

  auto& level = levels_[static_cast<std::size_t>(tick)];
  const bool was_empty = level.IsEmpty();

  auto* order = order_pool_->allocate(OrderNode{
      .next = nullptr,
      .prev = level.last_order,
      .level = &level,
      .market_order_id = market_order_id,
      .client_order_id = client_order_id,
      .priority = level.next_priority++,
      .remaining_quantity = quantity,
      .client_id = client_id,
  });

  // FIFO：新到达的订单加入队尾，撮合消费队头。
  if (was_empty) {
    level.first_order = order;
  } else {
    level.last_order->next = order;
  }
  level.last_order = order;
  level.total_quantity += quantity;

  if (was_empty) {
    MarkOccupied(tick);
    if (IsEmpty() || IsBetter(tick, best_tick_)) {
      best_tick_ = tick;
    }
  }

  return order;
}

void PriceLevels::ApplyFill(OrderNode* order,
                            common::Quantity quantity) noexcept {
  ASSERT(order != nullptr && order->level != nullptr,
         "Fill applied to an order that is not in the book");
  ASSERT(quantity <= order->remaining_quantity, "Fill exceeds resting size");

  order->remaining_quantity -= quantity;
  order->level->total_quantity -= quantity;
}

void PriceLevels::RemoveOrder(OrderNode* order) noexcept {
  ASSERT(order != nullptr && order->level != nullptr,
         "Removing an order that is not in the book");

  auto* level = order->level;

  if (order->prev != nullptr) {
    order->prev->next = order->next;
  } else {
    level->first_order = order->next;
  }
  if (order->next != nullptr) {
    order->next->prev = order->prev;
  } else {
    level->last_order = order->prev;
  }
  level->total_quantity -= order->remaining_quantity;

  order->next = nullptr;
  order->prev = nullptr;
  order->level = nullptr;

  // 清空价位并不释放任何东西：槽位依然可寻址，因此撮合过程中调用方
  // 持有的指针仍然有效，只是看到一条空队列。
  if (level->IsEmpty()) {
    level->total_quantity = 0;
    level->next_priority = 1;
    const Tick tick = IndexOf(level);
    ClearOccupied(tick);
    if (tick == best_tick_) {
      best_tick_ = FindBestTick();
    }
  }

  order_pool_->deallocate(order);
}

void PriceLevels::MarkOccupied(Tick tick) noexcept {
  const auto index = static_cast<std::size_t>(tick);
  const std::size_t word = index / kBitsPerWord;
  occupancy_[word] |= (std::uint64_t{1} << (index % kBitsPerWord));
  summary_[word / kBitsPerWord] |= (std::uint64_t{1} << (word % kBitsPerWord));
}

void PriceLevels::ClearOccupied(Tick tick) noexcept {
  const auto index = static_cast<std::size_t>(tick);
  const std::size_t word = index / kBitsPerWord;
  occupancy_[word] &= ~(std::uint64_t{1} << (index % kBitsPerWord));
  if (occupancy_[word] == 0) {
    summary_[word / kBitsPerWord] &=
        ~(std::uint64_t{1} << (word % kBitsPerWord));
  }
}

Tick PriceLevels::FindBestTick() const noexcept {
  if (side_ == common::Side::BUY) {
    // 最优买价：最高的已占用 tick。
    for (std::size_t summary_word = kSummaryWords; summary_word-- > 0;) {
      const std::uint64_t summary = summary_[summary_word];
      if (summary == 0) {
        continue;
      }
      const std::size_t word =
          (summary_word * kBitsPerWord) +
          (kBitsPerWord - 1 -
           static_cast<std::size_t>(std::countl_zero(summary)));
      const std::size_t bit =
          kBitsPerWord - 1 -
          static_cast<std::size_t>(std::countl_zero(occupancy_[word]));
      return static_cast<Tick>((word * kBitsPerWord) + bit);
    }
    return kInvalidTick;
  }

  // 最优卖价：最低的已占用 tick。
  for (std::size_t summary_word = 0; summary_word < kSummaryWords;
       ++summary_word) {
    const std::uint64_t summary = summary_[summary_word];
    if (summary == 0) {
      continue;
    }
    const std::size_t word =
        (summary_word * kBitsPerWord) +
        static_cast<std::size_t>(std::countr_zero(summary));
    const std::size_t bit =
        static_cast<std::size_t>(std::countr_zero(occupancy_[word]));
    return static_cast<Tick>((word * kBitsPerWord) + bit);
  }
  return kInvalidTick;
}

}  // namespace exchange
