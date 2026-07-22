#pragma once

#include <array>
#include <string>

#include "../../common/types.h"

namespace exchange {

// Matching-engine internal order (passive data; books mutate fields directly).
struct MatchingEngineOrder {
  MatchingEngineOrder() = default;

  MatchingEngineOrder(common::ClientId client_id,
                      common::OrderId client_order_id,
                      common::OrderId market_order_id,
                      common::SymbolId symbol_id, common::Side side,
                      common::Price price, common::Quantity remaining_quantity,
                      common::Priority priority);

  auto toString() const -> std::string;

  // Identity / routing
  common::ClientId client_id = common::ClientId_INVALID;  // owner client
  common::OrderId client_order_id =
      common::OrderId_INVALID;  // client-local id (cancel/lookup)
  common::OrderId market_order_id =
      common::OrderId_INVALID;  // exchange-global id (MD / responses)
  common::SymbolId symbol_id = common::SymbolId_INVALID;  // instrument

  // Economics
  common::Side side = common::Side::INVALID;
  common::Price price = common::Price_INVALID;
  common::Quantity remaining_quantity =
      common::Quantity_INVALID;  // unfilled size
  common::Priority priority =
      common::Priority_INVALID;  // price-time rank in level

  // Same-price FIFO links (owned by MatchingEngineSideBook)
  MatchingEngineOrder* next = nullptr;
  MatchingEngineOrder* prev = nullptr;
};

// Per-client order table: OrderId -> MatchingEngineOrder.
using OrderHashMap = std::array<MatchingEngineOrder*, common::kMaxOrderIds>;
// ClientId -> OrderId -> MatchingEngineOrder
using ClientOrderHashMap = std::array<OrderHashMap*, common::kMaxNumClients>;

// Same-price order list (FIFO circular). Aggregate; use designated init.
struct MatchingEngineOrdersAtPrice {
  auto toString() const -> std::string;

  common::Side side = common::Side::INVALID;
  common::Price price = common::Price_INVALID;
  MatchingEngineOrder* first_order = nullptr;  // FIFO head

  MatchingEngineOrdersAtPrice* next = nullptr;  // better/worse price
  MatchingEngineOrdersAtPrice* prev = nullptr;
  MatchingEngineOrdersAtPrice* hash_next = nullptr;  // hash collision
};

using OrdersAtPriceHashMap =
    std::array<MatchingEngineOrdersAtPrice*, common::kMaxPriceLevels>;

}  // namespace exchange
