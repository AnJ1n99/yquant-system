# PROJECT KNOWLEDGE BASE

**Generated:** 2025-01-07
**Commit:** d3144cf
**Branch:** feature1.001

## OVERVIEW
C++20 high-frequency trading exchange server with TCP client connections, deterministic order matching, and UDP multicast market data.

## STRUCTURE
```
./
├── common/              # Core utilities: logging, TCP, thread utils, types
├── exchange/            # Exchange modules
│   ├── matcher/         # Order matching engine, order book management
│   └── order_manager/   # TCP client handling, FIFO sequencing
├── CMakeLists.txt       # Root CMake (C++20, Release/Debug)
├── main.cpp             # Simple entry point (placeholder)
└── exchange_main.cpp    # Actual exchange server entry point
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Entry points | exchange_main.cpp | Real server, main.cpp is placeholder |
| Client connection | exchange/order_manager/ | OrderManager, TCP, FIFOSequencer |
| Order matching | exchange/matcher/ | MatchingEngine, OrderBook |
| Core utilities | common/ | Logging, TCP, types, ring buffer |

## CODE MAP
| Symbol | Type | Location | Refs | Role |
|--------|------|----------|------|------|
| OrderManager | class | exchange/order_manager/order_manager.h | high | TCP client handler |
| MatchingEngine | class | exchange/matcher/matching_engine.h | high | Core order matcher |
| FIFOSequencer | class | exchange/order_manager/FIFOSequencer.h | medium | Deterministic ordering |
| LFQueue | template | common/ringBuffer.h | high | Lock-free queue |
| Logger | class | common/logging.h | high | Async logging |
| TCPSocket/TCPServer | class | common/tcp_socket.h | medium | Networking |

## CONVENTIONS
- `#pragma once` for all headers
- `#pragma pack(push, 1)` for network protocol structs
- `noexcept` on performance-critical functions
- Explicit `= delete` for copy/move constructors
- `constexpr` for compile-time constants
- `inline auto` for small utility functions
- `std::array` with fixed-size limits (ME_MAX_NUM_CLIENTS, etc.)

## ANTI-PATTERNS (THIS PROJECT)
- NO dynamic memory allocation in hot paths
- NO virtual functions in performance-critical code
- NO exceptions in production paths (use ASSERT/FATAL macros)
- NO string copies in networking (use fixed-size buffers)
- NO blocking I/O in main threads (use epoll/async)

## UNIQUE STYLES
- Network structs use tight packing (#pragma pack(1)) + INVALID sentinel values
- Logging uses format-style `log("%=% %", val1, val2)` for zero-allocation
- Order sequencing uses FIFOSequencer for determinism before matching
- Lock-free queues (LFQueue) for cross-thread communication
- Time in Nanos (uint64_t) from time_utils.h

## COMMANDS
```bash
# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

## NOTES
- C++20 required
- 64MB TCP buffers per socket (TCPBufferSize)
- Exchange uses separate process with global components for signal handling
- Client authentication via clientId + socket binding verification
- Sequence numbers required on all client requests
