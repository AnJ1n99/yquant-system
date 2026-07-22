/*
 * 该组件通过UDP向Client实时发布市场更新，通过无所队列接受市场事件，并通过增量多播流广播
 * 使多个客户端能够同时接受低延迟的市场数据
 *
 * 市场数据发布者(MarketDataPublisher)从撮合引擎接收执行(成交),并根据执行流构建订单簿和K线图
 * MatchingEngine-->MarketDataPublisher-->DataService
 *       |
 *   MarketUpdate
 */

#pragma once

#include <string>

#include "../../common/ringBuffer.h"
#include "../../common/types.h"

namespace exchange {

enum class MarketUpdateType : uint8_t {
  INVALID = 0,
  CLEAR,
  ADD,
  MODIFY,
  CANCEL,
  TRADE,
  SNAPSHOT_START,
  SNAPSHOT_END,
};

inline std::string marketUpdateTypeToString(MarketUpdateType type) {
  switch (type) {
    case MarketUpdateType::INVALID:
      return "INVALID";
    case MarketUpdateType::CLEAR:
      return "CLEAR";
    case MarketUpdateType::ADD:
      return "ADD";
    case MarketUpdateType::MODIFY:
      return "MODIFY";
    case MarketUpdateType::CANCEL:
      return "CANCEL";
    case MarketUpdateType::TRADE:
      return "TRADE";
    case MarketUpdateType::SNAPSHOT_START:
      return "SNAPSHOT_START";
    case MarketUpdateType::SNAPSHOT_END:
      return "SNAPSHOT_END";
  }
  return "UNKNOWN";
}

// 这些结构用于网络发送，所以对二进制结构进行紧凑打包
#pragma pack(push, 1)
struct MatchingEngineMarketUpdate {
  MarketUpdateType type_ = MarketUpdateType::INVALID;

  common::SymbolId symbolId_ = common::SymbolId_INVALID;
  common::OrderId orderId_ = common::OrderId_INVALID;
  common::Side side_ = common::Side::INVALID;
  common::Price price_ = common::Price_INVALID;
  common::Quantity quantity_ = common::Quantity_INVALID;
  common::Priority priority_ = common::Priority_INVALID;  // 优先级

  auto toString() const {
    std::ostringstream oss;
    oss << "MatchingEngineMarketUpdate"
        << " ["
        << "type:" << marketUpdateTypeToString(type_)
        << " symbol:" << common::symbolIdToString(symbolId_)
        << " orderId:" << common::orderIdToString(orderId_)
        << " side:" << common::sideToString(side_)
        << " price:" << common::priceToString(price_)
        << " quantity:" << common::quantityToString(quantity_)
        << " priority: " << common::priorityToString(priority_) << "]";
    return oss.str();
  }
};

// MarketDataPublisher 通过网络发布的市场更新结构
struct MarketDataPublisherMarketUpdate {
  ssize_t seq_num = 0;                // 序列号
  MatchingEngineMarketUpdate update;  // 市场更新结构对象

  auto toString() const {
    std::ostringstream oss;
    oss << "MarketDataPublisherMarketUpdate"
        << " ["
        << "seq:" << seq_num << " update:" << update.toString() << "]";
    return oss.str();
  }
};
#pragma pack(pop)
// MatchingEngine 与 MarketDataPublisher 通信的中间件
using MatchingEngineMarketUpdateLFQueue =
    common::LFQueue<MatchingEngineMarketUpdate>;
using MarketDataPublisherMarketUpdateLFQueue =
    common::LFQueue<MarketDataPublisherMarketUpdate>;
}  // namespace exchange
