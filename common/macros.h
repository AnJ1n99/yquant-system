#pragma once

#include <cstring>
#include <iostream>

//  添加分支预测宏
/*
    分支预测
*/
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// TODO: Check condition and exit if not true.
inline auto ASSERT(const bool cond, const std::string& msg) noexcept {
  if (UNLIKELY(!cond)) {
    std::cerr << "ASSERT : " << msg << std::endl;

    exit(EXIT_FAILURE);
  }
}

inline auto FATAL(const std::string& msg) noexcept {
  std::cerr << "FATAL: " << msg << std::endl;
  exit(EXIT_FAILURE);
}