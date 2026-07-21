/*
        MatchingEngine是交易所服务器的核心订单处理组件。
        它通过维护每个交易品种的订单簿、处理新建和取消请求以及生成客户端响应和市场数据更新来协调客户端订单的匹配
*/
#pragma once

#include <streambuf>

#include "../../common/logging.h"
#include "../market_data/market_update.h"
#include "../order_manager/client_request.h"
#include "../order_manager/client_response.h"
#include "me_order_book.h"

namespace Exchange {
class MatchingEngine final {
 public:
  MatchingEngine(ClientRequestLFQueue* clientRequests,
                 ClientResponseLFQueue* outgoingResponses,
                 MEMarketUpdateLFQueue* outgoingUpdates);

  ~MatchingEngine();

  void start();
  void stop();

  // 处理从无锁队列读取的客户端请求（由OrderManager发送）
  void processClientRequest(const MEClientRequest* clientRequest) noexcept;
  // 将客户端响应写入无锁队列，供OrderManager消费
  void sendClientResponse(const MEClientResponse* response) noexcept;
  // 将市场更新写入无锁队列，供MarketDataPublisher消费
  void sendMarketUpdate(const MEMarketUpdate* update) noexcept;

  // 禁用拷贝构造函数、移动构造函数、拷贝赋值操作符和移动赋值操作符
  MatchingEngine() = delete;
  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine(MatchingEngine&&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;
  MatchingEngine& operator=(MatchingEngine&&) = delete;

 private:
  // 主运行循环
  void run();
  // symbol 到 MEOrderBook 的哈希映射
  OrderBookHashMap symbol_order_book;

  // 无锁队列：
  // 一个用于消费 OrderManager 发送的传入客户端请求
  // 第二个用于发布 outgoing ClientResponses，供OrderManager消费
  // 第三个用于发布 outgoing 市场更新，供市场数据发布器消费

  ClientRequestLFQueue* incoming_requests = nullptr;
  ClientResponseLFQueue* outgoing_ogw_responses = nullptr;
  MEMarketUpdateLFQueue* outgoing_md_updates = nullptr;

  volatile bool running_ = false;

  std::string time_str_;
  Common::Logger logger;
};

}  // namespace Exchange
