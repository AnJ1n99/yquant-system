# COMMON UTILITIES

## OVERVIEW
Core infrastructure reused across exchange: logging, networking, threading, types, memory pools.

## STRUCTURE
```
common/
├── logging.h           # Async lock-free logger
├── types.h             # Core trading types (OrderId, TickerId, Side, etc.)
├── tcp_socket.h/cc     # TCP socket/client (64MB buffers)
├── tcp_server.h/cc     # TCP server with epoll
├── ringBuffer.h        # Lock-free queue template
├── macros.h            # LIKELY/UNLIKELY, ASSERT, FATAL
├── time_utils.h        # Nanos timestamp utilities
├── thread_utils.h      # Thread creation helpers
├── mcast_socket.h/cc   # UDP multicast for market data
├── socket_utils.h      # Socket address helpers
├── mem_pool.h          # Memory pool (unused?)
├── perf_utils.h        # Performance measurement macros
└── CMakeLists.txt      # Interface library (headers only)
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Logging | logging.h | LFQueue<LogElement>, async flush thread |
| TCP client | tcp_socket.h | 64MB buffers, sendAndRecv() loop |
| TCP server | tcp_server.h | epoll-based, accept loop |
| Types | types.h | OrderId, TickerId, ClientId, Price, Qty, Side |
| Lock-free queue | ringBuffer.h | LFQueue<T> template |

## CONVENTIONS
- All headers use `#pragma once`
- Constants use `constexpr` + INVALID sentinel values
- String conversion functions: `orderIdToString()`, `sideToString()`, etc.
- Logger uses template-based format: `log("%=% %", val1, val2)`
- NO dynamic allocation in logging path

## ANTI-PATTERNS (THIS MODULE)
- NO virtual functions
- NO exceptions in hot paths (use FATAL/ASSERT)
- NO dynamic strings in TCP buffers (use fixed 64MB)
- NO blocking operations in sendAndRecv() (should be non-blocking)

## NOTES
- TCPBufferSize = 64MB per socket
- LFQueue is header-only template
- Logger runs in background thread, queues LogElement union types
