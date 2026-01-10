/*
	MatchingEngine是交易所服务器的核心订单处理组件。
	它通过维护每个交易品种的订单簿、处理新建和取消请求以及生成客户端响应和市场数据更新来协调客户端订单的匹配
*/
#pragma once

#include "../order_manager/client_request.h"
#include "../order_manager/client_response.h"
#include "../../common/logging.h"
#include "../market_data/market_update.h"

namespace Exchange {

    class MatchingEngine final {
    public:
        MatchingEngine(ClientRequestLFQueue *clientRequests,ClientResponseLFQueue *outgoingResponses,
                      MEMarketUpdateLFqueue *outgoingUpdates);

        ~MatchingEngine();

        void start();
        void end();

        // 禁用拷贝构造函数、移动构造函数、拷贝赋值操作符和移动赋值操作符
        MatchingEngine() = delete;
        MatchingEngine(const MatchingEngine&) = delete;
        MatchingEngine(MatchingEngine&&) = delete;
        MatchingEngine& operator=(const MatchingEngine&) = delete;
        MatchingEngine& operator=(MatchingEngine&&) = delete;
    private:
        // 股票代码 到 MEOrderBook 的哈希映射
        OrderBookHashMap ticker_order_book;

        // 无锁队列：
        // 一个用于消费 OrderManager 发送的传入客户端请求
        // 第二个用于发布 outgoing ClientResponses，供OrderManager消费
        // 第三个用于发布 outgoing 市场更新，供市场数据发布器消费

        ClientRequestLFQueue *client_requests = nullptr;
        ClientResponseLFQueue *outgoing_ogw_responses = nullptr;
        MEMarketUpdateLFqueue *outgoing_md_updates = nullptr;

        volatile bool run = false;

        std::string time_str_;
        Logger logger;
    };

}
