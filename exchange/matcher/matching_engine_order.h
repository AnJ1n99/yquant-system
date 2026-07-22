#pragma once

#include <array>
#include <sstream>
#include <string>

#include "../../common/types.h"

namespace exchange {

// 匹配引擎内部的订单结构

class MatchingEngineOrder {
  friend class MatchingEngineOrderBook;
  friend class MatchingEngineSideBook;

 public:
  MatchingEngineOrder() = default;

  MatchingEngineOrder(common::ClientId client_id,
                      common::OrderId client_order_id,
                      common::OrderId market_order_id,
                      common::SymbolId symbol_id, common::Side side,
                      common::Price price, common::Quantity remaining_quantity,
                      common::Priority priority)
      : client_id(client_id),
        client_order_id(client_order_id),
        market_order_id(market_order_id),
        symbol_id(symbol_id),
        side(side),
        price(price),
        remaining_quantity(remaining_quantity),
        priority(priority) {}

  auto toString() const -> std::string;

 private:
  // --- Identifiers ---
  // Trading client that owns this order (used to route responses).
  common::ClientId client_id = common::ClientId_INVALID;
  // Client-assigned order id; unique per client, used for cancel/lookup.
  common::OrderId client_order_id = common::OrderId_INVALID;
  // Exchange-assigned market order id; unique across the matching engine,
  // used in market data and client responses.
  common::OrderId market_order_id = common::OrderId_INVALID;
  // Instrument this order trades (index into symbol order books).
  common::SymbolId symbol_id = common::SymbolId_INVALID;

  // --- Order attributes ---
  common::Side side = common::Side::INVALID;
  common::Price price = common::Price_INVALID;
  // Unfilled quantity still resting (or remaining on an aggressive order).
  common::Quantity remaining_quantity = common::Quantity_INVALID;
  // Price-time priority within a price level (typically a timestamp rank).
  common::Priority priority = common::Priority_INVALID;

  // 链表指针（由 MatchingEngineSideBook::addOrder() 设置）
  MatchingEngineOrder* next = nullptr;
  MatchingEngineOrder* prev = nullptr;
};

// 单个客户端的订单哈希表 （用于快速查找订单）  OrderId -> MatchingEngineOrder.
using OrderHashMap = std::array<MatchingEngineOrder*, common::kMaxOrderIds>;
// 所有客户端的订单二维查找表（用于快速查找订单）  ClientId -> OrderId ->
// MatchingEngineOrder
using ClientOrderHashMap = std::array<OrderHashMap*, common::kMaxNumClients>;

// 同一价格档位的订单列表，内部维护同价位的 FIFO 循环链表
class MatchingEngineOrdersAtPrice {
  friend class MatchingEngineOrderBook;
  friend class MatchingEngineSideBook;

 public:
  MatchingEngineOrdersAtPrice() = default;

  MatchingEngineOrdersAtPrice(common::Price price,
                              MatchingEngineOrder* first_order,
                              MatchingEngineOrdersAtPrice* nextEntry,
                              MatchingEngineOrdersAtPrice* prevEntry)
      : price(price),
        first_order(first_order),
        next(nextEntry),
        prev(prevEntry),
        hash_next(nullptr) {}

  auto toString() const -> std::string {
    std::ostringstream oss;
    oss << "MatchingEngineOrdersAtPrice"
        << " ["
        << "price:" << common::priceToString(price)
        << " firstOrder:" << (first_order ? "exists" : "nullptr") << "]";
    return oss.str();
  }

 private:
  common::Price price = common::Price_INVALID;
  MatchingEngineOrder* first_order = nullptr;  // 该价位 FIFO 链表的头节点

  // 价格档位排序链表（按价格高低排序）
  MatchingEngineOrdersAtPrice* next = nullptr;
  MatchingEngineOrdersAtPrice* prev = nullptr;

  // 哈希碰撞链表（同一桶内的不同价格）
  MatchingEngineOrdersAtPrice* hash_next = nullptr;
};

using OrdersAtPriceHashMap =
    std::array<MatchingEngineOrdersAtPrice*, common::kMaxPriceLevels>;

}  // namespace exchange
