#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "../common/ringBuffer.h"
#include "../common/types.h"
#include "../matcher/me_order_book.h"
#include "../order_manager/client_response.h"

namespace Exchange {

// Forward declaration
struct MEMarketUpdate;

class MarketDataPublisher {
 public:
  MarketDataPublisher(MEMarketUpdateLFQueue* marketUpdates,
                      const std::string& multicastAddr, int port);
  ~MarketDataPublisher();

  void start();
  void stop();

 private:
  void run();

  [[maybe_unused]] MEMarketUpdateLFQueue* marketUpdates_ = nullptr;
  [[maybe_unused]] std::string multicastAddr_;
  [[maybe_unused]] int port_ = 0;
  std::atomic<bool> run_{false};
  std::thread publisherThread_;
  Common::Logger logger_;

  std::string time_str_;
};

}  // namespace Exchange