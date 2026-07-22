#include "matching_engine_order.h"

#include <sstream>
#include <string>

namespace exchange {

MatchingEngineOrder::MatchingEngineOrder(common::ClientId client_id,
                                         common::OrderId client_order_id,
                                         common::OrderId market_order_id,
                                         common::SymbolId symbol_id,
                                         common::Side side, common::Price price,
                                         common::Quantity remaining_quantity,
                                         common::Priority priority)
    : client_id(client_id),
      client_order_id(client_order_id),
      market_order_id(market_order_id),
      symbol_id(symbol_id),
      side(side),
      price(price),
      remaining_quantity(remaining_quantity),
      priority(priority) {}

// 供日志使用
auto MatchingEngineOrder::toString() const -> std::string {
  std::ostringstream oss;
  oss << "MatchingEngineOrder"
      << " ["
      << "clientId:" << common::ClientIdToString(client_id)
      << " clientOId:" << common::OrderIdToString(client_order_id)
      << " marketOId:" << common::OrderIdToString(market_order_id)
      << " symbolId:" << common::SymbolIdToString(symbol_id)
      << " side:" << common::SideToString(side)
      << " price:" << common::PriceToString(price)
      << " remaining_quantity:" << common::QuantityToString(remaining_quantity)
      << " priority:" << common::PriorityToString(priority) << "]";
  return oss.str();
}

auto MatchingEngineOrdersAtPrice::toString() const -> std::string {
  std::ostringstream oss;
  oss << "MatchingEngineOrdersAtPrice"
      << " ["
      << "side:" << common::SideToString(side) << " "
      << "price:" << common::PriceToString(price) << " "
      << " firstOrder:" << (first_order ? "exists" : "nullptr") << "]";
  return oss.str();
}

}  // namespace exchange
