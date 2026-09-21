#pragma once

#include <optional>

#include "common/macros.h"
#include "common/logging.h"

#include "exchange/order_manager/client_response.h"
#include "exchange/matcher/price_levels.h"

#include "om_order.h"
#include "risk_manager.h"


namespace trading {
  using namespace common;
  class TradeEngine;

  /// Manages orders for a trading algorithm, hides the complexity of order management to simplify trading strategies.
  class OrderManager {
  public:
    OrderManager(common::Logger *logger, TradeEngine *trade_engine, RiskManager& risk_manager)
        : trade_engine_(trade_engine), risk_manager_(risk_manager), logger_(logger) {
    }

    /// Process an order update from a client response and update the state of the orders being managed.
    auto onOrderUpdate(const exchange::MatchingEngineClientResponse *client_response) noexcept -> void {
      common::GetCurrentTimeStr(time_str_);
      logger_->log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, time_str_,
                   client_response->toString().c_str());
      if (client_response->type_ == exchange::ClientResponseType::INVALID) return;
      OMOrder* order = nullptr;
      for (auto& candidate : ticker_side_order_.at(client_response->symbol_id_)) {
        if (candidate.order_state_ != OMOrderState::INVALID &&
            candidate.order_id_ == client_response->client_order_id_) {
          order = &candidate;
          break;
        }
      }
      // 拒绝回报没有方向字段；旧订单的迟到回报不能覆盖当前订单。
      if (order == nullptr) return;

      switch (client_response->type_) {
        case exchange::ClientResponseType::ACCEPTED: {
          order->order_state_ = OMOrderState::LIVE;
        }
          break;
        case exchange::ClientResponseType::CANCELED: {
          order->order_state_ = OMOrderState::DEAD;
        }
          break;
        case exchange::ClientResponseType::FILLED: {
          order->qty_ = client_response->remaining_quantity_;
          if(!order->qty_)
            order->order_state_ = OMOrderState::DEAD;
        }
          break;
        case exchange::ClientResponseType::CANCEL_REJECTED:
          // 当前交易所只在订单已不存在时拒绝撤单。
          order->order_state_ = OMOrderState::DEAD;
          break;
        case exchange::ClientResponseType::INVALID: {
        }
          break;
      }
    }

    /// Send a new order with specified attribute, and update the OMOrder object passed here.
    auto newOrder(OMOrder *order, SymbolId ticker_id, Price price, Side side, Quantity qty) noexcept -> void;

    /// Send a cancel for the specified order, and update the OMOrder object passed here.
    auto cancelOrder(OMOrder *order) noexcept -> void;

    /// Move a single order on the specified side so that it has the specified price and quantity.
    /// This will perform risk checks prior to sending the order, and update the OMOrder object passed here.
    auto moveOrder(OMOrder *order, SymbolId ticker_id, std::optional<Price> price, Side side, Quantity qty) noexcept {
      // 当前交易所所有标的使用此价格带，越界报价按不报价处理。
      const auto& band = exchange::kDefaultPriceBand;
      if (price && (*price < band.base_price ||
                    (*price - band.base_price) % band.tick_size != 0 ||
                    band.ToTick(*price) >= exchange::kTickCount)) {
        price.reset();
      }
      switch (order->order_state_) {
        case OMOrderState::LIVE: {
          if(!price || order->price_ != *price || order->qty_ != qty) {
            START_MEASURE(Trading_OrderManager_cancelOrder);
            cancelOrder(order);
            END_MEASURE(Trading_OrderManager_cancelOrder, (*logger_));
          }
        }
          break;
        case OMOrderState::INVALID:
        case OMOrderState::DEAD: {
          if(LIKELY(price.has_value())) {
            START_MEASURE(Trading_RiskManager_checkPreTradeRisk);
            const auto risk_result = risk_manager_.checkPreTradeRisk(ticker_id, side, qty);
            END_MEASURE(Trading_RiskManager_checkPreTradeRisk, (*logger_));
            if(LIKELY(risk_result == RiskCheckResult::ALLOWED)) {
              START_MEASURE(Trading_OrderManager_newOrder);
              newOrder(order, ticker_id, *price, side, qty);
              END_MEASURE(Trading_OrderManager_newOrder, (*logger_));
            } else {
              common::GetCurrentTimeStr(time_str_);
              logger_->log("%:% %() % Ticker:% Side:% Quantity:% RiskCheckResult:%\n", __FILE__, __LINE__, __FUNCTION__,
                           time_str_,
                           std::to_string(ticker_id), std::to_string(static_cast<unsigned>(side)), std::to_string(qty),
                            riskCheckResultToString(risk_result));
            }
          }
        }
          break;
        case OMOrderState::PENDING_NEW:
        case OMOrderState::PENDING_CANCEL:
          break;
      }
    }

    /// Have orders of quantity clip at the specified buy and sell prices.
    /// This can result in new orders being sent if there are none.
    /// This can result in existing orders being cancelled if they are not at the specified price or of the specified quantity.
    /// std::nullopt 表示不在该侧报价，价格 0 仍是有效价格。
    auto moveOrders(SymbolId ticker_id, std::optional<Price> bid_price, std::optional<Price> ask_price, Quantity clip) noexcept {
      {
        auto bid_order = &(ticker_side_order_.at(ticker_id).at((static_cast<size_t>(Side::BUY) - 1)));
        START_MEASURE(Trading_OrderManager_moveOrder);
        moveOrder(bid_order, ticker_id, bid_price, Side::BUY, clip);
        END_MEASURE(Trading_OrderManager_moveOrder, (*logger_));
      }

      {
        auto ask_order = &(ticker_side_order_.at(ticker_id).at((static_cast<size_t>(Side::SELL) - 1)));
        START_MEASURE(Trading_OrderManager_moveOrder);
        moveOrder(ask_order, ticker_id, ask_price, Side::SELL, clip);
        END_MEASURE(Trading_OrderManager_moveOrder, (*logger_));
      }
    }

    /// Helper method to fetch the buy and sell OMOrders for the specified SymbolId.
    auto getOMOrderSideHashMap(SymbolId ticker_id) const {
      return &(ticker_side_order_.at(ticker_id));
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    OrderManager() = delete;

    OrderManager(const OrderManager &) = delete;

    OrderManager(const OrderManager &&) = delete;

    OrderManager &operator=(const OrderManager &) = delete;

    OrderManager &operator=(const OrderManager &&) = delete;

  private:
    /// The parent trade engine object, used to send out client requests.
    TradeEngine *trade_engine_ = nullptr;

    /// Risk manager to perform pre-trade risk checks.
    const RiskManager& risk_manager_;

    std::string time_str_;
    common::Logger *logger_ = nullptr;

    /// Hash map container from SymbolId -> Side -> OMOrder.
    OMOrderTickerSideHashMap ticker_side_order_{};

    /// Used to set OrderIds on outgoing new order requests.
    OrderId next_order_id_ = 1;
  };
}
