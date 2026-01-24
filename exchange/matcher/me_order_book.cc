#include "me_order_book.h"
#include "matching_engine.h"
#include "../../common/perf_utils.h"



using namespace Common;

namespace Exchange {

	// 需要初始化 order_pool_ 和 ordersAtPricePool
	MEOrderBook::MEOrderBook(SymbolId symbolId, MatchingEngine* engine, Logger* log)
		: symbol(symbolId), matchingEngine(engine), logger(log),
		  ordersAtPricePool(ME_MAX_ORDER_IDS), order_pool_(ME_MAX_ORDER_IDS),
		  bids_by_price_(nullptr), asks_by_price_(nullptr) {
		// 初始化客户端订单哈希表为 nullptr（防止野指针）
		cidOidToOrder_.fill(nullptr);
		// 初始化买单价格档位哈希表为 nullptr
		bid_price_levels_.fill(nullptr);
		// 初始化卖单价格档位哈希表为 nullptr
		ask_price_levels_.fill(nullptr);
	}

	MEOrderBook::~MEOrderBook() {
		// 释放所有订单对象
		// cidOidToOrder_ 是 std::array<OrderHashMap*, ME_MAX_NUM_CLIENTS>
		// OrderHashMap 是 std::array<MEOrder*, ME_MAX_ORDER_IDS>
		for (size_t clientId = 0; clientId < cidOidToOrder_.size(); ++clientId) {
			OrderHashMap* orderMap = cidOidToOrder_[clientId];
			if (orderMap) {
				for (size_t orderId = 0; orderId < orderMap->size(); ++orderId) {
					MEOrder* order = (*orderMap)[orderId];
					if (order) {
						order_pool_.deallocate(order);
						(*orderMap)[orderId] = nullptr;
					}
				}
				delete orderMap;
				cidOidToOrder_[clientId] = nullptr;
			}
		}

		// 释放所有价格档位对象（处理哈希碰撞链表）
		// 分别清理买单和卖单价格档位哈希表
		for (size_t priceIdx = 0; priceIdx < bid_price_levels_.size(); ++priceIdx) {
			MEOrdersAtPrice* bucket = bid_price_levels_[priceIdx];
			while (bucket != nullptr) {
				MEOrdersAtPrice* next = bucket->hash_next;
				ordersAtPricePool.deallocate(bucket);
				bucket = next;
			}
			bid_price_levels_[priceIdx] = nullptr;
		}

		for (size_t priceIdx = 0; priceIdx < ask_price_levels_.size(); ++priceIdx) {
			MEOrdersAtPrice* bucket = ask_price_levels_[priceIdx];
			while (bucket != nullptr) {
				MEOrdersAtPrice* next = bucket->hash_next;
				ordersAtPricePool.deallocate(bucket);
				bucket = next;
			}
			ask_price_levels_[priceIdx] = nullptr;
		}

		bids_by_price_ = nullptr;
		asks_by_price_ = nullptr;
	}

	MEOrdersAtPrice* MEOrderBook::getOrdersAtPrice(Price price, Side side) const noexcept {
        const auto& levels = (side == Side::BUY) ? bid_price_levels_ : ask_price_levels_;
        auto* bucket = levels[priceToIndex(price)];
        
        // 遍历链表查找匹配的价格（处理哈希碰撞）
        while (bucket != nullptr) {
            if (bucket->price == price) {
                return bucket;
            }
            bucket = bucket->hash_next;
        }
        return nullptr;
    }

