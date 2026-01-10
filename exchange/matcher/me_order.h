#pragma once

#include "../../common/types.h"
#include <string>

using namespace Common;

namespace Exchange {

class MEOrder {
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
    OrderId order_id = OrderId_INVALID;
    ClientId client_id = ClientId_INVALID;
    OrderId client_order_id = OrderId_INVALID;
    OrderId market_order_id = OrderId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Qty qty_remain = Qty_INVALID;
    Priority priority = Priority_INVALID;

    MEOrder* next = nullptr;
    MEOrder* prev = nullptr;

    friend class MatchingEngine;
    friend class MEOrderBook;
};

} // namespace Exchange
