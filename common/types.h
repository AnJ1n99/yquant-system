#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>

#include "macros.h"

namespace Common {
// Constants used across the ecosystem to represent upper bounds on various
// containers. Trading instruments / TickerIds from [0, ME_MAX_TICKERS].

constexpr size_t ME_MAX_SYMBOLS = 8;

// Maximum size of lock free queues used to transfer client requests, client
// responses and market updates between components.
constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;

// Maximum number of clients supported by the system.
constexpr size_t ME_MAX_NUM_CLIENTS = 256;

// max number of orders per trading clients
constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;

// Maximum price level depth in the order books.
constexpr size_t ME_MAX_PRICE_LEVELS = 256;

typedef uint64_t OrderId;
constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max();

inline auto orderIdToString(OrderId order_id) -> std::string {
  if (UNLIKELY(order_id == OrderId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(order_id);
}

// 股票代码
typedef uint32_t SymbolId;
constexpr auto SymbolId_INVALID = std::numeric_limits<SymbolId>::max();

inline auto symbolIdToString(SymbolId symbol_id) -> std::string {
  if (UNLIKELY(symbol_id == SymbolId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(symbol_id);
}

typedef uint32_t ClientId;
constexpr auto ClientId_INVALID = std::numeric_limits<ClientId>::max();

inline auto clientIdToString(ClientId client_id) -> std::string {
  if (UNLIKELY(client_id == ClientId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(client_id);
}

typedef int64_t Price;
constexpr auto Price_INVALID = std::numeric_limits<Price>::max();

inline auto priceToString(Price price) -> std::string {
  if (UNLIKELY(price == Price_INVALID)) {
    return "INVALID";
  }

  return std::to_string(price);
}

typedef uint32_t Qty;
constexpr auto Qty_INVALID = std::numeric_limits<Qty>::max();

inline auto qtyToString(Qty qty) -> std::string {
  if (UNLIKELY(qty == Qty_INVALID)) {
    return "INVALID";
  }

  return std::to_string(qty);
}

// 买卖方向
enum class Side : int8_t { INVALID = 0, BUY = 1, SELL = -1, MAX = 2 };

inline auto sideToString(Side side) -> std::string {
  switch (side) {
    case Side::BUY:
      return "BUY";
    case Side::SELL:
      return "SELL";
    case Side::INVALID:
      return "INVALID";
    case Side::MAX:
      return "MAX";
  }
  return "UNKNOWN";
}

// 优先级 (通常使用纳秒时间戳实现价格-时间优先级)
typedef uint64_t Priority;
constexpr auto Priority_INVALID = std::numeric_limits<Priority>::max();

inline auto priorityToString(Priority priority) -> std::string {
  if (UNLIKELY(priority == Priority_INVALID)) {
    return "INVALID";
  }

  return std::to_string(priority);
}
}  // namespace Common
