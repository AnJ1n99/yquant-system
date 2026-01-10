#include "me_order.h"
#include <string>

namespace Exchange {

    auto MEOrder::toString() const -> std::string{
        std::ostringstream oss;
        oss << "MEOrder"
            << " ["
            << "orderId:" << orderIdToString(order_id)
            << " clientId:" << clientIdToString(client_id)
            << " clientOrderId:" << orderIdToString(client_order_id)
            << " marketOrderId:" << orderIdToString(market_order_id)
            << " side:" << sideToString(side)
            << " price:" << priceToString(price)
            << " qty:" << qtyToString(qty_remain)
            << " priority:" << priorityToString(priority)
            << "]";
        return oss.str();
    }    

}
