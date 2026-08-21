// 基于数组的单标的订单簿。
//
// 订单簿按价格层级维护某标的的所有活跃订单，层级内按 FIFO 排序。
// 它支持 Add()（与对手方挂单撮合，剩余部分转为挂单）和 Cancel()
// （移除挂单）。买卖两侧都以 tick 寻址价位，因此订单簿需要快速完成
// 的操作全部是指针运算：
//
//   - 最优买价 / 最优卖价：         缓存的 tick，一次索引加载
//   - 某价格上的成交量：             价位聚合值，无需遍历链表
//   - 添加 / 撤单 / 成交：           O(1)，除节点池外无任何分配
//   - 从最优到最差遍历价位：         占用位图，不访问空价位
//
// 注意：best ask 是最低的卖价
// 注意：best bid 是最高的买价
#pragma once

#include <array>
#include <string>

#include "../../common/logging.h"
#include "../../common/mem_pool.h"
#include "../../common/types.h"
#include "../market_data/market_update.h"
#include "../order_manager/client_response.h"
#include "price_levels.h"

namespace exchange {

// 前向声明，避免循环包含。
class MatchingEngine;

class BookCore final {
 public:
  BookCore(common::SymbolId symbol_id, const PriceBand& band,
           MatchingEngine* engine, common::Logger* logger);

  ~BookCore();

  // 接受一笔新订单：回报它，与对手方挂单撮合，剩余部分转为挂单。
  // 标的即本订单簿自身的标的，调用方已据此路由到此。
  void Add(common::ClientId client_id, common::OrderId client_order_id,
           common::Side side, common::Price price,
           common::Quantity quantity) noexcept;

  // 移除一笔挂单；若订单号未知则回报 CANCEL_REJECTED。
  void Cancel(common::ClientId client_id,
              common::OrderId client_order_id) noexcept;

  std::string toString(bool detailed, bool validityCheck) const;

  // 供 SnapshotSynthesizer 使用的只读访问器。
  const PriceLevels& Bids() const noexcept { return bids_; }
  const PriceLevels& Asks() const noexcept { return asks_; }
  const PriceBand& Band() const noexcept { return band_; }

  BookCore() = delete;
  BookCore(const BookCore&) = delete;
  BookCore& operator=(const BookCore&) = delete;
  BookCore(BookCore&&) = delete;
  BookCore& operator=(BookCore&&) = delete;

 private:
  common::OrderId NextMarketOrderId() noexcept {
    return next_market_order_id_++;
  }

  PriceLevels& Levels(common::Side side) noexcept {
    return (side == common::Side::BUY) ? bids_ : asks_;
  }
  PriceLevels& OppositeLevels(common::Side side) noexcept {
    return (side == common::Side::BUY) ? asks_ : bids_;
  }

  // 让 taker 与对手方挂单撮合并回报每一笔成交。
  // 返回 taker 的剩余数量，也就是将要转为挂单的部分。
  common::Quantity Match(TakerOrder& taker) noexcept;

  // 订单号索引。超出可寻址范围的订单号会被拒绝而非索引：这些表是
  // 直接索引数组，未校验的订单号会越界读写。
  static bool IsIndexable(common::ClientId client_id,
                          common::OrderId client_order_id) noexcept {
    return client_id < common::kMaxNumClients &&
           client_order_id < common::kMaxOrderIds;
  }
  OrderNode* FindOrder(common::ClientId client_id,
                       common::OrderId client_order_id) const noexcept;
  void IndexOrder(common::ClientId client_id, common::OrderId client_order_id,
                  OrderNode* order) noexcept;
  void UnindexOrder(common::ClientId client_id,
                    common::OrderId client_order_id) noexcept;

  common::SymbolId symbol_id_;
  PriceBand band_;
  MatchingEngine* matching_engine_ = nullptr;

  // 买卖两侧的挂单节点。每个订单簿一个内存池：节点的生命周期即
  // 订单簿的生命周期，无论它挂在哪一侧。
  common::MemPool<OrderNode> order_pool_;

  PriceLevels bids_;
  PriceLevels asks_;

  // (client_id, client_order_id) -> 挂单，供 Cancel() 使用。
  ClientOrderIdTable orders_;

  // 出站消息的复用缓冲，保证回报路径不产生分配。
  MatchingEngineClientResponse client_response_;
  MatchingEngineMarketUpdate market_update_;

  common::OrderId next_market_order_id_ = 1;

  std::string time_str_;
  common::Logger* logger_ = nullptr;
};

// 标的 -> 订单簿。
// TODO: 是否需要改为共享指针来管理呢？
using OrderBookHashMap = std::array<BookCore*, common::kMaxSymbols>;
}  // namespace exchange
