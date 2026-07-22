#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "../common/ringBuffer.h"
#include "../common/types.h"
#include "../matcher/matching_engine_order_book.h"
#include "../order_manager/client_response.h"

namespace exchange {

// Forward declaration
struct MatchingEngineMarketUpdate;

class MarketDataPublisher {
 public:
  MarketDataPublisher(MatchingEngineMarketUpdateLFQueue* marketUpdates,
                      const std::string& multicastAddr, int port);
  ~MarketDataPublisher();

  void start();
  void stop();

 private:
  void run();

  [[maybe_unused]] MatchingEngineMarketUpdateLFQueue* marketUpdates_ = nullptr;
  [[maybe_unused]] std::string multicastAddr_;
  [[maybe_unused]] int port_ = 0;
  std::atomic<bool> run_{false};
  std::thread publisherThread_;
  common::Logger logger_;

  std::string time_str_;
};

}  // namespace exchange