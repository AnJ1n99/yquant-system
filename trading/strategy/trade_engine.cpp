#include "trade_engine.h"

namespace trading {
  using namespace common;
  TradeEngine::TradeEngine(common::ClientId client_id,
                           AlgoType algo_type,
                           const TradeEngineCfgHashMap &ticker_cfg,
                           exchange::ClientRequestLFQueue *client_requests,
                           exchange::ClientResponseLFQueue *client_responses,
                           exchange::MatchingEngineMarketUpdateLFQueue *market_updates)
      : client_id_(client_id), outgoing_ogw_requests_(client_requests), incoming_ogw_responses_(client_responses),
        incoming_md_updates_(market_updates), logger_("trading_engine_" + std::to_string(client_id) + ".log"),
        feature_engine_(&logger_),
        position_keeper_(&logger_),
        risk_manager_(&position_keeper_, ticker_cfg),
        order_manager_(&logger_, this, risk_manager_) {
    for (size_t i = 0; i < ticker_order_book_.size(); ++i) {
      ticker_order_book_[i] = new MarketOrderBook(i, &logger_);
      ticker_order_book_[i]->setTradeEngine(this);
    }

    // Initialize the function wrappers for the callbacks for order book changes, trade events and client responses.
    algoOnOrderBookUpdate_ = [this](auto ticker_id, auto price, auto side, auto book) {
      defaultAlgoOnOrderBookUpdate(ticker_id, price, side, book);
    };
    algoOnTradeUpdate_ = [this](auto market_update, auto book) { defaultAlgoOnTradeUpdate(market_update, book); };
    algoOnOrderUpdate_ = [this](auto client_response) { defaultAlgoOnOrderUpdate(client_response); };

    // Create the trading algorithm instance based on the AlgoType provided.
    // The constructor will override the callbacks above for order book changes, trade events and client responses.
    if (algo_type == AlgoType::MAKER) {
      mm_algo_ = new MarketMaker(&logger_, this, &feature_engine_, &order_manager_, ticker_cfg);
    } else if (algo_type == AlgoType::TAKER) {
      taker_algo_ = new LiquidityTaker(&logger_, this, &feature_engine_, &order_manager_, ticker_cfg);
    }

    for (SymbolId i = 0; i < ticker_cfg.size(); ++i) {
      common::GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % Initialized % Ticker:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                  time_str_,
                  AlgoTypeToString(algo_type), i,
                  ticker_cfg.at(i).toString());
    }
  }

  TradeEngine::~TradeEngine() {
    stop();

    delete mm_algo_; mm_algo_ = nullptr;
    delete taker_algo_; taker_algo_ = nullptr;

    for (auto &order_book: ticker_order_book_) {
      delete order_book;
      order_book = nullptr;
    }

    outgoing_ogw_requests_ = nullptr;
    incoming_ogw_responses_ = nullptr;
    incoming_md_updates_ = nullptr;
  }

  auto TradeEngine::stop() -> void {
    if (!thread_) return;
    stopping_ = true;
    run_ = false;
    if (thread_->joinable()) thread_->join();
    thread_.reset();
    common::GetCurrentTimeStr(time_str_);
    logger_.log("% 持仓\n%\n", time_str_, position_keeper_.toString());
  }

  /// Write a client request to the lock free queue for the order server to consume and send to the exchange.
  auto TradeEngine::sendClientRequest(const exchange::MatchingEngineClientRequest *client_request) noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                client_request->toString().c_str());
    auto next_write = outgoing_ogw_requests_->GetNextToWriteTo();
    *next_write = *client_request;
    outgoing_ogw_requests_->UpdateWriteIndex();
    TTT_MEASURE(T10_TradeEngine_LFQueue_write, logger_);
  }

  /// Main loop for this thread - processes incoming client responses and market data updates which in turn may generate client requests.
  auto TradeEngine::run() noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, time_str_);
    while (run_ || incoming_ogw_responses_->size() || incoming_md_updates_->size()) {
      for (auto client_response = incoming_ogw_responses_->GetNextToRead(); client_response; client_response = incoming_ogw_responses_->GetNextToRead()) {
        TTT_MEASURE(T9t_TradeEngine_LFQueue_read, logger_);

        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                    client_response->toString().c_str());
        onOrderUpdate(client_response);
        incoming_ogw_responses_->UpdateReadIndex();
        last_event_time_ = common::GetCurrentNanos();
      }

      for (auto market_update = incoming_md_updates_->GetNextToRead(); market_update; market_update = incoming_md_updates_->GetNextToRead()) {
        TTT_MEASURE(T9_TradeEngine_LFQueue_read, logger_);

        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                    market_update->toString().c_str());
        ASSERT(market_update->symbolId_ < ticker_order_book_.size(),
               "Unknown ticker-id on update:" + market_update->toString());
        ticker_order_book_[market_update->symbolId_]->onMarketUpdate(market_update);
        incoming_md_updates_->UpdateReadIndex();
        last_event_time_ = common::GetCurrentNanos();
      }
    }
  }

  /// Process changes to the order book - updates the position keeper, feature engine and informs the trading algorithm about the update.
  auto TradeEngine::onOrderBookUpdate(SymbolId ticker_id, Price price, Side side, MarketOrderBook *book) noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                time_str_, ticker_id, std::to_string(price).c_str(),
                std::to_string(static_cast<unsigned>(side)).c_str());

    auto bbo = book->getBBO();

    START_MEASURE(Trading_PositionKeeper_updateBBO);
    position_keeper_.updateBBO(ticker_id, bbo);
    END_MEASURE(Trading_PositionKeeper_updateBBO, logger_);

    START_MEASURE(Trading_FeatureEngine_onOrderBookUpdate);
    feature_engine_.onOrderBookUpdate(ticker_id, price, side, book);
    END_MEASURE(Trading_FeatureEngine_onOrderBookUpdate, logger_);

    START_MEASURE(Trading_TradeEngine_algoOnOrderBookUpdate_);
    if (!stopping_) algoOnOrderBookUpdate_(ticker_id, price, side, book);
    END_MEASURE(Trading_TradeEngine_algoOnOrderBookUpdate_, logger_);
  }

  /// Process trade events - updates the  feature engine and informs the trading algorithm about the trade event.
  auto TradeEngine::onTradeUpdate(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook *book) noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                market_update->toString().c_str());

    START_MEASURE(Trading_FeatureEngine_onTradeUpdate);
    feature_engine_.onTradeUpdate(market_update, book);
    END_MEASURE(Trading_FeatureEngine_onTradeUpdate, logger_);

    START_MEASURE(Trading_TradeEngine_algoOnTradeUpdate_);
    if (!stopping_) algoOnTradeUpdate_(market_update, book);
    END_MEASURE(Trading_TradeEngine_algoOnTradeUpdate_, logger_);
  }

  /// Process client responses - updates the position keeper and informs the trading algorithm about the response.
  auto TradeEngine::onOrderUpdate(const exchange::MatchingEngineClientResponse *client_response) noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                client_response->toString().c_str());

    if (UNLIKELY(client_response->type_ == exchange::ClientResponseType::FILLED)) {
      START_MEASURE(Trading_PositionKeeper_addFill);
      position_keeper_.addFill(client_response);
      END_MEASURE(Trading_PositionKeeper_addFill, logger_);
    }

    START_MEASURE(Trading_TradeEngine_algoOnOrderUpdate_);
    algoOnOrderUpdate_(client_response);
    END_MEASURE(Trading_TradeEngine_algoOnOrderUpdate_, logger_);
  }
}
