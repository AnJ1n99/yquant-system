#pragma once

#include <cmath>
#include <limits>

#include "market_order_book.h"

#include "common/macros.h"
#include "common/logging.h"


namespace trading {
  using namespace common;
  /// Sentinel value to represent invalid / uninitialized feature value.
  constexpr auto Feature_INVALID = std::numeric_limits<double>::quiet_NaN();

  class FeatureEngine {
  public:
    FeatureEngine(common::Logger *logger)
        : logger_(logger) {
    }

    /// Process a change in order book and in this case compute the fair market price.
    auto onOrderBookUpdate(SymbolId ticker_id, Price price, Side side, MarketOrderBook* book) noexcept -> void {
      const auto bbo = book->getBBO();
      if(LIKELY(bbo->bid_qty_ != 0 && bbo->ask_qty_ != 0)) {
        mkt_price_ = (static_cast<double>(bbo->bid_price_) * bbo->ask_qty_ + static_cast<double>(bbo->ask_price_) * bbo->bid_qty_) / (static_cast<double>(bbo->bid_qty_) + bbo->ask_qty_);
      } else {
        mkt_price_ = Feature_INVALID;
      }

      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % ticker:% price:% side:% mkt-price:% agg-trade-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                   time_str_, ticker_id, std::to_string(price).c_str(),
                   std::to_string(static_cast<unsigned>(side)).c_str(), mkt_price_, agg_trade_qty_ratio_);
    }

    /// Process a trade event and in this case compute the feature to capture aggressive trade quantity ratio against the BBO quantity.
    auto onTradeUpdate(const exchange::MatchingEngineMarketUpdate *market_update, MarketOrderBook* book) noexcept -> void {
      const auto bbo = book->getBBO();
      if(LIKELY(bbo->bid_qty_ != 0 && bbo->ask_qty_ != 0)) {
        agg_trade_qty_ratio_ = static_cast<double>(market_update->quantity_) / (market_update->side_ == Side::BUY ? bbo->ask_qty_ : bbo->bid_qty_);
      } else {
        agg_trade_qty_ratio_ = Feature_INVALID;
      }

      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % % mkt-price:% agg-trade-ratio:%\n", __FILE__, __LINE__, __FUNCTION__,
                   time_str_,
                   market_update->toString().c_str(), mkt_price_, agg_trade_qty_ratio_);
    }

    auto getMktPrice() const noexcept {
      return mkt_price_;
    }

    auto getAggTradeQtyRatio() const noexcept {
      return agg_trade_qty_ratio_;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    FeatureEngine() = delete;

    FeatureEngine(const FeatureEngine &) = delete;

    FeatureEngine(const FeatureEngine &&) = delete;

    FeatureEngine &operator=(const FeatureEngine &) = delete;

    FeatureEngine &operator=(const FeatureEngine &&) = delete;

  private:
    std::string time_str_;
    common::Logger *logger_ = nullptr;

    /// The two features we compute in our feature engine.
    double mkt_price_ = Feature_INVALID, agg_trade_qty_ratio_ = Feature_INVALID;
  };
}
