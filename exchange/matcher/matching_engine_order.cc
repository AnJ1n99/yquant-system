#include "matching_engine_order.h"

#include <sstream>
#include <string>

namespace exchange {

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