	///  处理流程：
	///   1. 生成唯一的市场订单ID
	///   2. 发送订单接受确认给客户端
	///   3. 尝试与对手方订单进行匹配（调用 checkAndExecute）
	///   4. 若有剩余数量，将订单加入订单簿并广播市场更新
	void MEOrderBook::add(ClientId clientId, OrderId clientOrderId, SymbolId symbolId, 
						  Side side, Price price, Qty qty) noexcept {
		// 生成唯一的市场订单ID
		const auto newMarketOrderId = generateMarketOrderId();

		// 构造并发送订单接受确认响应
		// 告知客户端订单已被交易所接受进入撮合流程
		clientResponse = {
			ClientResponseType::ACCEPTED,    // 响应类型：订单已接受
			clientId,                         // 客户端ID
			symbolId,                         // 交易标的ID
			clientOrderId,                    // 客户端订单ID
			newMarketOrderId,                 // 市场订单ID（交易所分配）
			side,                             // 买卖方向
			price,                            // 订单价格
			Qty_INVALID,                      // 成交数量（接受时为0/INVALID）
			qty                               // 剩余数量（初始等于下单数量）
		};
		matchingEngine->sendClientResponse(&clientResponse);

		// 尝试与对手方被动订单进行撮合
		// 返回撮合后的剩余数量（0表示全部成交）
		START_MEASURE(Exchange_MEOrderBook_checkForMatch);
		const auto leavesQty = checkForMatch(clientId, clientOrderId, symbolId, 
											   side, price, qty, newMarketOrderId);
		END_MEASURE(Exchange_MEOrderBook_checkForMatch, (*logger));

		// 若有剩余数量，将订单加入订单簿（成为被动订单）
		if (LIKELY(leavesQty)) {
			// 获取该价格档位的下一个优先级（用于FIFO排序）
			const auto priority = getNextPriority(price, side);

			// 从内存池分配并初始化订单对象
			auto order = order_pool_.allocate(
				clientOrderId,          // order_id（使用客户端订单ID）
				clientId,               // client_id
				clientOrderId,          // client_order_id
				newMarketOrderId,       // market_order_id
				side,                   // side
				price,                  // price
				leavesQty,              // qty_remain（剩余数量）
				priority,               // priority
				nullptr,                // prev
				nullptr                 // next
			);

			// 将订单添加到订单簿数据结构
			START_MEASURE(Exchange_MEOrderBook_addOrder);
			addOrder(order);
			END_MEASURE(Exchange_MEOrderBook_addOrder, (*logger));

			// 广播市场更新消息：新增订单
			marketUpdate = {
				MarketUpdateType::ADD,    // 更新类型：新增
				symbolId,                 // 交易标的ID
				newMarketOrderId,         // 订单ID
				side,                     // 买卖方向
				price,                    // 价格
				leavesQty,                // 数量
				priority                  // 优先级
			};
			matchingEngine->sendMarketUpdate(&marketUpdate);
		}
	}

	void MEOrderBook::cancel(ClientId clientId, OrderId orderId) noexcept {
		// TODO: 实现取消订单逻辑
	}

	std::string MEOrderBook::toString(bool detailed, bool validityCheck) const {
		// TODO: 实现订单簿状态字符串表示
		return "";
	}

	void MEOrderBook::addOrderAtPrice(MEOrdersAtPrice* ordersAtPrice) noexcept {
		// 根据买卖方向选择对应的哈希表和链表
		const auto isBid = (ordersAtPrice->side == Side::BUY);
		auto& priceLevels = isBid ? bid_price_levels_ : ask_price_levels_;
		auto& bestPrice = isBid ? bids_by_price_ : asks_by_price_;

		// 将价格档位加入哈希表（链地址法处理碰撞）
		const auto idx = priceToIndex(ordersAtPrice->price);
		ordersAtPrice->hash_next = priceLevels[idx];
		priceLevels[idx] = ordersAtPrice;

		// 如果是空链表，直接设为头节点
		if (UNLIKELY(bestPrice == nullptr)) {
			bestPrice = ordersAtPrice;
			ordersAtPrice->prev = ordersAtPrice->next = ordersAtPrice; // 自己是自己的前驱和后继
		} else {
			// 遍历链表找到合适的插入位置
			// 买单：价格从高到低排序 (best bid是最高价)
			// 卖单：价格从低到高排序 (best ask是最低价)
			auto target = bestPrice;
			auto shouldInsertBefore = [&]() {
				return isBid ? (ordersAtPrice->price > target->price)
				             : (ordersAtPrice->price < target->price);
			};

			// 如果新价格应该成为新的最佳价格
			if (shouldInsertBefore()) {
				target = bestPrice->prev;  // 插入到链表尾部（循环链表）
				bestPrice = ordersAtPrice;  // 更新最佳价格指针
			} else {
				// 从最佳价格开始遍历，找到第一个应该插在其前面的位置
				target = bestPrice->next;
				while (target != bestPrice) {
					if (shouldInsertBefore()) {
						break;
					}
					target = target->next;
				}
				target = target->prev;  // 插入到找到位置的前一个节点之后
			}

			// 插入到target之后
			ordersAtPrice->next = target->next;
			ordersAtPrice->prev = target;
			target->next->prev = ordersAtPrice;
			target->next = ordersAtPrice;
		}
	}

