#include "market_order_book.h"

#include <limits>

#include "trade_engine.h"

namespace trading {
  using namespace common;
  MarketOrderBook::MarketOrderBook(SymbolId ticker_id, Logger *logger)
      : ticker_id_(ticker_id), orders_at_price_pool_(kMaxPriceLevels), order_pool_(kMaxOrderIds), logger_(logger) {
  }

  MarketOrderBook::~MarketOrderBook() {
    common::GetCurrentTimeStr(time_str_);
    logger_->log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__,
                 time_str_, toString(false, true));

    trade_engine_ = nullptr;
    bids_by_price_ = asks_by_price_ = nullptr;
    oid_to_order_.fill(nullptr);
  }

  /// Process market data update and update the limit order book.
  auto MarketOrderBook::onMarketUpdate(const exchange::MatchingEngineMarketUpdate *market_update) noexcept -> void {
    const auto bid_updated = market_update->type_ == exchange::MarketUpdateType::CLEAR ||
        (market_update->side_ == Side::BUY && (!bids_by_price_ || market_update->price_ >= bids_by_price_->price_));
    const auto ask_updated = market_update->type_ == exchange::MarketUpdateType::CLEAR ||
        (market_update->side_ == Side::SELL && (!asks_by_price_ || market_update->price_ <= asks_by_price_->price_));

    switch (market_update->type_) {
      case exchange::MarketUpdateType::ADD: {
        auto order = order_pool_.allocate(market_update->orderId_, market_update->side_, market_update->price_,
                                          market_update->quantity_, market_update->priority_, nullptr, nullptr);
        START_MEASURE(Trading_MarketOrderBook_addOrder);
        addOrder(order);
        END_MEASURE(Trading_MarketOrderBook_addOrder, (*logger_));
      }
        break;
      case exchange::MarketUpdateType::MODIFY: {
        auto order = oid_to_order_.at(market_update->orderId_);
        order->qty_ = market_update->quantity_;
      }
        break;
      case exchange::MarketUpdateType::CANCEL: {
        auto order = oid_to_order_.at(market_update->orderId_);
        START_MEASURE(Trading_MarketOrderBook_removeOrder);
        removeOrder(order);
        END_MEASURE(Trading_MarketOrderBook_removeOrder, (*logger_));
      }
        break;
      case exchange::MarketUpdateType::TRADE: {
        if (trade_engine_) trade_engine_->onTradeUpdate(market_update, this);
        return;
      }
        break;
      case exchange::MarketUpdateType::CLEAR: { // Clear the full limit order book and deallocate MarketOrdersAtPrice and MarketOrder objects.
        for (auto &order: oid_to_order_) {
          if (order)
            order_pool_.deallocate(order);
        }
        oid_to_order_.fill(nullptr);

        if(bids_by_price_) {
          for(auto bid = bids_by_price_->next_entry_; bid != bids_by_price_;) {
            auto* next = bid->next_entry_;
            orders_at_price_pool_.deallocate(bid);
            bid = next;
          }
          orders_at_price_pool_.deallocate(bids_by_price_);
        }

        if(asks_by_price_) {
          for(auto ask = asks_by_price_->next_entry_; ask != asks_by_price_;) {
            auto* next = ask->next_entry_;
            orders_at_price_pool_.deallocate(ask);
            ask = next;
          }
          orders_at_price_pool_.deallocate(asks_by_price_);
        }

        bids_by_price_ = asks_by_price_ = nullptr;
        price_orders_at_price_.fill(nullptr);
      }
        break;
      case exchange::MarketUpdateType::INVALID:
      case exchange::MarketUpdateType::SNAPSHOT_START:
      case exchange::MarketUpdateType::SNAPSHOT_END:
        return;
    }

    START_MEASURE(Trading_MarketOrderBook_updateBBO);
    updateBBO(bid_updated, ask_updated);
    END_MEASURE(Trading_MarketOrderBook_updateBBO, (*logger_));

    common::GetCurrentTimeStr(time_str_);
    logger_->log("%:% %() % % %", __FILE__, __LINE__, __FUNCTION__,
                 time_str_, market_update->toString(), bbo_.toString());

    if (trade_engine_) trade_engine_->onOrderBookUpdate(market_update->symbolId_, market_update->price_, market_update->side_, this);
  }

  auto MarketOrderBook::toString(bool detailed, bool validity_check) const -> std::string {
    std::stringstream ss;
    std::string time_str;

    auto printer = [&](std::stringstream &ss, MarketOrdersAtPrice *itr, Side side, Price &last_price,
                       bool sanity_check) {
      char buf[4096];
      Quantity qty = 0;
      size_t num_orders = 0;

      for (auto o_itr = itr->first_mkt_order_;; o_itr = o_itr->next_order_) {
        qty += o_itr->qty_;
        ++num_orders;
        if (o_itr->next_order_ == itr->first_mkt_order_)
          break;
      }
      sprintf(buf, " <px:%3s p:%3s n:%3s> %-3s @ %-5s(%-4s)",
              std::to_string(itr->price_).c_str(), std::to_string(itr->prev_entry_->price_).c_str(),
              std::to_string(itr->next_entry_->price_).c_str(),
              std::to_string(itr->price_).c_str(), std::to_string(qty).c_str(), std::to_string(num_orders).c_str());
      ss << buf;
      for (auto o_itr = itr->first_mkt_order_;; o_itr = o_itr->next_order_) {
        if (detailed) {
          sprintf(buf, "[oid:%s q:%s p:%s n:%s] ",
                  std::to_string(o_itr->order_id_).c_str(), std::to_string(o_itr->qty_).c_str(),
                  std::to_string(o_itr->prev_order_ ? o_itr->prev_order_->order_id_ : 0).c_str(),
                  std::to_string(o_itr->next_order_ ? o_itr->next_order_->order_id_ : 0).c_str());
          ss << buf;
        }
        if (o_itr->next_order_ == itr->first_mkt_order_)
          break;
      }

      ss << std::endl;

      if (sanity_check) {
        if ((side == Side::SELL && last_price >= itr->price_) || (side == Side::BUY && last_price <= itr->price_)) {
          FATAL("Bids/Asks not sorted by ascending/descending prices last:" + std::to_string(last_price) + " itr:" +
                itr->toString());
        }
        last_price = itr->price_;
      }
    };

    ss << "Ticker:" << std::to_string(ticker_id_) << std::endl;
    {
      auto ask_itr = asks_by_price_;
      auto last_ask_price = std::numeric_limits<Price>::min();
      for (size_t count = 0; ask_itr; ++count) {
        ss << "ASKS L:" << count << " => ";
        auto next_ask_itr = (ask_itr->next_entry_ == asks_by_price_ ? nullptr : ask_itr->next_entry_);
        printer(ss, ask_itr, Side::SELL, last_ask_price, validity_check);
        ask_itr = next_ask_itr;
      }
    }

    ss << std::endl << "                          X" << std::endl << std::endl;

    {
      auto bid_itr = bids_by_price_;
      auto last_bid_price = std::numeric_limits<Price>::max();
      for (size_t count = 0; bid_itr; ++count) {
        ss << "BIDS L:" << count << " => ";
        auto next_bid_itr = (bid_itr->next_entry_ == bids_by_price_ ? nullptr : bid_itr->next_entry_);
        printer(ss, bid_itr, Side::BUY, last_bid_price, validity_check);
        bid_itr = next_bid_itr;
      }
    }

    return ss.str();
  }
}
