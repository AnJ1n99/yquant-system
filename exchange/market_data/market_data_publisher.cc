#include "market_data_publisher.h"

namespace exchange {
MarketDataPublisher::MarketDataPublisher (
  MatchingEngineMarketUpdateLFQueue *market_updates, const std::string &iface,
  const std::string &snapshot_ip, int snapshot_port,
  const std::string &incremental_ip, int incremental_port) 
  : outgoing_md_updates_(market_updates),
    snapshot_md_updates_(common::kMaxMarketUpdates),
    run_(false),
    logger_("exchange_market_data_publisher.log"),
    incremental_socket_(logger_)
{
  ASSERT(incremental_socket_.init(incremental_ip, iface, incremental_port, /*is_listening*/ false) >= 0,
             "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));
  snapshot_synthesizer_ = new SnapshotSynthesizer(&snapshot_md_updates_, iface, snapshot_ip, snapshot_port);
}

MarketDataPublisher::~MarketDataPublisher() {
  stop();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(5s);
  
  delete snapshot_synthesizer_;
  snapshot_synthesizer_ = nullptr;
}

void MarketDataPublisher::start() {
  run_ = true;
  // Start the market data publishing thread
  // This thread will read from the marketUpdates queue and publish via UDP
  // multicast
  ASSERT(common::createAndStartThread(3, "Exchange/MarketDataPublisher", 
    [this]() { 
      run(); 
    }) != nullptr, "Failed to start MarketData thread.");

  snapshot_synthesizer_->start();
}

void MarketDataPublisher::stop() { 
  run_ = false; 

  snapshot_synthesizer_->stop();
}

void MarketDataPublisher::run() {
  while (run_) {
    // Process market updates from the queue and publish them via UDP multicast
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, time_str_);

    while (run_) {
      for (auto market_update = outgoing_md_updates_->GetNextToRead();
           outgoing_md_updates_->size() && market_update; 
           market_update = outgoing_md_updates_->GetNextToRead()) {

        TTT_MEASURE(T5_MarketDataPublisher_LFQueue_read, logger_);

        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % Sending seq:% %\n", __FILE__, __LINE__, __FUNCTION__,time_str_, next_inc_seq_num_,
                            market_update->toString().c_str());

        START_MEASURE(Exchange_McastSocket_Send);
        incremental_socket_.send(&next_inc_seq_num_, sizeof(next_inc_seq_num_));
        incremental_socket_.send(market_update, sizeof(MatchingEngineMarketUpdate));
        END_MEASURE(Exchange_McastSocket_Send, logger_);

        outgoing_md_updates_->UpdateReadIndex();

        // Forward this incremental market data update the snapshot synthesizer.
        auto next_write = snapshot_md_updates_.GetNextToWriteTo();
        next_write->seq_num = next_inc_seq_num_;
        next_write->update = *market_update;
        snapshot_md_updates_.UpdateWriteIndex();

        ++next_inc_seq_num_;
      }
      incremental_socket_.sendAndRecv();
    }
  }
}

}  // namespace exchange