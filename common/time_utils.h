#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "perf_utils.h"

namespace common {
// Represent a nanosecond timestamp.
typedef int64_t Nanos;

// Convert between nanos, micros, millis and secs.
constexpr Nanos NANOS_TO_MICROS = 1000;
constexpr Nanos MICROS_TO_MILLIS = 1000;
constexpr Nanos MILLIS_TO_SECS = 1000;
constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;
constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;

// Wall-clock nanoseconds since Unix epoch (for TTT / absolute timestamps).
inline Nanos GetCurrentNanos() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Format current UTC wall time for logging/debugging, e.g.
// "14:30:05.123456789". Pre-sized buffer avoids allocation; UTC avoids timezone
// lookup cost.
inline void GetCurrentTimeStr(std::string& time_str) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();

  // Seconds + nanos from epoch, avoiding precision loss via intermediate units.
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(now);
  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs).count();

  auto time_t_val = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::time_point(secs));
  std::tm tm_val;
  gmtime_r(&time_t_val, &tm_val);  // Thread-safe UTC conversion.

  // "HH:MM:SS.nnnnnnnnn" = 21 chars (+ null needs 22).
  time_str.resize(21);
  std::snprintf(time_str.data(), 22, "%02d:%02d:%02d.%09lld", tm_val.tm_hour,
                tm_val.tm_min, tm_val.tm_sec, static_cast<long long>(nanos));
}
}  // namespace common
