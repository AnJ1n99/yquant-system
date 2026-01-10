#include "matching_engine.h"
#include "../../common/logging.h"
#include <cstddef>

namespace Exchange {

MatchingEngine::MatchingEngine(ClientRequestLFQueue *clientRequests,ClientResponseLFQueue *outgoingResponses,
                              MEMarketUpdateLFqueue *outgoingUpdates)
    : client_requests(clientRequests)
    , outgoing_ogw_responses(outgoingResponses)
    , outgoing_md_updates(outgoingUpdates)
    , logger("MatchingEngine.log") {
    // Initialize the matching engine with the provided queues
    for (size_t i = 0; i < ticker_order_book.size(); ++i) {
        
    }
}

MatchingEngine::~MatchingEngine() {
    // Clean up resources if needed
    // The destructor should ensure proper cleanup of the matching engine
    
}

void MatchingEngine::start() {
    run = true;
    // Start the matching engine processing loop
}

void MatchingEngine::end() {
    run = false;
    // Stop the matching engine processing and perform cleanup if needed
}

} // namespace Exchange
