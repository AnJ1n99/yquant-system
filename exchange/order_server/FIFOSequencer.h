/*
 * 序列器是使撮合引擎具有确定性的关键组件。
 * 它在订单被撮合引擎处理之前为每个传入的订单打上一个序列ID。
 * 它还为撮合引擎完成的每对执行(成交)打上序列ID。
 * 换句话说,序列器有一个入站实例和一个出站实例,每个实例维护自己的序列。
 * 每个序列器生成的序列必须是连续的数字,以便可以轻松检测到任何缺失的数字
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include "client_request.h"
#include "../../common/logging.h"

namespace Exchange {
constexpr std::size_t ME_MAX_PENDING_REQUESTS = 1024;

class FIFOSequencer {
public:
    FIFOSequencer(ClientRequestLFQueue* requests, Logger* logger) :
        incomingRequest_(requests),
        logger_(logger)
    {

    }

    ~FIFOSequencer() {

    }

    auto addClientRequest(Nanos rxTime, const MEClientRequest &request) {
        if (pendingSize >= ME_MAX_PENDING_REQUESTS) {
            FATAL("To many pending requests ");
        }                                         // this is a right value, so we use the std::move
        pendingClientRequests.at(pendingSize++) = std::move(RecvTimeClientRequest{rxTime, request});
    }

    auto sequenceAndPublish() {
        if (UNLIKELY(!pendingSize)) return;

        getCurrentTimeStr(time);
        logger_->log("%:% %() % Processing % requests.\n",
                     __FILE__, __LINE__, __FUNCTION__, time, pendingSize);

        std::sort(pendingClientRequests.begin(), pendingClientRequests.begin() + pendingSize);

        for (size_t i = 0; i < pendingSize; ++i) {
            const auto &client_request = pendingClientRequests.at(i);

            getCurrentTimeStr(time);
            logger_->log("%:% %() % Writing RX: % Req % to FIFO.\n",
                         __FILE__, __LINE__, __FUNCTION__, time, client_request.recvTime, client_request.request.toString());

            auto nextWrite = incomingRequest_->getNextToWriteTo();
            *nextWrite = std::move(client_request.request);
            incomingRequest_->updateWriteIndex();
            TTT_MEASURE(T2_OrderServer_LFQueue_write, (*logger_));

            }
        pendingSize = 0;
    }

    FIFOSequencer()                                = delete;
    FIFOSequencer(const FIFOSequencer &)           = delete;
    FIFOSequencer(const FIFOSequencer &&)          = delete;
    FIFOSequencer operator=(const FIFOSequencer&)  = delete;
    FIFOSequencer operator=(const FIFOSequencer&&) = delete;

private:
    ClientRequestLFQueue *incomingRequest_ = nullptr;

    std::string time;
    Logger *logger_ = nullptr;

    struct RecvTimeClientRequest {
        Nanos recvTime = 0;
        MEClientRequest request;

        auto operator<(const RecvTimeClientRequest &rhs) const {
            return (recvTime < rhs.recvTime);
        }
    };

    std::array<RecvTimeClientRequest, ME_MAX_PENDING_REQUESTS> pendingClientRequests;
    size_t pendingSize = 0;
};

}
