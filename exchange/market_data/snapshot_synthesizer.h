#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "../common/logging.h"
#include "../matcher/book_core.h"
#include "../matcher/price_levels.h"
#include "market_update.h"

namespace exchange {

class SnapshotSynthesizer {
 public:
  SnapshotSynthesizer(OrderBookHashMap* symbol_order_book,
                      MatchingEngineMarketUpdateLFQueue* marketUpdates);
  ~SnapshotSynthesizer();

  void start();
  void stop();

  void generateSnapshot(common::SymbolId symbolId);
  void generateAllSnapshots();

 private:
  void publishSnapshot(const std::vector<MatchingEngineMarketUpdate>& snapshot);

  [[maybe_unused]] OrderBookHashMap* symbol_order_book_ = nullptr;
  [[maybe_unused]] MatchingEngineMarketUpdateLFQueue* marketUpdates_ = nullptr;
  std::atomic<bool> run_{false};
  std::thread snapshotThread_;
  common::Logger logger_;

  std::string time_str_;
};

}  // namespace exchange
