// MatchingEngineSideBook 封装单侧（BUY 或 SELL）的订单簿逻辑
// 管理该侧的价格档位链表和订单链表
// MatchingEngineOrderBook 持有两个 MatchingEngineSideBook 实例（bid_book_ 和
// ask_book_）
#pragma once

#include <cstddef>

#include "../../common/mem_pool.h"
#include "../../common/types.h"
#include "matching_engine_order.h"

namespace exchange {

class MatchingEngineSideBook {
  friend class MatchingEngineOrderBook;

public:
  MatchingEngineSideBook(
      common::Side side, common::MemPool<MatchingEngineOrder>* orderPool,
      common::MemPool<MatchingEngineOrdersAtPrice>* pricePool);

  ~MatchingEngineSideBook();

  // 只读访问器
  MatchingEngineOrdersAtPrice* getBestPrice() const noexcept {
    return best_price_;
  }
  MatchingEngineOrdersAtPrice* getOrdersAtPrice(
      common::Price price) const noexcept;
  common::Priority getNextPriority(common::Price price) const noexcept;
  bool isEmpty() const noexcept { return best_price_ == nullptr; }

  // 修改操作
  void addOrder(MatchingEngineOrder* order) noexcept;
  void removeOrder(MatchingEngineOrder* order) noexcept;

  // 禁用拷贝和移动
  MatchingEngineSideBook(const MatchingEngineSideBook&) = delete;
  MatchingEngineSideBook& operator=(const MatchingEngineSideBook&) = delete;
  MatchingEngineSideBook(MatchingEngineSideBook&&) = delete;
  MatchingEngineSideBook& operator=(MatchingEngineSideBook&&) = delete;

 private:
  void addOrderAtPrice(MatchingEngineOrdersAtPrice* ordersAtPrice) noexcept;
  void removeOrderAtPrice(common::Price price) noexcept;
  std::size_t priceToIndex(common::Price price) const noexcept;

  common::Side side_;
  MatchingEngineOrdersAtPrice* best_price_ = nullptr;
  OrdersAtPriceHashMap price_levels_;
  common::MemPool<MatchingEngineOrder>* order_pool_;
  common::MemPool<MatchingEngineOrdersAtPrice>* price_pool_;
};

}  // namespace exchange
