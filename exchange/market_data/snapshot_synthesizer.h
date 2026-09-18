/*
 * 周期性广播当前时刻全市场的全部存活订单状态。
 * 1. 独立维护全量状态镜像
 * 2. 锚定增量序列号
 * 
 * 使用独立的组播套接字与线程避免体积庞大的全量快照广播阻塞核心撮合引擎与实时增量行情的关键传输路径。
 */

#pragma once

#include "../../common/logging.h"
#include "../../common/types.h"
#include "../matcher/book_core.h"
#include "market_update.h"
#include "../../common/mcast_socket.h"

namespace exchange {

class SnapshotSynthesizer {
 public:

  SnapshotSynthesizer(MarketDataPublisherMarketUpdateLFQueue* market_updates_, const std::string& iface,
                      const std::string& snapshot_ip, int snapshot_port);

  ~SnapshotSynthesizer();
   
  void start();
  void stop();

  // Process an incremental market update and update the limit order book snapshot.
  void AddtoSnapshot(const MarketDataPublisherMarketUpdate* market_update);
  // Publish a full snapshot cycle on the snapshot multicast stream.
  void PublishSnapshot();
  // Main method for this thread - processes incremental updates from the market data publisher, updates the snapshot and publishes the snapshot periodically.
  auto run() -> void;

 public:
  SnapshotSynthesizer() = delete;
  
  SnapshotSynthesizer(const SnapshotSynthesizer &) = delete;
  
  SnapshotSynthesizer(const SnapshotSynthesizer &&) = delete;
  
  SnapshotSynthesizer &operator=(const SnapshotSynthesizer &) = delete;
  
  SnapshotSynthesizer &operator=(const SnapshotSynthesizer &&) = delete;
 private:
   MarketDataPublisherMarketUpdateLFQueue *snapshot_md_updates_ = nullptr;

   volatile bool run_ = false;

   common::Logger logger_;
   common::McastSocket snapshot_socket_;

   /// Hash map from SymbolId -> Full limit order book snapshot containing information for every live order.
   std::array<std::array<MatchingEngineMarketUpdate*, common::kMaxOrderIds>, common::kMaxSymbols> symbol_orders_;
   size_t last_inc_seq_num_ = 0;
   common::Nanos last_snapshot_time_ = 0;

  std::string time_str_;

  // Memory pool to manage MEMarketUpdate messages for the orders in the snapshot limit order books.
  common::MemPool<MatchingEngineMarketUpdate> order_pool_;
};

}  // namespace exchange
