/*
 * 该组件通过UDP向Client实时发布市场更新，通过无所队列接受市场事件，并通过增量多播流广播
 * 使多个客户端能够同时接受低延迟的市场数据
 *
 * 市场数据发布者(MDP)从撮合引擎接收执行(成交),并根据执行流构建订单簿和K线图
 * ME-->MDP-->DataService
 *       |
 *   MarketUpdate
 */

#pragma once

#include <string>

#include "../../common/ringBuffer.h"
#include "../../common/types.h"

namespace Exchange {

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
struct MEMarketUpdate {
  MarketUpdateType type_ = MarketUpdateType::INVALID;

  Common::SymbolId symbolId_ = Common::SymbolId_INVALID;
  Common::OrderId orderId_ = Common::OrderId_INVALID;
  Common::Side side_ = Common::Side::INVALID;
  Common::Price price_ = Common::Price_INVALID;
  Common::Qty qty_ = Common::Qty_INVALID;
  Common::Priority priority_ = Common::Priority_INVALID;  // 优先级

  auto toString() const {
    std::ostringstream oss;
    oss << "MEMarketUpdate"
        << " ["
        << "type:" << marketUpdateTypeToString(type_)
        << " symbol:" << Common::symbolIdToString(symbolId_)
        << " orderId:" << Common::orderIdToString(orderId_)
        << " side:" << Common::sideToString(side_)
        << " price:" << Common::priceToString(price_)
        << " qty:" << Common::qtyToString(qty_)
        << " priority: " << Common::priorityToString(priority_) << "]";
    return oss.str();
  }
};

// MDP通过网络发布的市场更新结构
struct MDPMarketUpdate {
  ssize_t seq_num = 0;    // 序列号
  MEMarketUpdate update;  // 市场更新结构对象

  auto toString() const {
    std::ostringstream oss;
    oss << "MDPMarketUpdate"
        << " ["
        << "seq:" << seq_num << " update:" << update.toString() << "]";
    return oss.str();
  }
};
#pragma pack(pop)
// MDP和ME通信的中间件
using MEMarketUpdateLFQueue = Common::LFQueue<MEMarketUpdate>;
using MDPMarketUpdateLFQueue = Common::LFQueue<MDPMarketUpdate>;
}  // namespace Exchange
