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
#include "book_core.h"

namespace exchange {
class MatchingEngine final {
 public:
  MatchingEngine(ClientRequestLFQueue* clientRequests,
                 ClientResponseLFQueue* outgoingResponses,
                 MatchingEngineMarketUpdateLFQueue* outgoingUpdates);

  ~MatchingEngine();

  void start();
  void stop();

  // 处理从无锁队列读取的客户端请求（由OrderManager发送）
  void ProcessClientRequest(
      const MatchingEngineClientRequest* clientRequest) noexcept;

  // 禁用拷贝构造函数、移动构造函数、拷贝赋值操作符和移动赋值操作符
  MatchingEngine() = delete;
  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine(MatchingEngine&&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;
  MatchingEngine& operator=(MatchingEngine&&) = delete;

 private:
  // 主运行循环
  void run();
  // symbol 到 BookCore 的直接索引表
  OrderBookHashMap symbol_order_book;

  // 消费 OrderManager 发送的客户端请求；输出队列直接交给订单簿。
  ClientRequestLFQueue* incoming_requests = nullptr;

  volatile bool running_ = false;

  std::string time_str_;
  common::Logger logger;
};

}  // namespace exchange
