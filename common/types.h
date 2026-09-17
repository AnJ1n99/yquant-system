#pragma once

#include <cstddef>
#include <cstdint>

namespace common {
// Constants used across the ecosystem to represent upper bounds on various
// containers. Trading instruments / SymbolIds from [0, kMaxSymbols).

constexpr std::size_t kMaxSymbols = 8;

// Maximum size of lock free queues used to transfer client requests, client
// responses and market updates between components.
constexpr std::size_t kMaxClientUpdates = 256 * 1024;
constexpr std::size_t kMaxMarketUpdates = 256 * 1024;

// Maximum number of clients supported by the system.
constexpr std::size_t kMaxNumClients = 256;

// max number of orders per trading instrument
constexpr std::size_t kMaxOrderIds = 1024 * 1024;

// 单个订单簿一侧可寻址的价格刻度数：默认价格带的宽度，也是装配点在
// 连续实现与稀疏实现之间的选择阈值（见 MatchingEngine::MakePriceLevels）。
constexpr std::size_t kMaxPriceLevels = 16 * 1024;

// 布局决策假定的缓存行大小（x86-64 与 arm64）。
constexpr std::size_t kCacheLineBytes = 64;

using OrderId = uint64_t;

using SymbolId = uint32_t;

using ClientId = uint32_t;

using Price = int64_t;

// 价格网格上的位置，与绝对价格区分。
using Tick = std::int64_t;

using Quantity = uint32_t;

// 买卖方向
enum class Side : uint8_t { BUY = 1, SELL = 2 };

// 优先级 (通常使用纳秒时间戳实现价格-时间优先级)
using Priority = uint64_t;
}  // namespace common
