#include "book_core.h"

#include <algorithm>

#include "../../common/macros.h"

namespace exchange {

using common::ClientId;
using common::OrderId;
using common::OrderId_INVALID;
using common::Price;
using common::Price_INVALID;
using common::Priority_INVALID;
using common::Quantity;
using common::Quantity_INVALID;
using common::Side;
using common::SymbolId;

BookCore::BookCore(SymbolId symbol_id, const PriceBand& band,
                   ClientResponseLFQueue& client_responses,
                   MatchingEngineMarketUpdateLFQueue& market_updates)
    : symbol_id_(symbol_id),
      band_(band),
      outgoing_client_responses_(client_responses),
      outgoing_market_updates_(market_updates),
      order_pool_(common::kMaxOrderIds),
      bids_(Side::BUY, &order_pool_),
      asks_(Side::SELL, &order_pool_) {
  ASSERT(band_.IsValid(), "Price band must be positive and representable");
  orders_.fill(nullptr);
}

BookCore::~BookCore() {
  // 挂单节点无需清理：它们是平凡可析构类型，存储归内存池所有。
  // 这里只拥有稀疏索引表。
  for (auto*& table : orders_) {
    delete table;
    table = nullptr;
  }
}

void BookCore::Add(ClientId client_id, OrderId client_order_id, Side side,
                   Price price, Quantity quantity) noexcept {
  const Tick tick = band_.ToTick(price);
  const auto market_order_id = NextMarketOrderId();

  client_response_ = {ClientResponseType::ACCEPTED,
                      client_id,
                      symbol_id_,
                      client_order_id,
                      market_order_id,
                      side,
                      price,
                      Quantity_INVALID,
                      quantity};
  SendClientResponse();

  TakerOrder taker{
      .client_id = client_id,
      .client_order_id = client_order_id,
      .market_order_id = market_order_id,
      .side = side,
      .price = price,
      .quantity = quantity,
  };

  const auto remaining_quantity = Match(taker);

  // 撮合后剩余的部分转为挂单，并对市场可见。
  if (LIKELY(remaining_quantity > 0)) {
    auto* order = Levels(side).AddOrder(tick, client_id, client_order_id,
                                        market_order_id, remaining_quantity);

    IndexOrder(client_id, client_order_id, order);

    market_update_ = {
        MarketUpdateType::ADD, symbol_id_,     market_order_id, side, price,
        remaining_quantity,    order->priority};
    SendMarketUpdate();
  }
}

void BookCore::Cancel(ClientId client_id, OrderId client_order_id) noexcept {
  auto* order = FindOrder(client_id, client_order_id);
  if (UNLIKELY(order == nullptr)) {
    client_response_ = {ClientResponseType::CANCEL_REJECTED,
                        client_id,
                        symbol_id_,
                        client_order_id,
                        OrderId_INVALID,
                        Side::INVALID,
                        Price_INVALID,
                        Quantity_INVALID,
                        Quantity_INVALID};
    SendClientResponse();
    return;
  }

  // 方向和价格是节点所在价位的属性，因此节点本身从不携带它们：
  // 方向即持有该价位的单侧订单簿，价格由槽位下标换算。
  const auto* level = order->level;
  const auto side = bids_.Owns(level) ? Side::BUY : Side::SELL;
  const auto price = band_.ToPrice(Levels(side).IndexOf(level));

  client_response_ = {
      ClientResponseType::CANCELED, client_id, symbol_id_, client_order_id,
      order->market_order_id,       side,      price,      Quantity_INVALID,
      order->remaining_quantity};
  SendClientResponse();

  market_update_ = {MarketUpdateType::CANCEL,
                    symbol_id_,
                    order->market_order_id,
                    side,
                    price,
                    Quantity_INVALID,
                    Priority_INVALID};
  SendMarketUpdate();

  UnindexOrder(client_id, client_order_id);
  Levels(side).RemoveOrder(order);
}

std::string BookCore::toString([[maybe_unused]] bool detailed,
                               [[maybe_unused]] bool validityCheck) const {
  // TODO: 实现订单簿状态导出。
  return "";
}

Quantity BookCore::Match(TakerOrder& taker) noexcept {
  auto& maker_levels = OppositeLevels(taker.side);
  const bool taker_buys = (taker.side == Side::BUY);
  // 被成交一侧的方向在整个撮合过程中恒定：即进攻方的对手方向。
  const Side maker_side = taker_buys ? Side::SELL : Side::BUY;

  // 外层循环：价格层级，最优优先。内层循环：该层级的 FIFO。
  while (taker.quantity > 0) {
    // 最优 tick 由单侧订单簿即时维护，价位与价格都从它派生。
    const Tick maker_tick = maker_levels.BestTick();
    if (maker_tick == kInvalidTick) {
      break;
    }
    const auto& level = maker_levels.LevelAt(maker_tick);

    const auto maker_price = band_.ToPrice(maker_tick);
    const bool crosses = taker_buys ? (maker_price <= taker.price)
                                    : (maker_price >= taker.price);
    if (!crosses) {
      break;
    }

    while (taker.quantity > 0) {
      // 清空价位后槽位仍可寻址且为空，因此即使最后一个节点已归还内存池，
      // 重新读取队头也是安全的。
      auto* maker = level.first_order;
      if (maker == nullptr) {
        break;
      }

      const auto executed_quantity =
          std::min(taker.quantity, maker->remaining_quantity);
      taker.quantity -= executed_quantity;
      maker_levels.ApplyFill(maker, executed_quantity);

      client_response_ = {ClientResponseType::FILLED,
                          taker.client_id,
                          symbol_id_,
                          taker.client_order_id,
                          taker.market_order_id,
                          taker.side,
                          maker_price,
                          executed_quantity,
                          taker.quantity};
      SendClientResponse();

      client_response_ = {ClientResponseType::FILLED,
                          maker->client_id,
                          symbol_id_,
                          maker->client_order_id,
                          maker->market_order_id,
                          maker_side,
                          maker_price,
                          executed_quantity,
                          maker->remaining_quantity};
      SendClientResponse();

      market_update_ = {MarketUpdateType::TRADE,
                        symbol_id_,
                        maker->market_order_id,
                        maker_side,
                        maker_price,
                        executed_quantity,
                        Priority_INVALID};
      SendMarketUpdate();

      if (maker->remaining_quantity == 0) {
        market_update_ = {MarketUpdateType::CANCEL,
                          symbol_id_,
                          maker->market_order_id,
                          maker_side,
                          maker_price,
                          Quantity_INVALID,
                          Priority_INVALID};
        SendMarketUpdate();

        UnindexOrder(maker->client_id, maker->client_order_id);
        maker_levels.RemoveOrder(maker);
      } else {
        market_update_ = {MarketUpdateType::MODIFY,
                          symbol_id_,
                          maker->market_order_id,
                          maker_side,
                          maker_price,
                          maker->remaining_quantity,
                          maker->priority};
        SendMarketUpdate();
      }
    }
  }

  return taker.quantity;
}

OrderNode* BookCore::FindOrder(ClientId client_id,
                               OrderId client_order_id) const noexcept {
  const auto* table = orders_[client_id];
  return (table != nullptr) ? (*table)[client_order_id] : nullptr;
}

