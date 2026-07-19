#pragma once

#include "../../common/types.h"
#include <string>

using namespace Common;

namespace Exchange {

// 用于匹配引擎内部的订单结构，每一个 MEOrder 是链表中的一个节点
class MEOrder {
    friend class MatchingEngine;
    friend class MEOrderBook;
    friend class MeSideBook;
public:
    MEOrder() = default;

    MEOrder(ClientId client_id, OrderId client_order_id, OrderId market_order_id,
            SymbolId symbol_id, Side side, Price price, Qty qty_remain, Priority priority)
        : client_id(client_id)
        , client_order_id(client_order_id)
        , market_order_id(market_order_id)
        , symbol_id(symbol_id)
        , side(side)
        , price(price)
        , qty_remain(qty_remain)
        , priority(priority) {}

    auto toString() const -> std::string;

private:
    // 路由标识
    ClientId  client_id        = ClientId_INVALID;
    OrderId   client_order_id  = OrderId_INVALID;
    OrderId   market_order_id  = OrderId_INVALID;
    SymbolId  symbol_id        = SymbolId_INVALID;

    // 订单属性
    Side      side             = Side::INVALID;
    Price     price            = Price_INVALID;
    Qty       qty_remain       = Qty_INVALID;
    Priority  priority         = Priority_INVALID;

    // 链表指针（由 MeSideBook::addOrder() 设置）
    MEOrder*  next             = nullptr;
    MEOrder*  prev             = nullptr;
};

// 单个客户端的订单哈希表 （用于快速查找订单）  OrderId -> MEOrder.
typedef std::array<MEOrder*, ME_MAX_ORDER_IDS> OrderHashMap;
// 所有客户端的订单二维查找表（用于快速查找订单）  ClientId -> OrderId -> MEOrder
typedef std::array<OrderHashMap*, ME_MAX_NUM_CLIENTS> ClientOrderHashMap;

// 同一价格档位的订单列表，内部维护同价位的 FIFO 循环链表
class MEOrdersAtPrice {
    friend class MEOrderBook;
    friend class MeSideBook;
public:
    MEOrdersAtPrice() = default;

    MEOrdersAtPrice(Price price, MEOrder* first_order,
                    MEOrdersAtPrice* nextEntry, MEOrdersAtPrice* prevEntry)
        : price(price)
        , firstMeOrder(first_order)
        , next(nextEntry)
        , prev(prevEntry)
        , hash_next(nullptr) {}

    auto toString() const -> std::string {
        std::ostringstream oss;
        oss << "MEOrdersAtPrice"
            << " ["
            << "price:" << priceToString(price)
            << " firstOrder:" << (firstMeOrder ? "exists" : "nullptr")
            << "]";
        return oss.str();
    }

private:
    Price             price        = Price_INVALID;
    MEOrder*          firstMeOrder = nullptr;  // 该价位 FIFO 链表的头节点

    // 价格档位排序链表（按价格高低排序）
    MEOrdersAtPrice*  next         = nullptr;
    MEOrdersAtPrice*  prev         = nullptr;

    // 哈希碰撞链表（同一桶内的不同价格）
    MEOrdersAtPrice*  hash_next    = nullptr;
};

typedef std::array<MEOrdersAtPrice*, ME_MAX_PRICE_LEVELS> OrdersAtPriceHashMap;

} // namespace Exchange
