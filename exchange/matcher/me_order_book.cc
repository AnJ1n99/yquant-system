#include "me_order_book.h"
#include "matching_engine.h"
#include "../../common/perf_utils.h"

using namespace Common;

namespace Exchange {

	MEOrderBook::MEOrderBook(SymbolId symbolId, MatchingEngine* engine, Logger* log)
		: symbol(symbolId), matchingEngine(engine),
		  ordersAtPricePool(ME_MAX_ORDER_IDS), order_pool_(ME_MAX_ORDER_IDS),
		  bid_book_(Side::BUY, &order_pool_, &ordersAtPricePool),
		  ask_book_(Side::SELL, &order_pool_, &ordersAtPricePool),
		  logger(log) {
		cidOidToOrder_.fill(nullptr);
	}

	MEOrderBook::~MEOrderBook() {
		// 释放所有订单对象
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
		// bid_book_ 和 ask_book_ 的析构函数会自动清理价格档位
	}

	void MEOrderBook::add(ClientId clientId, OrderId clientOrderId, SymbolId symbolId,
	                      Side side, Price price, Qty qty) noexcept {
		const auto newMarketOrderId = generateMarketOrderId();

		// 发送订单接受确认响应
		clientResponse = {
			ClientResponseType::ACCEPTED,
			clientId,
			symbolId,
			clientOrderId,
			newMarketOrderId,
			side,
			price,
			Qty_INVALID,
			qty
		};
		matchingEngine->sendClientResponse(&clientResponse);

		// 尝试与对手方被动订单进行撮合
		START_MEASURE(Exchange_MEOrderBook_checkForMatch);
		const auto leavesQty = checkForMatch(clientId, clientOrderId, symbolId,
		                                     side, price, qty, newMarketOrderId);
		END_MEASURE(Exchange_MEOrderBook_checkForMatch, (*logger));

		// 若有剩余数量，将订单加入订单簿（成为被动订单）
		if (LIKELY(leavesQty)) {
			auto& sideBook = getSideBook(side);
			const auto priority = sideBook.getNextPriority(price);

			auto order = order_pool_.allocate(
				clientId,
				clientOrderId,
				newMarketOrderId,
				symbolId,
				side,
				price,
				leavesQty,
				priority
			);

			// cidOidToOrder_ 写入在 MEOrderBook 层，因为是共享资源
			auto& orderMap = cidOidToOrder_[clientId];
			if (UNLIKELY(orderMap == nullptr)) {
				orderMap = new OrderHashMap{};
			}
			(*orderMap)[clientOrderId] = order;

			// 委托给对应侧 book 添加到价格档位链表
			START_MEASURE(Exchange_MEOrderBook_addOrder);
			sideBook.addOrder(order);
			END_MEASURE(Exchange_MEOrderBook_addOrder, (*logger));

			// 广播市场更新消息：新增订单
			marketUpdate = {
				MarketUpdateType::ADD,
				symbolId,
				newMarketOrderId,
				side,
				price,
				leavesQty,
				priority
			};
			matchingEngine->sendMarketUpdate(&marketUpdate);
		}
	}

	void MEOrderBook::cancel(ClientId clientId, OrderId orderId) noexcept {
		// 1. 从 cidOidToOrder_ 查找
		auto* orderMap = cidOidToOrder_[clientId];
		if (orderMap == nullptr || (*orderMap)[orderId] == nullptr) {
			// 发送 CANCEL_REJECTED
			clientResponse = {
				ClientResponseType::CANCEL_REJECTED,
				clientId,
				symbol,
				orderId,
				OrderId_INVALID,
				Side::INVALID,
				Price_INVALID,
				Qty_INVALID,
				Qty_INVALID
			};
			matchingEngine->sendClientResponse(&clientResponse);
			return;
		}

		auto* order = (*orderMap)[orderId];

		// 2. 根据 order->side 路由到对应 book 移除
		auto& sideBook = getSideBook(order->side);

		// 3. 发送 CANCELED 响应
		clientResponse = {
			ClientResponseType::CANCELED,
			clientId,
			symbol,
			orderId,
			order->market_order_id,
			order->side,
			order->price,
			Qty_INVALID,
			order->qty_remain
		};
		matchingEngine->sendClientResponse(&clientResponse);

		// 4. 发送 CANCEL 市场更新
		marketUpdate = {
			MarketUpdateType::CANCEL,
			symbol,
			order->market_order_id,
			order->side,
			order->price,
			Qty_INVALID,
			Priority_INVALID
		};
		matchingEngine->sendMarketUpdate(&marketUpdate);

		// 5. 从 cidOidToOrder_ 移除
		(*orderMap)[orderId] = nullptr;

		// 6. 从 side book 移除（内部会从链表移除 + 释放到内存池）
		sideBook.removeOrder(order);
	}

