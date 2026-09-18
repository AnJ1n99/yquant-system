#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "common/thread_utils.h"
#include "common/time_utils.h"
#include "common/ringBuffer.h"
#include "common/macros.h"
#include "common/logging.h"

#include "exchange/order_manager/client_request.h"
#include "exchange/order_manager/client_response.h"
#include "exchange/market_data/market_update.h"

#include "market_order_book.h"

#include "feature_engine.h"
#include "position_keeper.h"
#include "order_manager.h"
#include "risk_manager.h"

#include "market_maker.h"
#include "liquidity_taker.h"

namespace trading {
  using namespace common;
  class TradeEngine {
  public:
    TradeEngine(common::ClientId client_id,
                AlgoType algo_type,
                const TradeEngineCfgHashMap &ticker_cfg,
                exchange::ClientRequestLFQueue *client_requests,
                exchange::ClientResponseLFQueue *client_responses,
                exchange::MatchingEngineMarketUpdateLFQueue *market_updates);

    ~TradeEngine();

    /// Start and stop the trade engine main thread.
    auto start() -> void {
      if (thread_) return;
      stopping_ = false;
      run_ = true;
      thread_.reset(common::createAndStartThread(-1, "trading/TradeEngine", [this] { run(); }));
      ASSERT(thread_ != nullptr, "Failed to start TradeEngine thread.");
    }

    auto stop() -> void;

    /// Main loop for this thread - processes incoming client responses and market data updates which in turn may generate client requests.
    auto run() noexcept -> void;

    /// Write a client request to the lock free queue for the order server to consume and send to the exchange.
    auto sendClientRequest(const exchange::MatchingEngineClientRequest *client_request) noexcept -> void;

    /// Process changes to the order book - updates the position keeper, feature engine and informs the trading algorithm about the update.
    auto onOrderBookUpdate(SymbolId ticker_id, Price price, Side side, MarketOrderBook *book) noexcept -> void;

    /// Process trade events - updates the  feature engine and informs the trading algorithm about the trade event.
    auto onTradeUpdate(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook *book) noexcept -> void;

    /// Process client responses - updates the position keeper and informs the trading algorithm about the response.
    auto onOrderUpdate(const exchange::MatchingEngineClientResponse *client_response) noexcept -> void;

    /// Function wrappers to dispatch order book updates, trade events and client responses to the trading algorithm.
    std::function<void(SymbolId ticker_id, Price price, Side side, MarketOrderBook *book)> algoOnOrderBookUpdate_;
    std::function<void(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook *book)> algoOnTradeUpdate_;
    std::function<void(const exchange::MatchingEngineClientResponse *client_response)> algoOnOrderUpdate_;

    auto initLastEventTime() {
      last_event_time_ = common::GetCurrentNanos();
    }

    auto silentSeconds() {
      return (common::GetCurrentNanos() - last_event_time_.load()) / NANOS_TO_SECS;
    }

    auto clientId() const {
      return client_id_;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    TradeEngine() = delete;

    TradeEngine(const TradeEngine &) = delete;

    TradeEngine(const TradeEngine &&) = delete;

    TradeEngine &operator=(const TradeEngine &) = delete;

    TradeEngine &operator=(const TradeEngine &&) = delete;

  private:
    /// This trade engine's ClientId.
    const ClientId client_id_;

    /// Hash map container from SymbolId -> MarketOrderBook.
    MarketOrderBookHashMap ticker_order_book_;

    /// Lock free queues.
    /// One to publish outgoing client requests to be consumed by the order gateway and sent to the exchange.
    /// Second to consume incoming client responses from, written to by the order gateway based on data received from the exchange.
    /// Third to consume incoming market data updates from, written to by the market data consumer based on data received from the exchange.
    exchange::ClientRequestLFQueue *outgoing_ogw_requests_ = nullptr;
    exchange::ClientResponseLFQueue *incoming_ogw_responses_ = nullptr;
    exchange::MatchingEngineMarketUpdateLFQueue *incoming_md_updates_ = nullptr;

    std::atomic<Nanos> last_event_time_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> run_{false};
    std::unique_ptr<std::thread> thread_;

    std::string time_str_;
    Logger logger_;

    /// Feature engine for the trading algorithms.
    FeatureEngine feature_engine_;

    /// Position keeper to track position, pnl and volume.
    PositionKeeper position_keeper_;

    RiskManager risk_manager_;
    OrderManager order_manager_;

    /// Market making or liquidity taking algorithm instance - only one of these is created in a single trade engine instance.
    MarketMaker *mm_algo_ = nullptr;
    LiquidityTaker *taker_algo_ = nullptr;

    /// Default methods to initialize the function wrappers.
    auto defaultAlgoOnOrderBookUpdate(SymbolId ticker_id, Price price, Side side, MarketOrderBook *) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                  time_str_, ticker_id, std::to_string(price).c_str(),
                  std::to_string(static_cast<unsigned>(side)).c_str());
    }

    auto defaultAlgoOnTradeUpdate(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook *) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                  market_update->toString().c_str());
    }

    auto defaultAlgoOnOrderUpdate(const exchange::MatchingEngineClientResponse *client_response) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                  client_response->toString().c_str());
    }
  };
}
