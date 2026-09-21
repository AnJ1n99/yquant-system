#include "snapshot_synthesizer.h"

#include "../common/logging.h"
#include "market_update.h"

namespace exchange {

SnapshotSynthesizer::SnapshotSynthesizer(
    MarketDataPublisherMarketUpdateLFQueue *market_updates, 
    const std::string &iface,
    const std::string &snapshot_ip, int snapshot_port)
    : snapshot_md_updates_(market_updates),
      logger_("SnapshotSynthesizer.log"),
      snapshot_socket_(logger_),
      order_pool_(common::kMaxOrderIds)
{
  ASSERT(snapshot_socket_.init(snapshot_ip, iface, snapshot_port, /*is_listening*/ false) >= 0,
             "Unable to create snapshot mcast socket. error:" + std::string(std::strerror(errno)));
  for(auto& orders : symbol_orders_) {
    orders.fill(nullptr);
  }
}

SnapshotSynthesizer::~SnapshotSynthesizer() { 
  stop(); 
}

void SnapshotSynthesizer::start() {
  run_ = true;
  
  ASSERT(common::createAndStartThread(4, "Exchange/SnapshotSynthesizer", 
    [this]() { 
      run(); 
    }) != nullptr,
    "Failed to start SnapshotSynthesizer thread.");
}

void SnapshotSynthesizer::stop() { 
  run_ = false; 
}

void SnapshotSynthesizer::run() {
  common::GetCurrentTimeStr(time_str_);
  logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, time_str_);

  while (run_) {
    for (auto market_update = snapshot_md_updates_->GetNextToRead(); 
         snapshot_md_updates_->size() && market_update; 
         market_update = snapshot_md_updates_->GetNextToRead()) {
           
      common::GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__, 
                  time_str_, market_update->toString().c_str());

      AddtoSnapshot(market_update);
      snapshot_md_updates_->UpdateReadIndex();
    }

    if (common::GetCurrentNanos() - last_snapshot_time_ > 60 * common::NANOS_TO_SECS) {
      last_snapshot_time_ = common::GetCurrentNanos();
      PublishSnapshot();
    }
  }
}

void SnapshotSynthesizer::AddtoSnapshot(const MarketDataPublisherMarketUpdate* market_update) {
  const auto& me_market_update = market_update->update;
  auto* orders = &symbol_orders_.at(me_market_update.symbolId_);

  switch (me_market_update.type_) {
    case MarketUpdateType::ADD: {
      auto order = orders->at(me_market_update.orderId_);
      ASSERT(order == nullptr, "Received:" + me_market_update.toString() + " but order already exists:" + (order ? order->toString() : ""));
      orders->at(me_market_update.orderId_) = order_pool_.allocate(me_market_update);
    }
      break;
    case MarketUpdateType::MODIFY: {
      auto order = orders->at(me_market_update.orderId_);
      ASSERT(order != nullptr, "Received:" + me_market_update.toString() + " but order does not exist.");
      ASSERT(order->orderId_ == me_market_update.orderId_, "Expecting existing order to match new one.");
      ASSERT(order->side_ == me_market_update.side_, "Expecting existing order to match new one.");
  
      order->quantity_ = me_market_update.quantity_;
      order->price_ = me_market_update.price_;
      }
        break;
    case MarketUpdateType::CANCEL: {
      auto order = orders->at(me_market_update.orderId_);
      ASSERT(order != nullptr, "Received:" + me_market_update.toString() + " but order does not exist.");
      ASSERT(order->orderId_ == me_market_update.orderId_, "Expecting existing order to match new one.");
      ASSERT(order->side_ == me_market_update.side_, "Expecting existing order to match new one.");
  
      order_pool_.deallocate(order);
      orders->at(me_market_update.orderId_) = nullptr;
      }
        break;
    case MarketUpdateType::SNAPSHOT_START:
    case MarketUpdateType::CLEAR:
    case MarketUpdateType::SNAPSHOT_END:
    case MarketUpdateType::TRADE:
    case MarketUpdateType::INVALID:
      break;
    }

    ASSERT(
        market_update->seq_num == static_cast<ssize_t>(last_inc_seq_num_) + 1,
        "Expected incremental seq_nums to increase.");
    last_inc_seq_num_ = market_update->seq_num;
}

/// Publish a full snapshot cycle on the snapshot multicast stream.
void SnapshotSynthesizer::PublishSnapshot() {
  ssize_t snapshot_size = 0;

  // The snapshot cycle starts with a SNAPSHOT_START message and orderId_ contains the last sequence number from the incremental market data stream used to build this snapshot.
  const MarketDataPublisherMarketUpdate start_market_update{
      snapshot_size++,
      {.type_ = MarketUpdateType::SNAPSHOT_START,
       .symbolId_ = 0,
       .orderId_ = last_inc_seq_num_,
       .side_ = common::Side{},
       .price_ = 0,
       .quantity_ = 0,
       .priority_ = 0}};
  common::GetCurrentTimeStr(time_str_);
  logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, start_market_update.toString().c_str());
  snapshot_socket_.send(&start_market_update, sizeof(MarketDataPublisherMarketUpdate));

  // Publish order information for each order in the limit order book for each instrument.
  for (common::SymbolId symbol_id = 0; symbol_id < symbol_orders_.size(); ++symbol_id) {
    const auto &orders = symbol_orders_.at(symbol_id);

    MatchingEngineMarketUpdate me_market_update{};
    me_market_update.type_ = MarketUpdateType::CLEAR;
    me_market_update.symbolId_ = symbol_id;

    // We start order information for each instrument by first publishing a CLEAR message so the downstream consumer can clear the order book.
    const MarketDataPublisherMarketUpdate clear_market_update{snapshot_size++, me_market_update};
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, clear_market_update.toString().c_str());
    snapshot_socket_.send(&clear_market_update, sizeof(MarketDataPublisherMarketUpdate));

    // Publish each order.
    for (const auto order : orders) {
      if (order) {
        const MarketDataPublisherMarketUpdate market_update{snapshot_size++, *order};
        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, market_update.toString().c_str());
        snapshot_socket_.send(&market_update, sizeof(MarketDataPublisherMarketUpdate));
        snapshot_socket_.sendAndRecv();
      }
    }
  }

  // The snapshot cycle ends with a SNAPSHOT_END message and orderId_ contains the last sequence number from the incremental market data stream used to build this snapshot.
  const MarketDataPublisherMarketUpdate end_market_update{
      snapshot_size++,
      {.type_ = MarketUpdateType::SNAPSHOT_END,
       .symbolId_ = 0,
       .orderId_ = last_inc_seq_num_,
       .side_ = common::Side{},
       .price_ = 0,
       .quantity_ = 0,
       .priority_ = 0}};
  common::GetCurrentTimeStr(time_str_);
  logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, end_market_update.toString().c_str());
  snapshot_socket_.send(&end_market_update, sizeof(MarketDataPublisherMarketUpdate));
  snapshot_socket_.sendAndRecv();

  common::GetCurrentTimeStr(time_str_);
  logger_.log("%:% %() % Published snapshot of % orders.\n", __FILE__, __LINE__, __FUNCTION__, time_str_, snapshot_size - 1);
}

}  // namespace exchange
