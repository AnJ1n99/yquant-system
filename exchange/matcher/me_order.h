#pragma once

#include <array>
#include <sstream>
#include <string>

#include "../../common/types.h"

namespace Exchange {

// 用于匹配引擎内部的订单结构，每一个 MEOrder 是链表中的一个节点
class MEOrder {
  friend class MatchingEngine;
  friend class MEOrderBook;
  friend class MeSideBook;

 public:
  MEOrder() = default;

  MEOrder(Common::ClientId client_id, Common::OrderId client_order_id,
          Common::OrderId market_order_id, Common::SymbolId symbol_id,
          Common::Side side, Common::Price price, Common::Qty qty_remain,
          Common::Priority priority)
      : client_id(client_id),
        client_order_id(client_order_id),
        market_order_id(market_order_id),
        symbol_id(symbol_id),
        side(side),
        price(price),
        qty_remain(qty_remain),
        priority(priority) {}

  auto toString() const -> std::string;

 private:
  // 路由标识
  Common::ClientId client_id = Common::ClientId_INVALID;
  Common::OrderId client_order_id = Common::OrderId_INVALID;
  Common::OrderId market_order_id = Common::OrderId_INVALID;
  Common::SymbolId symbol_id = Common::SymbolId_INVALID;

  // 订单属性
  Common::Side side = Common::Side::INVALID;
  Common::Price price = Common::Price_INVALID;
  Common::Qty qty_remain = Common::Qty_INVALID;
  Common::Priority priority = Common::Priority_INVALID;

  // 链表指针（由 MeSideBook::addOrder() 设置）
  MEOrder* next = nullptr;
  MEOrder* prev = nullptr;
};

// 单个客户端的订单哈希表 （用于快速查找订单）  OrderId -> MEOrder.
using OrderHashMap = std::array<MEOrder*, Common::ME_MAX_ORDER_IDS>;
// 所有客户端的订单二维查找表（用于快速查找订单）  ClientId -> OrderId ->
// MEOrder
using ClientOrderHashMap =
    std::array<OrderHashMap*, Common::ME_MAX_NUM_CLIENTS>;

// 同一价格档位的订单列表，内部维护同价位的 FIFO 循环链表
class MEOrdersAtPrice {
  friend class MEOrderBook;
  friend class MeSideBook;

 public:
  MEOrdersAtPrice() = default;

  MEOrdersAtPrice(Common::Price price, MEOrder* first_order,
                  MEOrdersAtPrice* nextEntry, MEOrdersAtPrice* prevEntry)
      : price(price),
        firstMeOrder(first_order),
        next(nextEntry),
        prev(prevEntry),
        hash_next(nullptr) {}

  auto toString() const -> std::string {
    std::ostringstream oss;
    oss << "MEOrdersAtPrice"
        << " ["
        << "price:" << Common::priceToString(price)
        << " firstOrder:" << (firstMeOrder ? "exists" : "nullptr") << "]";
    return oss.str();
  }

 private:
  Common::Price price = Common::Price_INVALID;
  MEOrder* firstMeOrder = nullptr;  // 该价位 FIFO 链表的头节点

  // 价格档位排序链表（按价格高低排序）
  MEOrdersAtPrice* next = nullptr;
  MEOrdersAtPrice* prev = nullptr;

  // 哈希碰撞链表（同一桶内的不同价格）
  MEOrdersAtPrice* hash_next = nullptr;
};

using OrdersAtPriceHashMap =
    std::array<MEOrdersAtPrice*, Common::ME_MAX_PRICE_LEVELS>;

}  // namespace Exchange
