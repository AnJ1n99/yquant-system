#pragma once

#include <array>
#include <sstream>
#include "common/types.h"


namespace trading {
  using namespace common;
  /// Represents the type / action in the order structure in the order manager.
  enum class OMOrderState : int8_t {
    INVALID = 0,
    PENDING_NEW = 1,
    LIVE = 2,
    PENDING_CANCEL = 3,
    DEAD = 4
  };

  inline auto OMOrderStateToString(OMOrderState side) -> std::string {
    switch (side) {
      case OMOrderState::PENDING_NEW:
        return "PENDING_NEW";
      case OMOrderState::LIVE:
        return "LIVE";
      case OMOrderState::PENDING_CANCEL:
        return "PENDING_CANCEL";
      case OMOrderState::DEAD:
        return "DEAD";
      case OMOrderState::INVALID:
        return "INVALID";
    }

    return "UNKNOWN";
  }

  /// Internal structure used by the order manager to represent a single strategy order.
  struct OMOrder {
    SymbolId ticker_id_ = 0;
    OrderId order_id_ = 0;
    Side side_ = Side::BUY;
    Price price_ = 0;
    Quantity qty_ = 0;
    OMOrderState order_state_ = OMOrderState::INVALID;

    auto toString() const {
      std::stringstream ss;
      ss << "OMOrder" << "["
         << "tid:" << std::to_string(ticker_id_) << " "
         << "oid:" << std::to_string(order_id_) << " "
         << "side:" << std::to_string(static_cast<unsigned>(side_)) << " "
         << "price:" << std::to_string(price_) << " "
         << "qty:" << std::to_string(qty_) << " "
         << "state:" << OMOrderStateToString(order_state_) << "]";

      return ss.str();
    }
  };

  /// Hash map from Side -> OMOrder.
  typedef std::array<OMOrder, 2> OMOrderSideHashMap;

  /// Hash map from SymbolId -> Side -> OMOrder.
  typedef std::array<OMOrderSideHashMap, kMaxSymbols> OMOrderTickerSideHashMap;
}
