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

#include "../../common/logging.h"
#include "../../common/perf_utils.h"
#include "client_request.h"

namespace exchange {
constexpr std::size_t kMaxPendingRequests = 1024;

class FIFOSequencer {
 public:
  FIFOSequencer(ClientRequestLFQueue* requests, common::Logger* logger)
      : incomingRequest_(requests), logger_(logger) {}

  ~FIFOSequencer() {}

  auto addClientRequest(common::Nanos rxTime,
                        const MatchingEngineClientRequest& request) {
    if (pendingSize >= kMaxPendingRequests) {
      FATAL("To many pending requests ");
    }  // this is a right value, so we use the std::move
    pendingClientRequests.at(pendingSize++) =
        TimestampedClientRequest{rxTime, request};
  }

  // 接收到一批请求后，按接收时间排序，并将它们写入无锁队列供后续处理
  auto sequenceAndPublish() {
    if (UNLIKELY(!pendingSize)) return;

    common::GetCurrentTimeStr(time_str_);
    logger_->log("%:% %() % Processing % requests.\n", __FILE__, __LINE__,
                 __FUNCTION__, time_str_, pendingSize);

    std::sort(pendingClientRequests.begin(),
              pendingClientRequests.begin() + pendingSize);

    for (size_t i = 0; i < pendingSize; ++i) {
      const auto& client_request = pendingClientRequests.at(i);

      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % Writing RX: % Req % to FIFO.\n", __FILE__,
                   __LINE__, __FUNCTION__, time_str_,
                   client_request.receive_time,
                   client_request.request.toString());

      auto nextWrite = incomingRequest_->GetNextToWriteTo();
      *nextWrite = std::move(client_request.request);
      incomingRequest_->UpdateWriteIndex();
      TTT_MEASURE(T2_OrderManager_LFQueue_write, (*logger_));
    }
    pendingSize = 0;
  }

  FIFOSequencer() = delete;
  FIFOSequencer(const FIFOSequencer&) = delete;
  FIFOSequencer(const FIFOSequencer&&) = delete;
  FIFOSequencer& operator=(const FIFOSequencer&) = delete;
  FIFOSequencer& operator=(const FIFOSequencer&&) = delete;

 private:
  ClientRequestLFQueue* incomingRequest_ = nullptr;

  std::string time_str_;
  common::Logger* logger_ = nullptr;

  struct TimestampedClientRequest {
    common::Nanos receive_time = 0;
    MatchingEngineClientRequest request;

    bool operator<(const TimestampedClientRequest& rhs) const noexcept {
      return receive_time < rhs.receive_time;
    }
  };

  std::array<TimestampedClientRequest, kMaxPendingRequests>
      pendingClientRequests;
  size_t pendingSize = 0;
};

}  // namespace exchange
