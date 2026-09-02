#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../../common/mem_pool.h"
#include "../../common/types.h"

namespace exchange {

// ---------------------------------------------------------------------------
// 订单簿几何结构
//
// 订单簿在固定的 tick 网格上寻址价格，而不是对绝对价格做哈希：
//
//     price(tick) = base_price + tick * tick_size,   tick in [0, kTickCount)
//
// tick 直接索引价位数组，因此定位一个价位只需一次减法、一次除法
// 和一次边界检查：无需探测、无冲突链、也完全不需要分配价位。
// 数组顺序即价格顺序，这也省去了旧布局中手工维护的有序价位链表。
//
// 网格宽度是编译期常量，因此价位数组及其占用位图可以直接内联在
// 单侧订单簿中。只有价格带的位置（base_price）和步长（tick_size）
// 是按标的区分的。
// ---------------------------------------------------------------------------

// 网格上的位置，而非价格。使用有符号类型，便于用一次比较即可判断
// kInvalidTick。
using Tick = std::int32_t;

// 超出价格带范围、或不在 tick 边界上的价格。
constexpr Tick kInvalidTick = -1;

// 单个订单簿单侧可寻址的 tick 数量。
constexpr Tick kTickCount = static_cast<Tick>(common::kMaxPriceLevels);

/// 某个标的的价格带：tick 网格的原点与步长，负责绝对价格与网格位置的
/// 互转。是订单簿全部价格 <-> tick 转换的唯一来源。
struct PriceBand {
  common::Price base_price = 0;  // tick 0 处的绝对价格
  common::Price tick_size = 1;   // 最小价格增量，必须大于 0

  /// 将绝对价格映射为网格上的 tick。

  /// 低于价格带、超出最后一个 tick、或落在两个 tick 之间的价格会被拒绝，
  /// 而不会就近归入相邻价位：把订单悄悄移到客户从未报出的价格上，比直接拒绝更糟。
  
  /// @param price 待映射的绝对价格。
  /// @return 对应的网格位置；不可表示时为 kInvalidTick。
  constexpr Tick ToTick(common::Price price) const noexcept {
    const common::Price offset = price - base_price;
    if (offset < 0 || offset % tick_size != 0) {
      return kInvalidTick;
    }
    const common::Price tick = offset / tick_size;
    return (tick < kTickCount) ? static_cast<Tick>(tick) : kInvalidTick;
  }

  /// ToTick 的逆映射。

  /// @param tick 价格带内的网格位置，调用方保证其有效。
  /// @return 该网格位置对应的绝对价格。
  constexpr common::Price ToPrice(Tick tick) const noexcept {
    return base_price + (static_cast<common::Price>(tick) * tick_size);
  }

  /// 价格带能否支撑一个完整且不溢出的 tick 网格。
  ///
  /// 仅在订单簿构建时检查一次，绝不在请求路径上调用。
  ///
  /// @return tick_size 为正、base_price 非负、且网格顶端不越过
  ///         common::Price_INVALID 时为 true。
  constexpr bool IsValid() const noexcept {
    return tick_size > 0 && base_price >= 0 &&
           (common::Price_INVALID - base_price) / tick_size > kTickCount;
  }
};

// 在标的拥有各自的参考价格之前使用的价格带：价格即从零开始计数的
// 整数 tick。
constexpr PriceBand kDefaultPriceBand{.base_price = 0, .tick_size = 1};

struct FIFOLevel;

// ---------------------------------------------------------------------------
// 挂单（maker）订单：某个价位 FIFO 队列中的一个节点。
//
// 挂单只携带撮合与回报路径在其等待期间需要读取的字段。挂单存续期间
// 恒定不变的信息（标的、方向、价格）属于持有它的订单簿和价位；
// 只描述来单请求的信息（参见 TakerOrder）随请求一起消亡。
// 指向价位的反向指针是 id 索引本应逐订单号保存的内容，它让撤单成为
// 纯粹的指针操作：无需查价格、无需查方向。
//
// 按缓存行对齐，保证节点不会横跨两个缓存行；撮合与撤单路径每个节点
// 只访问一次，且地址由内存池按回收顺序（而非队列顺序）给出。
// ---------------------------------------------------------------------------
struct alignas(common::kCacheLineBytes) OrderNode {
  /// 调试输出节点身份与状态；仅用于日志，不在任何热路径上调用。
  auto toString() const -> std::string;

  // 所在价位内部的 FIFO 链接：队头（最早）-> 队尾（最新）。
  OrderNode* next = nullptr;
  OrderNode* prev = nullptr;

  // 该节点所在的价位；节点离开订单簿后为 null。
  FIFOLevel* level = nullptr;

