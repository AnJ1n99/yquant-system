#include "market_order.h"

namespace trading {
  using namespace common;
  auto MarketOrder::toString() const -> std::string {
    std::stringstream ss;
    ss << "MarketOrder" << "["
       << "oid:" << std::to_string(order_id_) << " "
       << "side:" << std::to_string(static_cast<unsigned>(side_)) << " "
       << "price:" << std::to_string(price_) << " "
       << "qty:" << std::to_string(qty_) << " "
       << "prio:" << std::to_string(priority_) << " "
       << "prev:" << std::to_string(prev_order_ ? prev_order_->order_id_ : 0) << " "
       << "next:" << std::to_string(next_order_ ? next_order_->order_id_ : 0) << "]";

    return ss.str();
  }
}
