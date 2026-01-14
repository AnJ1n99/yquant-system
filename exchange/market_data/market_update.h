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
    
#include "../../common/types.h"
#include "../../common/ringBuffer.h"
#include <string>

using namespace Common;

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

    SymbolId symbolId_ = SymbolId_INVALID;
    OrderId orderId_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty qty_ = Qty_INVALID;
    Priority priority_ = Priority_INVALID;    // 优先级

    auto toString() const {
        std::ostringstream oss;
        oss << "MEMarketUpdate"
            << " ["
            << "type:" << marketUpdateTypeToString(type_)
            << " symbol:" << symbolIdToString(symbolId_)
            << " orderId:" << orderIdToString(orderId_)
            << " side:" << sideToString(side_)
            << " price:" << priceToString(price_)
            << " qty:" << qtyToString(qty_)
            << " priority: " << priorityToString(priority_)
            << "]";
        return oss.str();
    }
};

// MDP通过网络发布的市场更新结构
struct MDPMarketUpdate {
    ssize_t seq_num = 0;              // 序列号
    MEMarketUpdate update;             // 市场更新结构对象

    auto toString() const {
        std::ostringstream oss;
        oss << "MDPMarketUpdate"
            << " ["
            << "seq:" << seq_num
            << " update:" << update.toString()
            << "]";
        return oss.str();
    }
};
#pragma pack(pop)
// MDP和ME通信的中间件 
typedef Common::LFQueue<Exchange::MEMarketUpdate> MEMarketUpdateLFqueue;
typedef Common::LFQueue<Exchange::MDPMarketUpdate> MDPMarketUpdateLFqueue;
} // namespace Exchange