  common::OrderId market_order_id = common::OrderId_INVALID;
  common::OrderId client_order_id = common::OrderId_INVALID;
  common::Priority priority = common::Priority_INVALID;  // 价位内的排序
  common::Quantity remaining_quantity = common::Quantity_INVALID;
  common::ClientId client_id = common::ClientId_INVALID;
};

static_assert(sizeof(OrderNode) == common::kCacheLineBytes,
              "A resting order should occupy exactly one cache line.");

// ---------------------------------------------------------------------------
// 价位：单个 tick 上挂单的 FIFO 队列。
//
// 价位是单侧订单簿数组中的槽位，既不分配也不释放：即使价位为空，
// 指向价位的指针在订单簿生命周期内依然有效。因此清空一个价位并非
// 所有权变更事件，这正是撮合循环中悬空价位隐患被消除的原因。
//
// 除 toString() 外，本结构的所有字段都只由 PriceLevels
// 写入：价位的聚合量、发号器与队列指针必须与占用位图保持一致，
// 因此两者定义在一起。
// 槽位不保存自己的 tick 与方向：tick 即数组下标，方向即持有它的
// PriceLevels 实例（IndexOf / Owns 随时能以纯指针运算取回两者）。
// 省下的 5 字节让价位正好是半条缓存行，且大小保持 2 的幂。
// ---------------------------------------------------------------------------
struct alignas(common::kCacheLineBytes / 2) FIFOLevel {
  /// 调试输出价位状态；仅用于日志，不在任何热路径上调用。
  auto toString() const -> std::string;

  OrderNode* first_order = nullptr;    // 队头，下一笔被成交
  OrderNode* last_order = nullptr;     // 队尾，最后到达
  common::Priority next_priority = 1;  // 下一个到达订单的排序
  // 该价位的挂单总量，由单侧订单簿同步维护，使深度查询为 O(1)。
  // 宽度大于 common::Quantity：它是求和值，而非单个订单。
  std::uint64_t total_quantity = 0;

  /// 该价位当前是否不持有任何挂单（队头为空即整队为空）。
  bool IsEmpty() const noexcept { return first_order == nullptr; }
};

static_assert(sizeof(FIFOLevel) == common::kCacheLineBytes / 2,
              "Two price levels should share one cache line.");

// ---------------------------------------------------------------------------
// PriceLevels：单标的订单簿一侧（BUY 或 SELL）的全部价位 —— 该侧的价位
// 数组、跟踪哪些价位持有订单的占用位图，以及缓存的最优 tick。
// 单个价位是 FIFOLevel；BookCore 每侧持有一个 PriceLevels 实例，并把同一个
// 订单池交给两侧使用。
// ---------------------------------------------------------------------------
class PriceLevels final {
 public:
  /// 构造单侧订单簿。价位是数组槽位而非对象，因此除入参校验外没有
  /// 任何逐槽位初始化：网格位置即下标，方向即本实例。
  ///
  /// @param side 该侧方向，必须为 BUY 或 SELL。
  /// @param order_pool 与对手侧共享的挂单节点池；由 BookCore 拥有，
  ///                   生命周期必须覆盖本对象。
  PriceLevels(common::Side side, common::MemPool<OrderNode>* order_pool);

  /// @return 本侧方向（BUY 或 SELL）。
  common::Side side() const noexcept { return side_; }

  /// 本侧当前是否不持有任何挂单。最优 tick 被即时维护，因此空订单簿
  /// 即是没有最优 tick 的订单簿。
  bool IsEmpty() const noexcept { return best_tick_ == kInvalidTick; }

  /// @return 缓存的最优 tick：买单为最高的已占用 tick，卖单为最低的；
  ///         本侧为空时为 kInvalidTick。
  Tick BestTick() const noexcept { return best_tick_; }

  /// 寻址价格带内的一个价位，无论其是否为空。
  ///
  /// @param tick 网格位置；调用方需先通过 PriceBand::ToTick 把价格
  ///             映射为 tick 并确认其有效。
  /// @return 该 tick 上的价位槽位；引用在本对象生命周期内始终有效。
  const FIFOLevel& LevelAt(Tick tick) const noexcept {
    return levels_[static_cast<std::size_t>(tick)];
  }

  /// 槽位的网格位置：一次指针减法加一次移位 —— 价位大小是 2 的幂。
  ///
  /// @param level 本侧持有的价位指针。
  /// @return 该价位所在的 tick。
  Tick IndexOf(const FIFOLevel* level) const noexcept {
    return static_cast<Tick>(level - levels_.data());
  }

  /// 该价位是否属于本侧。价位数组内联在订单簿中，边界即 this 加
  /// 编译期常量，因此判断只含比较、不含访存。
  ///
  /// @param level 任意价位指针。
  /// @return level 落在本侧槽数组范围内时为 true。
  bool Owns(const FIFOLevel* level) const noexcept {
    return level >= levels_.data() && level < levels_.data() + kTickCount;
  }

  /// 将一笔新挂单追加到 `tick` 处价位的 FIFO 队尾，并同步该价位的
  /// 聚合量、占用位图与缓存的最优 tick。
  ///
  /// @param tick 目标价位的网格位置，必须在 [0, kTickCount) 内。
  /// @param client_id 下单客户端。
  /// @param client_order_id 客户端侧订单号。
  /// @param market_order_id 撮合引擎分配的订单号。
  /// @param quantity 挂单数量，必须为正。
  /// @return 新分配的挂单节点；由本侧持有，直到 RemoveOrder() 归还。
  OrderNode* AddOrder(Tick tick, common::ClientId client_id,
                      common::OrderId client_order_id,
                      common::OrderId market_order_id,
                      common::Quantity quantity) noexcept;

