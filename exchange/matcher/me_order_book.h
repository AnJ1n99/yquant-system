// 该订单薄是基于数组
// 订单簿维护着单个股票代码的所有活跃订单状态，并按价格层级组织，每个层级内采用先进先出 (FIFO) 的排序方式。
// 它支持两种主要操作：add()新建订单（立即与对方的被动订单进行匹配）和cancel()移除现有订单。
// 该实现采用内存池进行 O(1) 的内存分配/释放，并维护多个索引以实现高效查找。

//                                requirement
// 恒定的查找时间。操作包括:获取某个价格水平或价格水平之间的交易量。
// 快速添加/取消/执行操作,最好是O(1)时间复杂度。操作包括:下新订单、取消订单和匹配订单
// 快速更新。操作:替换订单。
// 查询最佳买价/卖价
// 遍历价格水平

// note : best ask is the lowest ask price
// note : best bid is the highest bid price

#include "me_order.h"

#pragma once

namespace Exchange {

class MEOrderBook {
public:
		
};


}
