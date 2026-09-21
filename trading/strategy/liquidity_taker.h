#pragma once

#include "common/macros.h"
#include "common/logging.h"

#include "order_manager.h"
#include "feature_engine.h"


namespace trading {
  using namespace common;
  class LiquidityTaker {
  public:
    LiquidityTaker(common::Logger *logger, TradeEngine *trade_engine, const FeatureEngine *feature_engine,
                   OrderManager *order_manager,
                   const TradeEngineCfgHashMap &ticker_cfg);

    /// Process order book updates, which for the liquidity taking algorithm is none.
    auto onOrderBookUpdate(SymbolId ticker_id, Price price, Side side, MarketOrderBook *) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % ticker:% price:% side:%\n", __FILE__, __LINE__, __FUNCTION__,
                   time_str_, ticker_id, std::to_string(price).c_str(),
                   std::to_string(static_cast<unsigned>(side)).c_str());
    }

    /// Process trade events, fetch the aggressive trade ratio from the feature engine, check against the trading threshold and send aggressive orders.
    auto onTradeUpdate(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook *book) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                   market_update->toString().c_str());

      const auto bbo = book->getBBO();
      const auto agg_qty_ratio = feature_engine_->getAggTradeQtyRatio();

      if (LIKELY(bbo->bid_qty_ != 0 && bbo->ask_qty_ != 0 && std::isfinite(agg_qty_ratio))) {
        common::GetCurrentTimeStr(time_str_);
        logger_->log("%:% %() % % agg-qty-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                     time_str_,
                     bbo->toString().c_str(), agg_qty_ratio);

        const auto clip = ticker_cfg_.at(market_update->symbolId_).clip_;
        const auto threshold = ticker_cfg_.at(market_update->symbolId_).threshold_;

        if (agg_qty_ratio >= threshold) {
          START_MEASURE(Trading_OrderManager_moveOrders);
          if (market_update->side_ == Side::BUY)
            order_manager_->moveOrders(market_update->symbolId_, bbo->ask_price_, std::nullopt, clip);
          else
            order_manager_->moveOrders(market_update->symbolId_, std::nullopt, bbo->bid_price_, clip);
          END_MEASURE(Trading_OrderManager_moveOrders, (*logger_));
        }
      }
    }

    /// Process client responses for the strategy's orders.
    auto onOrderUpdate(const exchange::MatchingEngineClientResponse *client_response) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                   client_response->toString().c_str());
      START_MEASURE(Trading_OrderManager_onOrderUpdate);
      order_manager_->onOrderUpdate(client_response);
      END_MEASURE(Trading_OrderManager_onOrderUpdate, (*logger_));
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    LiquidityTaker() = delete;

    LiquidityTaker(const LiquidityTaker &) = delete;

    LiquidityTaker(const LiquidityTaker &&) = delete;

    LiquidityTaker &operator=(const LiquidityTaker &) = delete;

    LiquidityTaker &operator=(const LiquidityTaker &&) = delete;

  private:
    /// The feature engine that drives the liquidity taking algorithm.
    const FeatureEngine *feature_engine_ = nullptr;

    /// Used by the liquidity taking algorithm to send aggressive orders.
    OrderManager *order_manager_ = nullptr;

    std::string time_str_;
    common::Logger *logger_ = nullptr;

    /// Holds the trading configuration for the liquidity taking algorithm.
    const TradeEngineCfgHashMap ticker_cfg_;
  };
}
