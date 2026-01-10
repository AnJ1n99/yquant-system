# ORDER MANAGER

## OVERVIEW
TCP client handler: accept connections, validate requests, enforce sequence ordering, route responses.

## STRUCTURE
```
order_manager/
├── order_manager.h/cc         # Main TCP handler
├── FIFOSequencer.h            # Deterministic request ordering
├── client_request.h           # MEClientRequest, OMClientRequest structs
├── client_response.h          # MEClientResponse, OMClientResponse structs
└── CMakeLists.txt             # Static libexchange
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Client connections | order_manager.h | OrderManager::run(), recvCallback() |
| Sequence validation | order_manager.h | cidNextExpSeqNum[] check |
| Request ordering | FIFOSequencer.h | Sort by rxTime → publish to LFQueue |
| Request/response structs | client_request.h, client_response.h | #pragma pack(push, 1) |

## CONVENTIONS
- All network structs packed: `#pragma pack(push, 1)`
- Client authentication: clientId → socket binding (cidTcpSocketMap[])
- Sequence numbers: expected vs received validation
- Send responses with seqNum first, then response body

## ANTI-PATTERNS (THIS MODULE)
- NO accepting requests from wrong socket (security check in recvCallback)
- NO processing out-of-order requests (reject wrong seqNum)
- NO skipping sequence validation (critical for determinism)
- NO dynamic allocation in request parsing (use fixed buffers)

## UNIQUE STYLES
- `run()` method runs in dedicated thread, processes outgoingResponses LFQueue
- `recvCallback()` validates socket binding + seqNum before forwarding
- `recvFinishedCallback()` triggers FIFOSequencer::sequenceAndPublish()
- Two-step validation: socket identity + sequence number

## NOTES
- ME_MAX_NUM_CLIENTS = 256 (types.h)
- ME_MAX_CLIENT_UPDATES = 256K (LFQueue size)
- OMClientRequest wraps MEClientRequest with seqNum
