#pragma once

#include <cstdint>
namespace common {
// 读取 CPU 时间戳计数器（TSC）
/*
        精度：单个 CPU 周期粒度
        开销：每次测量通常需要20-30个CPU周期
        单位转换：使用CPU频率（2.60 GHz）将周期转换为纳秒 -> NanoSec =
   rdtsc_cycles / CPU_FREQ
*/
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
inline auto rdtsc() {
  unsigned int lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}
#elif defined(__aarch64__)
inline auto rdtsc() {
  uint64_t val;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}
#else
inline auto rdtsc() { return static_cast<uint64_t>(0); }
#endif

// 使用rdtsc（）启动延迟测量。在局部作用域中创建一个名为tga的变量。
#define START_MEASURE(TAG) const auto TAG = common::rdtsc();

// 使用rdtsc（）测量结束延迟。期望一个名为tgg的变量已经存在于局部作用域中。
#define END_MEASURE(TAG, LOGGER)                                \
  do {                                                          \
    auto end = common::rdtsc();                                 \
    common::GetCurrentTimeStr(time_str_);                       \
    LOGGER.log("% RDTSC " #TAG " %\n", time_str_, (end - TAG)); \
  } while (false)

// Log a current timestamp at the time this macro is invoked.
// 分析多个事件发生的先后顺序或绝对时间点（Time-To-Tick）
#define TTT_MEASURE(TAG, LOGGER)                      \
  do {                                                \
    const auto TAG = common::GetCurrentNanos();       \
    common::GetCurrentTimeStr(time_str_);             \
    LOGGER.log("% TTT " #TAG " %\n", time_str_, TAG); \
  } while (false)
}  // namespace common
