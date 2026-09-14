#include "matching_engine.h"

#include <chrono>
#include <cstddef>

#include "../../common/macros.h"
#include "../../common/thread_utils.h"

// TODO: SHARDING

namespace exchange {

MatchingEngine::MatchingEngine(
    ClientRequestLFQueue* clientRequests,
    ClientResponseLFQueue* outgoingResponses,
    MatchingEngineMarketUpdateLFQueue* outgoingUpdates)
    : incoming_requests(clientRequests) {
  // 使用传入的队列初始化撮合引擎。在标的拥有各自的参考价格之前，
  // 所有订单簿共用同一个价格网格。
  for (size_t i = 0; i < symbol_order_book.size(); ++i) {
    symbol_order_book[i] = new BookCore(i, kDefaultPriceBand,
                                        *outgoingResponses, *outgoingUpdates);
  }
}

MatchingEngine::~MatchingEngine() {
  // Clean up resources if needed
  // The destructor should ensure proper cleanup of the matching engine
  stop();

  std::this_thread::sleep_for(std::chrono::seconds(1));

  incoming_requests = nullptr;

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

void MatchingEngine::ProcessClientRequest(
    const MatchingEngineClientRequest* client_request) noexcept {
  // 获取对应symbol的订单簿
  auto* order_book = symbol_order_book[client_request->symbolId_];
  // 根据请求类型处理
  switch (client_request->type_) {
    case ClientRequestType::NEW: {
      // 添加订单刀订单薄
      order_book->Add(client_request->clientId_, client_request->orderId_,
                      client_request->side_, client_request->price_,
                      client_request->quantity_);
    } break;
    case ClientRequestType::CANCELED: {
      order_book->Cancel(client_request->clientId_, client_request->orderId_);
    } break;
    default: {
      FATAL("收到无效的客户端请求类型：" +
            clientRequestTypeToString(client_request->type_));
    } break;
  }
}

void MatchingEngine::run() {
  while (running_) {
    // 从客户端请求队列中读取请求
    const auto client_request = incoming_requests->GetNextToRead();

    if (LIKELY(client_request)) {
      // 处理客户端请求
      ProcessClientRequest(client_request);
      // 标记已读取完成
      incoming_requests->UpdateReadIndex();
    }
  }
}

}  // namespace exchange
