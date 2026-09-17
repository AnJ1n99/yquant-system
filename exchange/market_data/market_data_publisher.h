#pragma once

#include "market_update.h"
#include "snapshot_synthesizer.h"

namespace exchange {

// Forward declaration
struct MatchingEngineMarketUpdate;

class MarketDataPublisher {
 public:
   MarketDataPublisher(MatchingEngineMarketUpdateLFQueue *market_updates, const std::string &iface,
                           const std::string &snapshot_ip, int snapshot_port,
                           const std::string &incremental_ip, int incremental_port);
  ~MarketDataPublisher();

  void start();
  void stop();

  void run();

  MarketDataPublisher() = delete;
  
  MarketDataPublisher(const MarketDataPublisher &) = delete;
  
  MarketDataPublisher(const MarketDataPublisher &&) = delete;
  
  MarketDataPublisher &operator=(const MarketDataPublisher &) = delete;
  
  MarketDataPublisher &operator=(const MarketDataPublisher &&) = delete;

 private:

  std::size_t next_inc_seq_num_ = 1;

  MatchingEngineMarketUpdateLFQueue *outgoing_md_updates_ = nullptr;
  MarketDataPublisherMarketUpdateLFQueue snapshot_md_updates_;
  
  volatile bool run_ = false;

  common::Logger logger_;
  std::string time_str_;

  common::McastSocket incremental_socket_;
  SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
};
}  // namespace exchange
