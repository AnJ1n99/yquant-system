#pragma once

#include <cstddef>
#include <sstream>

#include "../../common/ringBuffer.h"
#include "../../common/types.h"

namespace Exchange {
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
struct MEClientResponse {
  ClientResponseType type_ = ClientResponseType::INVALID;
  Common::ClientId client_id_ = Common::ClientId_INVALID;
  Common::SymbolId symbol_id_ = Common::SymbolId_INVALID;
  Common::OrderId client_order_id_ = Common::OrderId_INVALID;
  Common::OrderId market_order_id_ = Common::OrderId_INVALID;
  Common::Side side_ = Common::Side::INVALID;
  Common::Price price_ = Common::Price_INVALID;
  Common::Qty exec_qty_ = Common::Qty_INVALID;
  Common::Qty leaves_qty_ = Common::Qty_INVALID;

  auto toString() const {
    std::ostringstream oss;
    oss << "MEClientResponse"
        << " ["
        << "type:" << clientResponseTypeToString(type_)
        << " client:" << Common::clientIdToString(client_id_)
        << " symbol:" << Common::symbolIdToString(symbol_id_)
        << " coid:" << Common::orderIdToString(client_order_id_)
        << " moid:" << Common::orderIdToString(market_order_id_)
        << " side:" << Common::sideToString(side_)
        << " exec_qty:" << Common::qtyToString(exec_qty_)
        << " leaves_qty:" << Common::qtyToString(leaves_qty_)
        << " price:" << Common::priceToString(price_) << "]";
    return oss.str();
  }
};

struct OMClientResponse {
  size_t seqNum = 0;
  MEClientResponse meClientResponse;

  auto toString() {
    std::ostringstream oss;
    oss << "OMClientResponse"
        << " ["
        << "seq:" << seqNum << " " << meClientResponse.toString() << "]";
    return oss.str();
  }
};

#pragma pack(pop)
using ClientResponseLFQueue = Common::LFQueue<MEClientResponse>;
}  // namespace Exchange
