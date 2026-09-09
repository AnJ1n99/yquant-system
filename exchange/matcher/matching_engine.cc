#include "matching_engine.h"

#include <chrono>
#include <cstddef>

#include "../../common/logging.h"
#include "../../common/thread_utils.h"
#include "../../common/time_utils.h"

// TODO: SHARDING

namespace exchange {

MatchingEngine::MatchingEngine(
    ClientRequestLFQueue* clientRequests,
    ClientResponseLFQueue* outgoingResponses,
    MatchingEngineMarketUpdateLFQueue* outgoingUpdates)
    : incoming_requests(clientRequests),
      outgoing_client_responses(outgoingResponses),
      outgoing_market_updates(outgoingUpdates),
      logger("MatchingEngine.log") {
  // 使用传入的队列初始化撮合引擎。在标的拥有各自的参考价格之前，
  // 所有订单簿共用同一个价格网格。
  for (size_t i = 0; i < symbol_order_book.size(); ++i) {
    symbol_order_book[i] = new BookCore(i, kDefaultPriceBand, this, &logger);
  }
}

MatchingEngine::~MatchingEngine() {
  // Clean up resources if needed
  // The destructor should ensure proper cleanup of the matching engine
  stop();

  std::this_thread::sleep_for(std::chrono::seconds(1));

  incoming_requests = nullptr;
  outgoing_client_responses = nullptr;
  outgoing_market_updates = nullptr;

  for (auto& order_book : symbol_order_book) {
    delete order_book;
    order_book = nullptr;
  }
}

void MatchingEngine::start() {
  running_ = true;
  // Start the matching engine processing loop
  ASSERT(common::createAndStartThread(2, "Exchange/MatchingEngine",
                                      [this]() { run(); }) != nullptr,
         "Failed to start MatchingEngine thread.");
}

void MatchingEngine::stop() {
  running_ = false;
  // Stop the matching engine processing and perform cleanup if needed
}

void MatchingEngine::processClientRequest(
    const MatchingEngineClientRequest* client_request) noexcept {
  // 获取对应symbol的订单簿
  auto* order_book = symbol_order_book[client_request->symbolId_];
  // 根据请求类型处理
  switch (client_request->type_) {
    case ClientRequestType::NEW: {
      // 添加订单刀订单薄
      START_MEASURE(Exchange_BookCore_Add);
      order_book->Add(client_request->clientId_, client_request->orderId_,
                      client_request->side_, client_request->price_,
                      client_request->quantity_);
      END_MEASURE(Exchange_BookCore_Add, logger);
    } break;
    case ClientRequestType::CANCELED: {
      START_MEASURE(Exchange_MatchingEngine_Cancel);
      order_book->Cancel(client_request->clientId_, client_request->orderId_);
      END_MEASURE(Exchange_MatchingEngine_Cancel, logger);
    } break;
    default: {
      FATAL("收到无效的客户端请求类型：" +
            clientRequestTypeToString(client_request->type_));
    } break;
  }
}

void MatchingEngine::run() {
  common::GetCurrentTimeStr(time_str_);
  logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__,
             "MatchingEngine thread started at " + time_str_);

  while (running_) {
    // 从客户端请求队列中读取请求
    const auto client_request = incoming_requests->GetNextToRead();

    if (LIKELY(client_request)) {
      TTT_MEASURE(T3_MatchingEngine_LFQueue_read, logger);  // 测量队列读取时间

      // 记录日志
      common::GetCurrentTimeStr(time_str_);
      logger.log("%:% %() % Processing request: %\n", __FILE__, __LINE__,
                 __FUNCTION__, time_str_, client_request->toString());

      // 处理客户端请求
      START_MEASURE(Exchange_MatchingEngine_processClientRequest);
      processClientRequest(client_request);
      END_MEASURE(Exchange_MatchingEngine_processClientRequest,
                  logger);  // 测量处理时间
      // 标记已读取完成
      incoming_requests->UpdateReadIndex();
    }
  }

  logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__,
             "MatchingEngine thread stopped");
}

void MatchingEngine::sendMarketUpdate(
    const MatchingEngineMarketUpdate* update) noexcept {
  common::GetCurrentTimeStr(time_str_);
  logger.log("%:% %() % Sending market update: %\n", __FILE__, __LINE__,
             __FUNCTION__, time_str_, update->toString());

  auto next_write = outgoing_market_updates->GetNextToWriteTo();
  *next_write = *update;
  outgoing_market_updates->UpdateWriteIndex();
  TTT_MEASURE(T4t_MatchingEngine_LFQueue_write, logger);  // 测量队列写入时间
}

void MatchingEngine::sendClientResponse(
    const MatchingEngineClientResponse* response) noexcept {
  common::GetCurrentTimeStr(time_str_);
  logger.log("%:% %() % 发送 %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
             response->toString());
  // 写入客户端响应队列
  auto next_write = outgoing_client_responses->GetNextToWriteTo();
  *next_write = *response;
  outgoing_client_responses->UpdateWriteIndex();
  TTT_MEASURE(T4_MatchingEngine_LFQueue_write, logger);  // 测量队列写入时间
}

}  // namespace exchange
