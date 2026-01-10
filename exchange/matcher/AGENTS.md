# ORDER MATCHING ENGINE

## OVERVIEW
Core order book management and matching logic: price-time priority, order lifecycle, market data generation.

## STRUCTURE
```
matcher/
├── matching_engine.h/cc      # Main orchestrator
├── me_order_book.h/cc        # Order book interface
├── unordered_map_me_order_book.cc  # HashMap implementation
├── ordered_map_me_order_book.h     # TreeMap implementation (stub)
└── me_order.h/cc             # Individual order representation
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Add/Cancel orders | matching_engine.cc | Process MEClientRequest |
| Order book | me_order_book.h/cc | Bids/asks, price levels |
| Order tracking | me_order.h/cc | Order metadata |

## CONVENTIONS
- Order book implementations: unordered_map (fast) vs ordered_map (sorted)
- Price-time priority enforced by data structure
- MEClientRequest → MEClientResponse + market updates

## ANTI-PATTERNS (THIS MODULE)
- NO dynamic allocation during order matching
- NO virtual function calls in match loop
- NO locks in hot paths (use lock-free queues for I/O)

## NOTES
- MatchingEngine orchestrates order book + client responses + market data
- Two order book implementations for performance testing