  // TODO: 这里可能会有尾延迟，我们稍后来优化。
void BookCore::IndexOrder(ClientId client_id, OrderId client_order_id,
                          OrderNode* order) noexcept {
  auto*& table = orders_[client_id];
  if (UNLIKELY(table == nullptr)) {
    // 该客户端在此标的上第一笔挂单。在这里分配（并清零）索引表是
    // 添加路径上仅剩的一次堆操作；改用更稠密的索引后即可消除。
    table = new OrderIdTable{};
  }
  (*table)[client_order_id] = order;
}

void BookCore::UnindexOrder(ClientId client_id,
                            OrderId client_order_id) noexcept {
  auto* table = orders_[client_id];
  if (LIKELY(table != nullptr)) {
    (*table)[client_order_id] = nullptr;
  }
}

void BookCore::SendMarketUpdate() noexcept {
  auto next_write = outgoing_market_updates_.GetNextToWriteTo();
  *next_write = market_update_;
  outgoing_market_updates_.UpdateWriteIndex();
}

void BookCore::SendClientResponse() noexcept {
  // 写入客户端响应队列
  auto next_write = outgoing_client_responses_.GetNextToWriteTo();
  *next_write = client_response_;
  outgoing_client_responses_.UpdateWriteIndex();
}

}  // namespace exchange
