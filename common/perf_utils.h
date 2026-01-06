#pragma once

#include <cstdint>
namespace Common {
	// 读取 CPU 时间戳计数器（TSC）
	/*
		精度：单个 CPU 周期粒度
		开销：每次测量通常需要20-30个CPU周期
		单位转换：使用CPU频率（2.60 GHz）将周期转换为纳秒 -> NanoSec = rdtsc_cycles / CPU_FREQ
	*/
	inline auto rdtsc() {
		unsigned int lo, hi;
		__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
		return ((uint64_t)hi << 32) | lo;
	}

	// 使用rdtsc（）启动延迟测量。在局部作用域中创建一个名为tga的变量。
	#define START_MEASURE(TAG) const auto TAG = Common::rdtsc();

	// 使用rdtsc（）测量结束延迟。期望一个名为tgg的变量已经存在于局部作用域中。
	#define END_MEASURE(TAG, LOGGER)                \
		do {                                        \
			auto end = Common::rdtsc();             \
			Common::getCurrentTimeStr(time_str_);  \
			LOGGER.log("% RDTSC "#TAG" %\n", time_str_, (end - TAG)); \
		} while (false)

	// Log a current timestamp at the time this macro is invoked.
	// 分析多个事件发生的先后顺序或绝对时间点（Time-To-Tick）
	#define TTT_MEASURE(TAG, LOGGER)            \
		do {                                    \
			const auto TAG = Common::getCurrentNanos(); \
			Common::getCurrentTimeStr(time_str_);      \
			LOGGER.log("% TTT "#TAG" %\n", time_str_, TAG); \
		} while (false)
}
