#include "matching_engine_order.h"

#include <string>

namespace exchange {

auto MatchingEngineOrder::toString() const -> std::string {
  std::ostringstream oss;
  oss << "MatchingEngineOrder"
      << " ["
      << "clientId:" << common::clientIdToString(client_id)
      << " clientOId:" << common::orderIdToString(client_order_id)
      << " marketOId:" << common::orderIdToString(market_order_id)
      << " symbolId:" << common::symbolIdToString(symbol_id)
      << " side:" << common::sideToString(side)
      << " price:" << common::priceToString(price)
      << " remaining_quantity:" << common::quantityToString(remaining_quantity)
      << " priority:" << common::priorityToString(priority) << "]";
  return oss.str();
}

}  // namespace exchange