	void MEOrderBook::removeOrderAtPrice(Side side, Price price) noexcept {
		// 获取该价格档位
		auto ordersAtPrice = getOrdersAtPrice(price, side);

		// 如果价格档位不存在，直接返回（防御性检查）
		if (UNLIKELY(ordersAtPrice == nullptr)) {
			return;
		}

		// 根据买卖方向选择对应的哈希表和链表（使用引用避免重复计算）
		auto& priceLevels = (side == Side::BUY) ? bid_price_levels_ : ask_price_levels_;
		auto& bestPrice = (side == Side::BUY) ? bids_by_price_ : asks_by_price_;

		// 从双向循环链表中移除该节点（价格排序链表）
		if (UNLIKELY(ordersAtPrice->next == ordersAtPrice)) {
			// 链表中只有一个节点，直接清空最佳价格指针
			bestPrice = nullptr;
		} else {
			// 多节点情况：调整前后节点指针
			ordersAtPrice->prev->next = ordersAtPrice->next;
			ordersAtPrice->next->prev = ordersAtPrice->prev;

			// 如果移除的是最佳价格节点，更新为下一个节点
			if (ordersAtPrice == bestPrice) {
				bestPrice = ordersAtPrice->next;
			}
		}

		// 清空节点指针（防御性编程，避免悬空指针）
		ordersAtPrice->prev = ordersAtPrice->next = nullptr;

		// 从哈希表中移除（处理哈希碰撞链表）
		const auto idx = priceToIndex(price);
		auto* bucket = priceLevels[idx];
		MEOrdersAtPrice* prev_bucket = nullptr;

		while (bucket != nullptr) {
			if (bucket->price == price) {
				// 找到目标节点，从哈希链表中移除
				if (prev_bucket == nullptr) {
					// 是链表头节点
					priceLevels[idx] = bucket->hash_next;
				} else {
					// 是中间或尾节点
					prev_bucket->hash_next = bucket->hash_next;
				}
				bucket->hash_next = nullptr;
				break;
			}
			prev_bucket = bucket;
			bucket = bucket->hash_next;
		}

		// 释放该价格档位对象回内存池
		ordersAtPricePool.deallocate(ordersAtPrice);
	}

	void MEOrderBook::addOrder(MEOrder* order) noexcept {
		// 0. 价格验证（防御性检查，避免非法价格破坏哈希表）
		if (UNLIKELY(order->price == Price_INVALID || order->price < 0)) {
			// 高频路径不记录日志，直接返回（调用方应确保价格有效性）
			return;
		}

		// 1. 将订单添加到客户端-订单ID哈希表
		auto& orderMap = cidOidToOrder_[order->client_id];
		if (UNLIKELY(orderMap == nullptr)) {
			// 为该客户端首次创建订单映射表（使用 {} 确保所有指针初始化为 nullptr）
			orderMap = new OrderHashMap{};
		}
		(*orderMap)[order->client_order_id] = order;

		// 2. 获取或创建该价格档位（传递 Side 参数）
		auto ordersAtPrice = getOrdersAtPrice(order->price, order->side);

		if (ordersAtPrice == nullptr) {
			// 价格档位不存在，创建新的价格档位
			// firstMeOrder 是 dummy node，需要初始化为循环链表
			order->next = order;
			order->prev = order;

			ordersAtPrice = ordersAtPricePool.allocate(
				order->side,      // side
				order->price,     // price
				order,            // firstMeOrder (dummy node 指向第一个订单)
				nullptr,          // next
				nullptr           // prev
			);

			// 将新价格档位添加到价格链表中
			addOrderAtPrice(ordersAtPrice);
		} else {
			// 价格档位已存在，将订单添加到该价格档位的订单链表尾部（FIFO）
			auto firstOrder = ordersAtPrice->firstMeOrder;

			// 在循环链表尾部插入（即在 firstOrder 之前插入）
			order->prev = firstOrder->prev;
			order->next = firstOrder;
			firstOrder->prev->next = order;
			firstOrder->prev = order;
		}
	}

	// 尝试撮合新订单，构造临时主动订单对象并调用 match 进行撮合
	Qty MEOrderBook::checkForMatch(ClientId clientId, OrderId clientOrderId, SymbolId symbolId,
	                                Side side, Price price, Qty qty, OrderId marketOrderId) noexcept {
		// 在栈上构造主动订单对象（不入簿，仅用于撮合）
		MEOrder activeOrder(
			clientOrderId,    // order_id
			clientId,         // client_id
			clientOrderId,    // client_order_id
			marketOrderId,    // market_order_id
			side,             // side
			price,            // price
			qty,              // qty_remain（初始剩余数量）
			Priority_INVALID, // priority（主动单无需优先级）
			nullptr,          // prev
			nullptr           // next
		);

		// 调用 match 进行撮合，返回剩余数量
		return match(&activeOrder);
	}

