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

/// 某个标的的完整限价订单簿：买卖两侧各一个 PriceLevels，外加
/// (client_id, client_order_id) -> 挂单 的索引和出站消息复用缓冲。
/// 除节点池与按客户端稀疏分配的索引表外，请求路径上没有任何分配。
class BookCore final {
 public:
  /// 构造某标的的订单簿。
  ///
  /// @param symbol_id 本簿服务的标的；请求已按标的路由到此。
  /// @param band 该标的的价格带，必须已通过 PriceBand::IsValid()。
  /// @param client_responses 客户端回报输出队列，供 OrderManager 消费。
  /// @param market_updates 行情输出队列，供 MarketDataPublisher 消费。
  /// 两条队列必须比订单簿存活更久；共用队列的订单簿必须在同一生产线程调用。
  /// @param logger 日志器，记录出站消息及撮合计时。
  BookCore(common::SymbolId symbol_id, const PriceBand& band,
           ClientResponseLFQueue& client_responses,
           MatchingEngineMarketUpdateLFQueue& market_updates,
           common::Logger* logger);

  ~BookCore();

  /// 接受一笔新订单：回报 ACCEPTED，与对手方挂单撮合并逐笔回报成交，
  /// 剩余数量转为挂单并对市场可见。标的即本订单簿自身的标的，调用方
  /// 已据此路由到此。
  ///
  /// 调用方必须保证以下输入条件；本函数不负责非法输入的校验与拒单。
  ///
  /// @param client_id 下单客户端，必须小于 common::kMaxNumClients。
  /// @param client_order_id 客户端侧订单号，必须小于 common::kMaxOrderIds。
  /// @param side 方向，BUY 或 SELL。
  /// @param price 限价，必须落在价格带内且对齐 tick 边界。
  /// @param quantity 委托数量，必须为正且不为 common::Quantity_INVALID。
  void Add(common::ClientId client_id, common::OrderId client_order_id,
           common::Side side, common::Price price,
           common::Quantity quantity) noexcept;

  /// 移除一笔挂单：回报 CANCELED 并发出对应的行情更新；订单号可寻址
  /// 但不存在对应挂单时，回报 CANCEL_REJECTED。本函数不检查输入边界。
  ///
  /// @param client_id 请求撤单的客户端，必须小于 common::kMaxNumClients。
  /// @param client_order_id 客户端侧订单号，必须小于 common::kMaxOrderIds。
  void Cancel(common::ClientId client_id,
              common::OrderId client_order_id) noexcept;

  /// 导出订单簿状态。
  ///
  /// @param detailed 是否包含逐订单明细（尚未实现）。
  /// @param validityCheck 是否附带一致性自检（尚未实现）。
  /// @return 订单簿的可读描述；两个开关均未实现，当前恒为空串。
  std::string toString(bool detailed, bool validityCheck) const;

  /// 供 SnapshotSynthesizer 使用的只读访问器。
  ///
  /// @return 买侧单侧订单簿。
  const PriceLevels& Bids() const noexcept { return bids_; }

  /// 供 SnapshotSynthesizer 使用的只读访问器。
  ///
  /// @return 卖侧单侧订单簿。
  const PriceLevels& Asks() const noexcept { return asks_; }

  /// 供 SnapshotSynthesizer 使用的只读访问器。
  ///
  /// @return 本簿的价格带。
  const PriceBand& Band() const noexcept { return band_; }

  BookCore() = delete;
  BookCore(const BookCore&) = delete;
  BookCore& operator=(const BookCore&) = delete;
  BookCore(BookCore&&) = delete;
  BookCore& operator=(BookCore&&) = delete;

 private:
  // 将复用缓冲中的消息写入相应输出队列。
  void SendClientResponse() noexcept;
  void SendMarketUpdate() noexcept;

  /// @return 当前市场订单号，并将发号器推进一位。
  common::OrderId NextMarketOrderId() noexcept {
    return next_market_order_id_++;
  }

  /// @param side 方向。
  /// @return 该方向的单侧订单簿。
  PriceLevels& Levels(common::Side side) noexcept {
    return (side == common::Side::BUY) ? bids_ : asks_;
  }

  /// 让 taker 与对手方挂单撮合并回报每一笔成交。
  ///
  /// @tparam IsBid 进攻单是否为买单，必须与 taker.side 一致。
  /// @param taker 进攻单；其 quantity 在撮合中被逐笔消耗。
  /// @return taker 的剩余数量，也就是将要转为挂单的部分。
  template <bool IsBid>
  common::Quantity Match(TakerOrder& taker) noexcept;

  // 订单号索引。调用方必须保证客户端号和订单号在直接索引表的容量内。

  /// 按订单号索引查找一笔挂单。
  ///
  /// @param client_id 客户端号，必须小于 common::kMaxNumClients。
  /// @param client_order_id 客户端侧订单号，必须小于 common::kMaxOrderIds。
  /// @return 对应挂单节点；无索引或不存在对应挂单时为 null。
  OrderNode* FindOrder(common::ClientId client_id,
                       common::OrderId client_order_id) const noexcept;

  /// 把一笔新挂单写入 (client_id, client_order_id) 二级索引表。
  ///
  /// @param client_id 下单客户端。
  /// @param client_order_id 客户端侧订单号。
  /// @param order 新挂单节点，须已在簿内。
  void IndexOrder(common::ClientId client_id, common::OrderId client_order_id,
                  OrderNode* order) noexcept;

  /// 清除 (client_id, client_order_id) 的索引项；无索引时为无操作。
  ///
  /// @param client_id 下单客户端。
  /// @param client_order_id 客户端侧订单号。
  void UnindexOrder(common::ClientId client_id,
                    common::OrderId client_order_id) noexcept;

  // 本簿服务的标的；请求已按标的路由到此。
  common::SymbolId symbol_id_;
  // 价格带：本簿全部价格 <-> tick 转换的唯一来源。
  PriceBand band_;
  // 借用输出队列；所有标的共用同一个生产线程。
  ClientResponseLFQueue& outgoing_client_responses_;
  MatchingEngineMarketUpdateLFQueue& outgoing_market_updates_;

  // 买卖两侧的挂单节点。每个订单簿一个内存池：节点的生命周期即
  // 订单簿的生命周期，无论它挂在哪一侧。
  common::MemPool<OrderNode> order_pool_;

  PriceLevels bids_;  // 买侧：最高已占用 tick 最优。
  PriceLevels asks_;  // 卖侧：最低已占用 tick 最优。

  // (client_id, client_order_id) -> 挂单，供 Cancel() 使用。
  ClientOrderIdTable orders_;

  // 出站消息的复用缓冲，保证回报路径不产生分配。
  MatchingEngineClientResponse client_response_;
  MatchingEngineMarketUpdate market_update_;

  // 市场订单号发号器，从 1 起。
  common::OrderId next_market_order_id_ = 1;

  // 日志和计时复用的时间字符串缓冲。
  std::string time_str_;
  // 日志器，记录出站消息及撮合计时。
  common::Logger* logger_ = nullptr;
};

// 标的 -> 订单簿。
// TODO: 是否需要改为共享指针来管理呢？
using OrderBookHashMap = std::array<BookCore*, common::kMaxSymbols>;
}  // namespace exchange