	std::string MEOrderBook::toString([[maybe_unused]] bool detailed, [[maybe_unused]] bool validityCheck) const {
		// TODO: 实现订单簿状态字符串表示
		return "";
	}

	Qty MEOrderBook::checkForMatch(ClientId clientId, OrderId clientOrderId, SymbolId symbolId,
	                               Side side, Price price, Qty qty, OrderId marketOrderId) noexcept {
		// 在栈上构造主动订单对象（不入簿，仅用于撮合）
		MEOrder activeOrder(
			clientId,
			clientOrderId,
			marketOrderId,
			symbolId,
			side,
			price,
			qty,
			Priority_INVALID
		);

		return match(&activeOrder);
	}

	// 主动订单与对手方被动订单进行撮合
	// 修复：使用 while + nullptr 检查替代 do-while + startOrder 模式，避免悬空指针
	Qty MEOrderBook::match(MEOrder* activeOrder) noexcept {
		auto leavesQty = activeOrder->qty_remain;
		const auto isBuy = (activeOrder->side == Side::BUY);
		auto& passiveBook = getOppositeSideBook(activeOrder->side);

		// 逐层撮合循环
		while (leavesQty > 0 && !passiveBook.isEmpty()) {
			auto* bestPriceLevel = passiveBook.getBestPrice();

			// 检查价格是否可成交
			const auto canMatch = isBuy
				? (bestPriceLevel->price <= activeOrder->price)
				: (bestPriceLevel->price >= activeOrder->price);
			if (!canMatch) break;

			// 逐个匹配该价位的订单
			auto* passiveOrder = bestPriceLevel->firstMeOrder;

			while (passiveOrder != nullptr && leavesQty > 0) {
				const auto execQty = std::min(leavesQty, passiveOrder->qty_remain);

				leavesQty -= execQty;
				passiveOrder->qty_remain -= execQty;

				// 发送主动方成交回报
				clientResponse = {
					ClientResponseType::FILLED,
					activeOrder->client_id,
					symbol,
					activeOrder->client_order_id,
					activeOrder->market_order_id,
					activeOrder->side,
					passiveOrder->price,
					execQty,
					leavesQty
				};
				matchingEngine->sendClientResponse(&clientResponse);

				// 发送被动方成交回报
				clientResponse = {
					ClientResponseType::FILLED,
					passiveOrder->client_id,
					symbol,
					passiveOrder->client_order_id,
					passiveOrder->market_order_id,
					passiveOrder->side,
					passiveOrder->price,
					execQty,
					passiveOrder->qty_remain
				};
				matchingEngine->sendClientResponse(&clientResponse);

				// 发送市场更新：成交事件
				marketUpdate = {
					MarketUpdateType::TRADE,
					symbol,
					passiveOrder->market_order_id,
					passiveOrder->side,
					passiveOrder->price,
					execQty,
					Priority_INVALID
				};
				matchingEngine->sendMarketUpdate(&marketUpdate);

				// 保存下一个订单指针和是否为最后一个订单
				auto* nextOrder = passiveOrder->next;
				bool isLastInLevel = (nextOrder == passiveOrder);

				if (passiveOrder->qty_remain == 0) {
					// 发送市场更新：订单完成
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

					// 从 cidOidToOrder_ 移除
					auto& orderMap = cidOidToOrder_[passiveOrder->client_id];
					if (orderMap != nullptr) {
						(*orderMap)[passiveOrder->client_order_id] = nullptr;
					}

					// 从 side book 移除（内部处理链表移除 + 价位清除 + 内存释放）
					passiveBook.removeOrder(passiveOrder);
				} else {
					// 被动单部分成交
					marketUpdate = {
						MarketUpdateType::MODIFY,
						symbol,
						passiveOrder->market_order_id,
						passiveOrder->side,
						passiveOrder->price,
						passiveOrder->qty_remain,
						passiveOrder->priority
					};
					matchingEngine->sendMarketUpdate(&marketUpdate);
				}

				// 如果是该价位最后一个订单，退出内层循环
				// 外层循环会重新检查 passiveBook.isEmpty() 和 getBestPrice()
				if (isLastInLevel) break;

				passiveOrder = nextOrder;
			}
		}

		return leavesQty;
	}

}