	// 主动订单与对手方被动订单进行撮合
	// 会根据匹配结果更新被动订单，若完全匹配则可能移除该订单
	// 返回主动订单的剩余数量
	Qty MEOrderBook::match(MEOrder* activeOrder) noexcept {
		auto leavesQty = activeOrder->qty_remain;

		// 确定对手方：买单对卖单，卖单对买单
		const auto isBuy = (activeOrder->side == Side::BUY);
		auto& oppositeBestPrice = isBuy ? asks_by_price_ : bids_by_price_;

		// 逐层撮合循环，直到主动单完全成交或无法继续成交
		while (leavesQty > 0 && oppositeBestPrice != nullptr) {
			// 检查价格是否可成交
			const auto bestPrice = oppositeBestPrice->price;
			const auto canMatch = isBuy ? (bestPrice <= activeOrder->price)
			                            : (bestPrice >= activeOrder->price);

			if (!canMatch) {
				break;  // 价格不匹配，停止撮合
			}

			// FIFO 遍历该价位的订单链表（循环链表，firstMeOrder 是头节点）
			auto passiveOrder = oppositeBestPrice->firstMeOrder;
			auto startOrder = passiveOrder;  // 记录起点以检测循环结束

			do {
				// 计算成交数量：主动剩余 vs 被动剩余 取较小值
				const auto execQty = std::min(leavesQty, passiveOrder->qty_remain);

				// 更新双方剩余数量
				leavesQty -= execQty;
				passiveOrder->qty_remain -= execQty;

				// 发送主动方成交回报（FILLED）
				clientResponse = {
					ClientResponseType::FILLED,
					activeOrder->client_id,
					symbol,
					activeOrder->client_order_id,
					activeOrder->market_order_id,
					activeOrder->side,
					passiveOrder->price,  // 成交价格为被动方价格
					execQty,              // 成交数量
					leavesQty             // 主动方剩余数量
				};
				matchingEngine->sendClientResponse(&clientResponse);

				// 发送被动方成交回报（FILLED）
				clientResponse = {
					ClientResponseType::FILLED,
					passiveOrder->client_id,
					symbol,
					passiveOrder->client_order_id,
					passiveOrder->market_order_id,
					passiveOrder->side,
					passiveOrder->price,
					execQty,
					passiveOrder->qty_remain  // 被动方剩余数量
				};
				matchingEngine->sendClientResponse(&clientResponse);

				// 发送市场更新：成交事件（TRADE）
				marketUpdate = {
					MarketUpdateType::TRADE,
					symbol,
					passiveOrder->market_order_id,
					passiveOrder->side,
					passiveOrder->price,
					execQty,
					Priority_INVALID  // 成交事件无优先级
				};
				matchingEngine->sendMarketUpdate(&marketUpdate);

				// 保存下一个订单指针（因为当前订单可能被移除）
				auto nextOrder = passiveOrder->next;

				// 被动单完全成交，从订单簿移除
				if (passiveOrder->qty_remain == 0) {
					// 发送市场更新：订单取消/完成（CANCEL）
					marketUpdate = {
						MarketUpdateType::CANCEL,
						symbol,
						passiveOrder->market_order_id,
						passiveOrder->side,
						passiveOrder->price,
						Qty_INVALID,
						Priority_INVALID
					};
					matchingEngine->sendMarketUpdate(&marketUpdate);

					// 从 cidOidToOrder_ 哈希表中移除
					auto& orderMap = cidOidToOrder_[passiveOrder->client_id];
					if (orderMap != nullptr) {
						(*orderMap)[passiveOrder->client_order_id] = nullptr;
					}

					// 从循环链表中移除节点
					if (passiveOrder->next == passiveOrder) {
						// 链表只有一个节点，清空价位
						oppositeBestPrice->firstMeOrder = nullptr;
					} else {
						// 调整链表指针
						passiveOrder->prev->next = passiveOrder->next;
						passiveOrder->next->prev = passiveOrder->prev;

						// 如果移除的是头节点，更新头指针
						if (oppositeBestPrice->firstMeOrder == passiveOrder) {
							oppositeBestPrice->firstMeOrder = passiveOrder->next;
						}
					}

					// 回收订单对象到内存池
					order_pool_.deallocate(passiveOrder);
				} else {
					// 被动单部分成交，发送市场更新：订单修改（MODIFY）
					marketUpdate = {
						MarketUpdateType::MODIFY,
						symbol,
						passiveOrder->market_order_id,
						passiveOrder->side,
						passiveOrder->price,
						passiveOrder->qty_remain,  // 更新后的剩余数量
						passiveOrder->priority
					};
					matchingEngine->sendMarketUpdate(&marketUpdate);
				}

				// 主动单完全成交，退出循环
				if (leavesQty == 0) {
					break;
				}

				// 移动到下一个被动订单（FIFO）
				passiveOrder = nextOrder;

			} while (passiveOrder != startOrder && oppositeBestPrice->firstMeOrder != nullptr);

			// 如果该价位所有订单被清空，移除价位
			if (oppositeBestPrice->firstMeOrder == nullptr) {
				removeOrderAtPrice(oppositeBestPrice->side, oppositeBestPrice->price);
				// oppositeBestPrice 已被更新为下一个价位（在 removeOrderAtPrice 中）
			}
		}

		return leavesQty;
	}
	
}