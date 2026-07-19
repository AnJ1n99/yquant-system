// MeSideBook 封装单侧（BUY 或 SELL）的订单簿逻辑
// 管理该侧的价格档位链表和订单链表
// MEOrderBook 持有两个 MeSideBook 实例（bid_book_ 和 ask_book_）
#pragma once

#include "me_order.h"
#include "../../common/mem_pool.h"
#include "../../common/types.h"

using namespace Common;

namespace Exchange {

class MeSideBook {
	friend class MEOrderBook;
public:
	explicit MeSideBook(Side side, MemPool<MEOrder>* orderPool,
	                    MemPool<MEOrdersAtPrice>* pricePool);

	~MeSideBook();

	// 只读访问器
	MEOrdersAtPrice* getBestPrice() const noexcept { return best_price_; }
	MEOrdersAtPrice* getOrdersAtPrice(Price price) const noexcept;
	Priority getNextPriority(Price price) const noexcept;
	bool isEmpty() const noexcept { return best_price_ == nullptr; }

	// 修改操作
	void addOrder(MEOrder* order) noexcept;
	void removeOrder(MEOrder* order) noexcept;

	// 禁用拷贝和移动
	MeSideBook(const MeSideBook&) = delete;
	MeSideBook& operator=(const MeSideBook&) = delete;
	MeSideBook(MeSideBook&&) = delete;
	MeSideBook& operator=(MeSideBook&&) = delete;

private:
	void addOrderAtPrice(MEOrdersAtPrice* ordersAtPrice) noexcept;
	void removeOrderAtPrice(Price price) noexcept;
	size_t priceToIndex(Price price) const noexcept;

	Side side_;
	MEOrdersAtPrice* best_price_ = nullptr;
	OrdersAtPriceHashMap price_levels_;
	MemPool<MEOrder>* order_pool_;
	MemPool<MEOrdersAtPrice>* price_pool_;
};

} // namespace Exchange
