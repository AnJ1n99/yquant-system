#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>

#include "../../common/ringBuffer.h"
#include "../../common/types.h"

namespace exchange {
enum class ClientResponseType : uint8_t {
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
// CANCEL_REJECTED 只携带类型及客户端、标的、客户端订单号。
// 其他字段在该消息中不适用，聚合初始化时清零；接受和撤单回报的本次成交量为 0。
struct MatchingEngineClientResponse {
  ClientResponseType type_;
  common::ClientId client_id_;
  common::SymbolId symbol_id_;
  common::OrderId client_order_id_;
  common::OrderId market_order_id_;
  common::Side side_;
  common::Price price_;
  common::Quantity executed_quantity_;
  common::Quantity remaining_quantity_;

  auto toString() const {
    std::ostringstream oss;
    oss << "MatchingEngineClientResponse"
        << " ["
        << "type:" << clientResponseTypeToString(type_)
        << " client:" << client_id_
        << " symbol:" << symbol_id_
        << " coid:" << client_order_id_;
    if (type_ != ClientResponseType::CANCEL_REJECTED) {
      oss << " moid:" << market_order_id_
          << " side:" << static_cast<unsigned>(side_)
          << " executed_quantity:" << executed_quantity_
          << " remaining_quantity:" << remaining_quantity_
          << " price:" << price_;
    }
    oss << "]";
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
        << "seq:" << seqNum 
        << " " << matching_engine_client_response.toString()
        << "]";
    return oss.str();
  }
};

#pragma pack(pop)
using ClientResponseLFQueue = common::LFQueue<MatchingEngineClientResponse>;
}  // namespace exchange
