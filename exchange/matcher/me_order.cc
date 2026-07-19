#include "me_order.h"
#include <string>

namespace Exchange {

    auto MEOrder::toString() const -> std::string {
        std::ostringstream oss;
        oss << "MEOrder"
            << " ["
            << "clientId:"      << clientIdToString(client_id)
            << " clientOId:"    << orderIdToString(client_order_id)
            << " marketOId:"    << orderIdToString(market_order_id)
            << " symbolId:"     << symbolIdToString(symbol_id)
            << " side:"         << sideToString(side)
            << " price:"        << priceToString(price)
            << " qty:"          << qtyToString(qty_remain)
            << " priority:"     << priorityToString(priority)
            << "]";
        return oss.str();
    }

}
