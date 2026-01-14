#pragma once

#include "../../common/types.h"
#include <string>

using namespace Common;

namespace Exchange {

// 用于匹配引擎内部的订单结构 每一个MEorder is a node
class MEOrder {
public:
    friend class MatchingEngine;
    friend class MEOrderBook;
public:
    MEOrder() = default;

    MEOrder(OrderId order_id, ClientId client_id, OrderId client_order_id,OrderId market_order_id,
            Side side, Price price, Qty qty, Priority priority,MEOrder *prev_order, MEOrder *next_order)
        : order_id(order_id)
        , client_id(client_id)
        , client_order_id(client_order_id)
        , market_order_id(market_order_id)
        , side(side)
        , price(price)
        , qty_remain(qty)
        , priority(priority)
        , prev(prev_order)
        , next(next_order) {
    }

    auto toString() const -> std::string;

private:
    OrderId             order_id         = OrderId_INVALID;
    ClientId            client_id        = ClientId_INVALID;
    OrderId             client_order_id  = OrderId_INVALID;
    OrderId             market_order_id  = OrderId_INVALID;
    Side                side             = Side::INVALID;
    Price               price            = Price_INVALID;
    Qty                 qty_remain       = Qty_INVALID;
    Priority            priority         = Priority_INVALID;

    MEOrder*            next             = nullptr;
    MEOrder*            prev             = nullptr;
};

// 单个客户端的订单哈希表
typedef std::array<MEOrder*, ME_MAX_ORDER_IDS> OrderHashMap;
// 所有客户端的订单二维查找表
typedef std::array<OrderHashMap*, ME_MAX_NUM_CLIENTS> ClientOrderHashMap;

// 同一个价格的订单薄列表 内部维护数个同价位的orders list
// ToDo:   totalValue 
class MEOrdersAtPrice {
public:
    friend class MEOrderBook;
public:
    MEOrdersAtPrice() = default;

    MEOrdersAtPrice(Side side, Price price, MEOrder* first_order,
                    MEOrdersAtPrice* nextEntry, MEOrdersAtPrice* prevEntry)
        : side(side)
        , price(price)
        , firstMeOrder(first_order)
        , next(nextEntry)
        , prev(prevEntry) {
    }

    auto toString() const -> std::string {
        std::ostringstream oss;
        oss << "MEOrdersAtPrice"
            << " ["
            << "side:" << sideToString(side)
            << " price:" << priceToString(price)
            << " firstOrder:" << (firstMeOrder ? "exists" : "nullptr")
            << "]";
        return oss.str();
    }

private:
    Side                side         = Side::INVALID;
    Price               price        = Price_INVALID;
    MEOrder*            firstMeOrder = nullptr; // dummy node

    MEOrdersAtPrice*    next         = nullptr;
    MEOrdersAtPrice*    prev         = nullptr;

};

typedef std::array<MEOrdersAtPrice *, ME_MAX_PRICE_LEVELS> OrdersAtPriceHashMap;
} // namespace Exchange
