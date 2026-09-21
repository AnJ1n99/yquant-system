#pragma once

#include <array>
#include <sstream>
#include "common/types.h"


namespace trading {
  using namespace common;
  /// Used by the trade engine to represent a single order in the limit order book.
  struct MarketOrder {
    OrderId order_id_ = 0;
    Side side_ = Side::BUY;
    Price price_ = 0;
    Quantity qty_ = 0;
    Priority priority_ = 0;

    /// MarketOrder also serves as a node in a doubly linked list of all orders at price level arranged in FIFO order.
    MarketOrder *prev_order_ = nullptr;
    MarketOrder *next_order_ = nullptr;

    /// Only needed for use with MemPool.
    MarketOrder() = default;

    MarketOrder(OrderId order_id, Side side, Price price, Quantity qty, Priority priority, MarketOrder *prev_order, MarketOrder *next_order) noexcept
        : order_id_(order_id), side_(side), price_(price), qty_(qty), priority_(priority), prev_order_(prev_order), next_order_(next_order) {}

    auto toString() const -> std::string;
  };

  /// Hash map from OrderId -> MarketOrder.
  typedef std::array<MarketOrder *, kMaxOrderIds> OrderHashMap;

  /// Used by the trade engine to represent a price level in the limit order book.
  /// Internally maintains a list of MarketOrder objects arranged in FIFO order.
  struct MarketOrdersAtPrice {
    Side side_ = Side::BUY;
    Price price_ = 0;

    MarketOrder *first_mkt_order_ = nullptr;

    /// MarketOrdersAtPrice also serves as a node in a doubly linked list of price levels arranged in order from most aggressive to least aggressive price.
    MarketOrdersAtPrice *prev_entry_ = nullptr;
    MarketOrdersAtPrice *next_entry_ = nullptr;

    /// Only needed for use with MemPool.
    MarketOrdersAtPrice() = default;

    MarketOrdersAtPrice(Side side, Price price, MarketOrder *first_mkt_order, MarketOrdersAtPrice *prev_entry, MarketOrdersAtPrice *next_entry)
        : side_(side), price_(price), first_mkt_order_(first_mkt_order), prev_entry_(prev_entry), next_entry_(next_entry) {}

    auto toString() const {
      std::stringstream ss;
      ss << "MarketOrdersAtPrice["
         << "side:" << std::to_string(static_cast<unsigned>(side_)) << " "
         << "price:" << std::to_string(price_) << " "
         << "first_mkt_order:" << (first_mkt_order_ ? first_mkt_order_->toString() : "null") << " "
         << "prev:" << std::to_string(prev_entry_ ? prev_entry_->price_ : 0) << " "
         << "next:" << std::to_string(next_entry_ ? next_entry_->price_ : 0) << "]";

      return ss.str();
    }
  };

  /// Hash map from Price -> MarketOrdersAtPrice.
  typedef std::array<MarketOrdersAtPrice *, kMaxPriceLevels> OrdersAtPriceHashMap;

  /// Represents a Best Bid Offer (BBO) abstraction for components which only need a small summary of top of book price and liquidity instead of the full order book.
  struct BBO {
    // 数量为 0 表示该侧为空，价格 0 本身仍然有效。
    Price bid_price_ = 0, ask_price_ = 0;
    Quantity bid_qty_ = 0, ask_qty_ = 0;

    auto toString() const {
      std::stringstream ss;
      ss << "BBO{"
         << std::to_string(bid_qty_) << "@" << std::to_string(bid_price_)
         << "X"
         << std::to_string(ask_price_) << "@" << std::to_string(ask_qty_)
         << "}";

      return ss.str();
    };
  };
}
