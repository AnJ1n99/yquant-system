// MeSideBook 封装单侧（BUY 或 SELL）的订单簿逻辑
// 管理该侧的价格档位链表和订单链表
// MEOrderBook 持有两个 MeSideBook 实例（bid_book_ 和 ask_book_）
#pragma once

#include <cstddef>

#include "../../common/mem_pool.h"
#include "../../common/types.h"
#include "me_order.h"

namespace Exchange {

class MeSideBook {
  friend class MEOrderBook;

 public:
  explicit MeSideBook(Common::Side side, Common::MemPool<MEOrder>* orderPool,
                      Common::MemPool<MEOrdersAtPrice>* pricePool);

  ~MeSideBook();

  // 只读访问器
  MEOrdersAtPrice* getBestPrice() const noexcept { return best_price_; }
  MEOrdersAtPrice* getOrdersAtPrice(Common::Price price) const noexcept;
  Common::Priority getNextPriority(Common::Price price) const noexcept;
  bool isEmpty() const noexcept { return best_price_ == nullptr; }

  // 修改操作
  void addOrder(MEOrder* order) noexcept;
  void removeOrder(MEOrder* order) noexcept;

  // 禁用拷贝和移动
  MeSideBook(const MeSideBook&) = delete;
  MeSideBook& operator=(const MeSideBook&) = delete;
  MeSideBook(MeSideBook&&) = delete;
  MeSideBook& operator=(MeSideBook&&) = delete;

 private:
  void addOrderAtPrice(MEOrdersAtPrice* ordersAtPrice) noexcept;
  void removeOrderAtPrice(Common::Price price) noexcept;
  std::size_t priceToIndex(Common::Price price) const noexcept;

  Common::Side side_;
  MEOrdersAtPrice* best_price_ = nullptr;
  OrdersAtPriceHashMap price_levels_;
  Common::MemPool<MEOrder>* order_pool_;
  Common::MemPool<MEOrdersAtPrice>* price_pool_;
};

}  // namespace Exchange
