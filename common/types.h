#pragma once

#include <cstdint>
#include <limits>
#include <sstream>
#include <array>

#include "macros.h"

namespace Common {
  // Constants used across the ecosystem to represent upper bounds on various containers.
  // Trading instruments / TickerIds from [0, ME_MAX_TICKERS].

  constexpr size_t ME_MAX_TICKERS = 8;

  // Maximum size of lock free queues used to transfer client requests, client responses and market updates between components.
  constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
  constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;

  
};