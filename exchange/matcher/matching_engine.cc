#include "matching_engine.h"

#include "../../common/logging.h"
#include "../../common/thread_utils.h"
#include "../../common/time_utils.h"
#include <cstddef>

namespace Exchange {

MatchingEngine::MatchingEngine (
    ClientRequestLFQueue *clientRequests,
    ClientResponseLFQueue *outgoingResponses,
    MEMarketUpdateLFQueue *outgoingUpdates
) : incoming_requests(clientRequests), outgoing_ogw_responses(outgoingResponses), outgoing_md_updates(outgoingUpdates), logger("MatchingEngine.log")
{
    // Initialize the matching engine with the provided queues
    for (size_t i = 0; i < symbol_order_book.size(); ++i) {
        symbol_order_book[i] = new MEOrderBook(i, this, &logger);
    }
}

MatchingEngine::~MatchingEngine() {
    // Clean up resources if needed
    // The destructor should ensure proper cleanup of the matching engine
    stop();

    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);

    incoming_requests      = nullptr;
    outgoing_ogw_responses = nullptr;
    outgoing_md_updates    = nullptr;

    for (auto& order_book : symbol_order_book) {
        delete order_book;
        order_book = nullptr;
    }
}

void MatchingEngine::start() {
    running_ = true;
    // Start the matching engine processing loop
    ASSERT(Common::createAndStartThread(2, "Exchange/MatchingEngine", [this]() { run(); }) != nullptr, "Failed to start MatchingEngine thread.");
}

void MatchingEngine::stop() {
    running_ = false;
    // Stop the matching engine processing and perform cleanup if needed
}

void MatchingEngine::processClientRequest(const MEClientRequest* client_request) noexcept {
    // 获取对应symbol的订单簿
    auto* order_book = symbol_order_book[client_request->symbolId_];
    // 根据请求类型处理
    switch (client_request->type_) {
        case ClientRequestType::NEW: {
            // 添加订单刀订单薄
            START_MEASURE(Exchange_MEOrderBook_add);
            order_book->add(client_request->clientId_, client_request->orderId_, client_request->symbolId_,
                           client_request->side_, client_request->price_, client_request->qty_);
            END_MEASURE(Exchange_MEOrderBook_add, logger);
        }
            break;
        case ClientRequestType::CANCELED: {
            START_MEASURE(Exchange_ME_Cancel);
            order_book->cancel(client_request->clientId_, client_request->orderId_);
            END_MEASURE(Exchange_ME_Cancel, logger);
        }
            break;
        default: {
            FATAL("收到无效的客户端请求类型：" + clientRequestTypeToString(client_request->type_));
        }
            break;
    }
}

void MatchingEngine::run() {
    Common::getCurrentTimeStr(time_str_);
    logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, "MatchingEngine thread started at " + time_str_);

    while (running_) {
        // 从客户端请求队列中读取请求
        const auto client_request = incoming_requests->getNextToRead();

        if (LIKELY(client_request)) {
            TTT_MEASURE(T3_MatchingEngine_LFQueue_read, logger);  // 测量队列读取时间

            // 记录日志
            Common::getCurrentTimeStr(time_str_);
            logger.log("%:% %() % Processing request: %\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      time_str_,
                      client_request->toString());

            // 处理客户端请求
            START_MEASURE(Exchange_MatchingEngine_processClientRequest);
            processClientRequest(client_request);
            END_MEASURE(Exchange_MatchingEngine_processClientRequest, logger);  // 测量处理时间
            // 标记已读取完成
            incoming_requests->updateReadIndex();
        }
    }

    logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, "MatchingEngine thread stopped");
}

void MatchingEngine::sendMarketUpdate(const MEMarketUpdate* update) noexcept {
    Common::getCurrentTimeStr(time_str_);
    logger.log("%:% %() % Sending market update: %\n",
              __FILE__, __LINE__, __FUNCTION__,
              time_str_,
              update->toString());
    
    auto next_write = outgoing_md_updates->getNextToWriteTo();
    *next_write = *update;
    outgoing_md_updates->updateWriteIndex();
    TTT_MEASURE(T4t_MatchingEngine_LFQueue_write, logger);  // 测量队列写入时间
}

void MatchingEngine::sendClientResponse(const MEClientResponse* response) noexcept {
    Common::getCurrentTimeStr(time_str_);
    logger.log("%:% %() % 发送 %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, response->toString());
    // 写入客户端响应队列
    auto next_write = outgoing_ogw_responses->getNextToWriteTo();
    *next_write = *response;
    outgoing_ogw_responses->updateWriteIndex();
    TTT_MEASURE(T4_MatchingEngine_LFQueue_write, logger);  // 测量队列写入时间
}

} // namespace Exchange
