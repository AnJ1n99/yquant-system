#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>

#include "macros.h"

namespace common {
// Constants used across the ecosystem to represent upper bounds on various
// containers. Trading instruments / SymbolIds from [0, kMaxSymbols).

constexpr size_t kMaxSymbols = 8;

// Maximum size of lock free queues used to transfer client requests, client
// responses and market updates between components.
constexpr size_t kMaxClientUpdates = 256 * 1024;
constexpr size_t kMaxMarketUpdates = 256 * 1024;

// Maximum number of clients supported by the system.
constexpr size_t kMaxNumClients = 256;

// max number of orders per trading instrument
constexpr size_t kMaxOrderIds = 1024 * 1024;

// Maximum price level depth in the order books.
constexpr size_t kMaxPriceLevels = 256;

using OrderId = uint64_t;
constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max();

inline auto OrderIdToString(OrderId order_id) -> std::string {
  if (UNLIKELY(order_id == OrderId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(order_id);
}

using SymbolId = uint32_t;
constexpr auto SymbolId_INVALID = std::numeric_limits<SymbolId>::max();

inline auto SymbolIdToString(SymbolId symbol_id) -> std::string {
  if (UNLIKELY(symbol_id == SymbolId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(symbol_id);
}

using ClientId = uint32_t;
constexpr auto ClientId_INVALID = std::numeric_limits<ClientId>::max();

inline auto ClientIdToString(ClientId client_id) -> std::string {
  if (UNLIKELY(client_id == ClientId_INVALID)) {
    return "INVALID";
  }

  return std::to_string(client_id);
}

using Price = int64_t;
constexpr auto Price_INVALID = std::numeric_limits<Price>::max();

inline auto PriceToString(Price price) -> std::string {
  if (UNLIKELY(price == Price_INVALID)) {
    return "INVALID";
  }

  return std::to_string(price);
}

using Quantity = uint32_t;
constexpr auto Quantity_INVALID = std::numeric_limits<Quantity>::max();

inline auto QuantityToString(Quantity quantity) -> std::string {
  if (UNLIKELY(quantity == Quantity_INVALID)) {
    return "INVALID";
  }

  return std::to_string(quantity);
}

// 买卖方向
enum class Side : uint8_t { INVALID = 0, BUY = 1, SELL = 2 };

inline auto SideToString(Side side) -> std::string {
  switch (side) {
    case Side::BUY:
      return "BUY";
    case Side::SELL:
      return "SELL";
    case Side::INVALID:
      return "INVALID";
  }
  return "UNKNOWN";
}

// 优先级 (通常使用纳秒时间戳实现价格-时间优先级)
using Priority = uint64_t;
constexpr auto Priority_INVALID = std::numeric_limits<Priority>::max();

inline auto PriorityToString(Priority priority) -> std::string {
  if (UNLIKELY(priority == Priority_INVALID)) {
    return "INVALID";
  }

  return std::to_string(priority);
}
}  // namespace common
