// 该订单薄是基于数组
// 订单簿维护着单个股票代码的所有活跃订单状态，并按价格层级组织，每个层级内采用先进先出
// (FIFO) 的排序方式。
// 它支持两种主要操作：add()新建订单（立即与对方的被动订单进行匹配）和cancel()移除现有订单。
// 该实现采用内存池进行 O(1) 的内存分配/释放，并维护多个索引以实现高效查找。
// 每一个股票symbol都有一个orderbook

//                                requirement
// 恒定的查找时间。操作包括:获取某个价格水平或价格水平之间的交易量。
// 快速添加/取消/执行操作,最好是O(1)时间复杂度。操作包括:下新订单、取消订单和匹配订单
// 快速更新。操作:替换订单。
// 查询最佳买价/卖价
// 遍历价格水平

// note : best ask is the lowest ask price
// note : best bid is the highest bid price
#pragma once

#include <array>
#include <string>

#include "../../common/logging.h"
#include "../../common/mem_pool.h"
#include "../../common/types.h"
#include "../market_data/market_update.h"
#include "../order_manager/client_response.h"
#include "matching_engine_order.h"
#include "matching_engine_side_book.h"

namespace exchange {

// 前向声明，避免循环依赖
class MatchingEngine;

class MatchingEngineOrderBook final {
 public:
  // 构造函数
  explicit MatchingEngineOrderBook(common::SymbolId symbolId,
                                   MatchingEngine* engine, common::Logger* log);

  // 析构函数
  ~MatchingEngineOrderBook();

  // 添加订单
  void add(common::ClientId clientId, common::OrderId orderId,
           common::SymbolId symbolId, common::Side side, common::Price price,
           common::Quantity quantity) noexcept;

  // 取消订单
  void cancel(common::ClientId clientId, common::OrderId orderId) noexcept;

  // 转换为字符串表示
  std::string toString(bool detailed, bool validityCheck) const;

  // 供 SnapshotSynthesizer 使用的访问器
  const MatchingEngineSideBook& getBidBook() const noexcept {
    return bid_book_;
  }
  const MatchingEngineSideBook& getAskBook() const noexcept {
    return ask_book_;
  }

  // 禁用默认构造函数、拷贝构造函数和赋值操作符
  MatchingEngineOrderBook() = delete;
  MatchingEngineOrderBook(const MatchingEngineOrderBook&) = delete;
  MatchingEngineOrderBook& operator=(const MatchingEngineOrderBook&) = delete;

  // 禁用移动构造函数和移动赋值操作符
  MatchingEngineOrderBook(MatchingEngineOrderBook&&) = delete;
  MatchingEngineOrderBook& operator=(MatchingEngineOrderBook&&) = delete;

 private:
  // 生成新的市场订单ID
  common::OrderId generateMarketOrderId() { return nextMarketOrderId++; }

  // 获取对应侧的 book
  MatchingEngineSideBook& getSideBook(common::Side side) noexcept {
    return (side == common::Side::BUY) ? bid_book_ : ask_book_;
  }

  // 获取对手方的 book
  MatchingEngineSideBook& getOppositeSideBook(common::Side side) noexcept {
    return (side == common::Side::BUY) ? ask_book_ : bid_book_;
  }

  // 尝试撮合新订单，返回剩余数量
  common::Quantity checkForMatch(common::ClientId clientId,
                                 common::OrderId clientOrderId,
                                 common::SymbolId symbolId, common::Side side,
                                 common::Price price, common::Quantity quantity,
                                 common::OrderId marketOrderId) noexcept;

  // 主动订单与对手方被动订单进行撮合，返回主动订单剩余数量
  common::Quantity match(MatchingEngineOrder* activeOrder) noexcept;

 private:
  // 交易标的代码
  common::SymbolId symbol;

  // 匹配引擎指针
  MatchingEngine* matchingEngine = nullptr;

  // 用于客户端和订单ID查找的两级哈希映射（用于cancel()操作）
  ClientOrderHashMap cidOidToOrder_;

  // 共享内存池
  common::MemPool<MatchingEngineOrdersAtPrice> ordersAtPricePool;
  common::MemPool<MatchingEngineOrder> order_pool_;

  // 买卖双侧独立 Book
  MatchingEngineSideBook bid_book_;
  MatchingEngineSideBook ask_book_;

  // 客户端响应对象
  MatchingEngineClientResponse clientResponse;
  // 市场更新对象
  MatchingEngineMarketUpdate marketUpdate;

  // 下一个市场订单ID，用于生成唯一的订单编号
  common::OrderId nextMarketOrderId = 1;

  // 时间字符串缓存
  std::string time_str_;
  // 日志记录器指针
  common::Logger* logger = nullptr;
};

// 使用数组来映射
using OrderBookHashMap =
    std::array<MatchingEngineOrderBook*, common::kMaxSymbols>;
}  // namespace exchange

// 每一个symbol都有一个orderbook
// orderbook 里面维护了所有的order
// orderbook 里面维护了所有的price level
// price level 里面维护了所有的order
// order 里面维护了所有的order detail
