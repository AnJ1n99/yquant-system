#pragma once

#include "../matcher/me_order_book.h"
#include "../matcher/me_order.h"
#include "market_update.h"
#include "../common/logging.h"
#include <vector>
#include <atomic>
#include <thread>

namespace Exchange {

class SnapshotSynthesizer {
public:
    SnapshotSynthesizer(OrderBookHashMap* tickerOrderBook, MEMarketUpdateLFQueue* marketUpdates);
    ~SnapshotSynthesizer();

    void start();
    void stop();

    void generateSnapshot(SymbolId symbolId);
    void generateAllSnapshots();

private:
    void publishSnapshot(const std::vector<MEMarketUpdate>& snapshot);

    [[maybe_unused]] OrderBookHashMap* tickerOrderBook_ = nullptr;
    [[maybe_unused]] MEMarketUpdateLFQueue* marketUpdates_ = nullptr;
    std::atomic<bool> run_{false};
    std::thread snapshotThread_;
    Common::Logger logger_;
    
    std::string time_str_;
};

} // namespace Exchange