  /// 对一笔挂单应用一笔部分成交：扣减其剩余量与所在价位的聚合量。
  /// 放在这里是为了保证价位聚合值永远不会偏离其订单之和。
  ///
  /// @param order 仍在簿内的挂单节点。
  /// @param quantity 本次成交数量，必须不大于 order->remaining_quantity。
  void ApplyFill(OrderNode* order, common::Quantity quantity) noexcept;

  /// 摘除一笔挂单（撤单或全部成交）：解除队列链接、归还节点给内存池，
  /// 并在价位因此清空时复位其聚合状态、清除占用位、必要时重算最优 tick。
  ///
  /// @param order 仍在簿内的挂单节点；调用后 `order` 变为悬空指针。
  void RemoveOrder(OrderNode* order) noexcept;

  // 单侧订单簿不可复制或移动：订单节点按地址反向引用其价位槽位。
  PriceLevels() = delete;
  PriceLevels(const PriceLevels&) = delete;
  PriceLevels& operator=(const PriceLevels&) = delete;
  PriceLevels(PriceLevels&&) = delete;
  PriceLevels& operator=(PriceLevels&&) = delete;

 private:
  // 占用位图：每个 tick 一个位，每个位图字另有一个汇总位。前一个最优
  // 价位被清空后，查找新的最优 tick 只需两次加载加一次前导/尾随零计数，
  // 而无需扫描空的价位。
  static constexpr std::size_t kBitsPerWord = 64;
  static constexpr std::size_t kOccupancyWords =
      static_cast<std::size_t>(kTickCount) / kBitsPerWord;
  static constexpr std::size_t kSummaryWords =
      (kOccupancyWords + kBitsPerWord - 1) / kBitsPerWord;

  static_assert(static_cast<std::size_t>(kTickCount) % kBitsPerWord == 0,
                "The tick count must fill whole occupancy words.");

  /// 在占用位图中标记一个 tick 为已占用，并同步其汇总位。
  void MarkOccupied(Tick tick) noexcept;

  /// 在占用位图中清除一个 tick；其所在字被清空时同步清除汇总位。
  void ClearOccupied(Tick tick) noexcept;

  /// 扫描位图查找最优的已占用 tick。
  ///
  /// @return 最优 tick；没有任何占用时为 kInvalidTick。
  Tick FindBestTick() const noexcept;

  /// 该侧的价格优先级：买单越高越好，卖单越低越好。
  ///
  /// @param candidate 候选 tick。
  /// @param current 现行 tick。
  /// @return candidate 在本侧优于 current 时为 true。
  bool IsBetter(Tick candidate, Tick current) const noexcept {
    return (side_ == common::Side::BUY) ? (candidate > current)
                                        : (candidate < current);
  }

  // 本侧方向，构造时确定，此后不变。
  common::Side side_;
  // 缓存的最优 tick；本侧为空时为 kInvalidTick。
  Tick best_tick_ = kInvalidTick;
  // 与对手侧共享的挂单节点池；不拥有，生命周期由 BookCore 提供。
  common::MemPool<OrderNode>* order_pool_;

  std::array<std::uint64_t, kSummaryWords> summary_{};
  std::array<std::uint64_t, kOccupancyWords> occupancy_{};
  std::array<FIFOLevel, kTickCount> levels_{};
};

// ---------------------------------------------------------------------------
// 进攻单（taker）订单：来单请求的进行中形态。
//
// 携带完整身份信息，无队列链接、无订单簿位置——它从不进入订单簿。
// 撮合消耗它的数量，只有剩余部分才转为挂单。将两者分开，正是挂单节点
// 能保持在一个缓存行内的原因：taker 的字段只为一次请求服务，而节点的
// 字段为整个挂单期间服务。
// ---------------------------------------------------------------------------
struct TakerOrder {
  common::ClientId client_id = common::ClientId_INVALID;
  common::OrderId client_order_id = common::OrderId_INVALID;
  common::OrderId market_order_id = common::OrderId_INVALID;
  common::Side side = common::Side::INVALID;
  common::Price price = common::Price_INVALID;           // 限价
  common::Quantity quantity = common::Quantity_INVALID;  // 尚未成交的数量
};

// ---------------------------------------------------------------------------
// 订单号索引：(client_id, client_order_id) -> 挂单。
//
// 两级都是直接索引数组，一次查找只需两次内存加载。第二级是稀疏表，
// 在客户端首笔挂单时按客户端分配；按 kMaxOrderIds 定容意味着每个活跃
// 客户端、每个标的占用 8 MiB，这是下一个值得缩减的目标（稠密的
// market order id 表，或对 id 对做开放寻址，都只需它的一小部分）。
// ---------------------------------------------------------------------------
using OrderIdTable = std::array<OrderNode*, common::kMaxOrderIds>;
using ClientOrderIdTable = std::array<OrderIdTable*, common::kMaxNumClients>;

}  // namespace exchange
