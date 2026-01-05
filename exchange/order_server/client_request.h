#pragma once

#include "../../common/types.h"
#include "../../common/ringBuffer.h"

#include <cstddef>
#include <cstdint>
#include <sstream>

using namespace Common;

namespace Exchange {
    enum class ClientRequestType : uint8_t {
        INVAILD  = 0,
        NEW      = 1,
        CANCELED = 2,
    };

    inline std::string clientRequestTypeToString (ClientRequestType type) {
        switch (type) {
            case ClientRequestType::INVAILD:
                return "INVAILD";
            case ClientRequestType::NEW:
                return "NEW";
            case ClientRequestType::CANCELED:
                return "CANCELED";
        }
        return "UNKNOWN";
    }

    // #pragma pack(push, 1) 是编译指令，用于设置内存对齐方式：
    // push - 保存当前的内存对齐设置到栈中
    // 1 - 将内存对齐改为 1 字节（即不对齐，紧密排列）
    // 这通常在定义网络协议结构体或二进制数据格式时使用，确保结构体成员紧密排列，没有填充字节。
    // 一般会在相应的结构体定义后面使用 #pragma pack(pop) 来恢复之前的对齐设置。
#pragma pack(push, 1)

    // 匹配引擎（Matching Engine）的客户端请求结构。在高频交易系统中，这种紧密排列的内存布局可以：
    // - 减少内存占用
    // - 提高数据序列化/反序列化的性能
    // - 确保跨进程/网络传输的数据格式一致性
    struct MEClientRequest {
        ClientRequestType type_ = ClientRequestType::INVAILD;

       // 初始化示例 - 表示一个未初始化的订单结构
        ClientId clientId_   = ClientId_INVALID;    // 客户端
        TickerId tickerId_   = TickerId_INVALID;    // 行情
        OrderId  orderId_    = OrderId_INVALID;     // 订单
        Side     side_       = Side::INVALID;       // 买卖
        Price    price_      = Price_INVALID;       // 价格
        Qty      qty_        = Qty_INVALID;         // 数量

        auto toString() const {
            std::ostringstream oss;
            oss  << "MEClientRequest"
                 << " ["
                 << "type:"    << clientRequestTypeToString(type_)
                 << " client:" << clientIdToString(clientId_)
                 << " ticker:" << tickerIdToString(tickerId_)
                 << " oid:"    << orderIdToString(orderId_)
                 << " side:"   << sideToString(side_)
                 << " qty:"    << qtyToString(qty_)
                 << " price:"  << priceToString(price_)
                 << "]";
            return oss.str();
        }
    };

    struct OMClientRequest {
        size_t seqNum = 0;
        MEClientRequest meClientRequest;

        auto toString() const {
            std::ostringstream oss;
            oss  << "OMClientRequest"
                << " ["
                << "seqNum:" << seqNum
                << " " << meClientRequest.toString()
                << "]";
            return oss.str();
        }
    };

#pragma pack(pop)

    typedef LFQueue<MEClientRequest> ClientRequestLFQueue;
}
