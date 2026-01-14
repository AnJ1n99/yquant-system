// 该订单薄是基于数组
// 订单簿维护着单个股票代码的所有活跃订单状态，并按价格层级组织，每个层级内采用先进先出 (FIFO) 的排序方式。
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

#include "me_order.h"
#include "../../common/mem_pool.h"
#include "../../common/types.h"
#include "../../common/logging.h"
#include "../order_manager/client_response.h"
#include "../market_data/market_update.h"

#include <array>
#include <unordered_map>
#include <vector>
#include <string>

namespace Exchange {

// 前向声明，避免循环依赖
class MatchingEngine;

class MEOrderBook final {
public:
    // 构造函数
    explicit MEOrderBook(SymbolId symbolId, MatchingEngine* engine, Logger* log);

    // 禁用默认构造函数、拷贝构造函数和赋值操作符
    MEOrderBook() = delete;
    MEOrderBook(const MEOrderBook&) = delete;
    MEOrderBook& operator=(const MEOrderBook&) = delete;

    // 禁用移动构造函数和移动赋值操作符
    MEOrderBook(MEOrderBook&&) = delete;
    MEOrderBook& operator=(MEOrderBook&&) = delete;

    // 析构函数
    ~MEOrderBook();

    // 添加订单
    void add(ClientId clientId, OrderId orderId, SymbolId symbolId, Side side, Price price, Qty qty) noexcept;

    // 取消订单
    bool cancel(ClientId clientId, OrderId orderId) noexcept;

    // 转换为字符串表示
    std::string toString(bool detailed, bool validityCheck) const;

private:
    // 交易标的代码
    SymbolId symbol;

    // 匹配引擎指针
    MatchingEngine *matchingEngine = nullptr;

    // 用于客户端和订单ID查找的两级哈希映射（用于cancel()操作）
    // 键为客户端ID，值为另一个映射，该映射的键为订单ID，值为订单指针
    ClientOrderHashMap cidOidToOrder_;

    // 价格档位内存池，用于高效分配MEOrdersAtPrice对象
    MemPool<MEOrder> ordersAtPricePool;

    // 指向最佳买卖价位的指针（市场深度顶部)
    // bids_by_price_ 指向最高买单价格档位
    // asks_by_price_ 指向最低卖单价格档位
    MEOrdersAtPrice* bids_by_price_;  // 最佳买单价格档位指针
    MEOrdersAtPrice* asks_by_price_;  // 最佳卖单价格档位指针

    // 从价格到订单集合的哈希映射，使用价格作为键
    // 存储在特定价格的所有活跃订单
    OrdersAtPriceHashMap price_orders_at_price_;

    // 订单内存池，用于高效分配MEOrder对象，避免频繁的堆分配/释放导致的碎片化
    MemPool<MEOrder> order_pool_;

    // 客户端响应对象
    MEClientResponse clientResponse;
    // 市场更新对象
    MEMarketUpdate marketUpdate;

    // 下一个市场订单ID，用于生成唯一的订单编号
    OrderId nextMarketOrderId = 1;

    // 时间字符串缓存
    std::string time_str_;
    // 日志记录器指针
    Logger* logger = nullptr;

private:
};

typedef std::array<MEOrderBook*, ME_MAX_SYMBOLS> OrderBookHashMap;
}
