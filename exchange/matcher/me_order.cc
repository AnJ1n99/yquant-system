#include "me_order.h"

#include <string>

namespace Exchange {

auto MEOrder::toString() const -> std::string {
  std::ostringstream oss;
  oss << "MEOrder"
      << " ["
      << "clientId:" << Common::clientIdToString(client_id)
      << " clientOId:" << Common::orderIdToString(client_order_id)
      << " marketOId:" << Common::orderIdToString(market_order_id)
      << " symbolId:" << Common::symbolIdToString(symbol_id)
      << " side:" << Common::sideToString(side)
      << " price:" << Common::priceToString(price)
      << " qty:" << Common::qtyToString(qty_remain)
      << " priority:" << Common::priorityToString(priority) << "]";
  return oss.str();
}

}  // namespace Exchange
