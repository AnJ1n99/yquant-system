#pragma once

#include <cstddef>
#include <sstream>

#include "../../common/ringBuffer.h"
#include "../../common/types.h"

namespace exchange {
enum class ClientResponseType {
  INVALID = 0,
  ACCEPTED = 1,
  CANCELED = 2,
  FILLED = 3,
  CANCEL_REJECTED = 4
};

inline std::string clientResponseTypeToString(ClientResponseType type) {
  switch (type) {
    case ClientResponseType::ACCEPTED:
      return "ACCEPTED";
    case ClientResponseType::CANCELED:
      return "CANCELED";
    case ClientResponseType::FILLED:
      return "FILLED";
    case ClientResponseType::CANCEL_REJECTED:
      return "CANCEL_REJECTED";
    case ClientResponseType::INVALID:
      return "INVALID";
  }
  return "UNKNOWN";
}

#pragma pack(push, 1)
struct MatchingEngineClientResponse {
  ClientResponseType type_ = ClientResponseType::INVALID;
  common::ClientId client_id_ = common::ClientId_INVALID;
  common::SymbolId symbol_id_ = common::SymbolId_INVALID;
  common::OrderId client_order_id_ = common::OrderId_INVALID;
  common::OrderId market_order_id_ = common::OrderId_INVALID;
  common::Side side_ = common::Side::INVALID;
  common::Price price_ = common::Price_INVALID;
  common::Quantity executed_quantity_ = common::Quantity_INVALID;
  common::Quantity remaining_quantity_ = common::Quantity_INVALID;

  auto toString() const {
    std::ostringstream oss;
    oss << "MatchingEngineClientResponse"
        << " ["
        << "type:" << clientResponseTypeToString(type_)
        << " client:" << common::clientIdToString(client_id_)
        << " symbol:" << common::symbolIdToString(symbol_id_)
        << " coid:" << common::orderIdToString(client_order_id_)
        << " moid:" << common::orderIdToString(market_order_id_)
        << " side:" << common::sideToString(side_)
        << " executed_quantity:" << common::quantityToString(executed_quantity_)
        << " remaining_quantity:"
        << common::quantityToString(remaining_quantity_)
        << " price:" << common::priceToString(price_) << "]";
    return oss.str();
  }
};

struct OrderManagerClientResponse {
  size_t seqNum = 0;
  MatchingEngineClientResponse matching_engine_client_response;

  auto toString() {
    std::ostringstream oss;
    oss << "OrderManagerClientResponse"
        << " ["
        << "seq:" << seqNum << " " << matching_engine_client_response.toString()
        << "]";
    return oss.str();
  }
};

#pragma pack(pop)
using ClientResponseLFQueue = common::LFQueue<MatchingEngineClientResponse>;
}  // namespace exchange
