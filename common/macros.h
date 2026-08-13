#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

//  添加分支预测宏
/*
    分支预测
*/
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Checks a condition and exits if it does not hold.
//
// A macro rather than a function so that `msg` is evaluated only on failure.
// Taking the message by const std::string& evaluated it on every call instead,
// including in release builds: any literal past the small-string limit, or any
// concatenation, then allocated on paths that must not allocate at all.
#define ASSERT(cond, msg)                             \
  do {                                                \
    if (UNLIKELY(!(cond))) {                          \
      std::cerr << "ASSERT : " << (msg) << std::endl; \
      exit(EXIT_FAILURE);                             \
    }                                                 \
  } while (false)

inline auto FATAL(const std::string& msg) noexcept {
  std::cerr << "FATAL: " << msg << std::endl;
  exit(EXIT_FAILURE);
}