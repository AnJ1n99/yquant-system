#pragma once

#include "perf_utils.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>


namespace Common {
    // Represent a nanosecond timestamp.
	typedef int64_t Nanos;

	// Convert between nanos, micros, millis and secs.
	constexpr Nanos NANOS_TO_MICROS = 1000;
	constexpr Nanos MICROS_TO_MILLIS = 1000;
	constexpr Nanos MILLIS_TO_SECS = 1000;
	constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;
	constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;

	// 获取当前时间
	inline auto getCurrentNanos() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
	}

	// 将当前时间戳格式化为易于阅读的字符串，用于日志记录和调试
	// 例如 "14:30:05.123456789" (UTC 时间)
	// 优化版本：使用预分配缓冲区避免内存分配，使用 UTC 避免时区查询开销
	inline void getCurrentTimeStr(std::string* timeStr) {
	    const auto now = std::chrono::system_clock::now().time_since_epoch();

	    // 直接从 epoch 计算秒和纳秒，避免精度损失
	    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now);
	    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs).count();

	    // 转换为 UTC 时间结构，避免时区查询开销
	    auto time_t_val = std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point(secs));
	    std::tm tm_val;
	    gmtime_r(&time_t_val, &tm_val);  // 线程安全的 UTC 转换

	    // 预分配固定大小缓冲区，避免 std::format 的动态内存分配
	    // 格式: "HH:MM:SS.nnnnnnnnn" = 21 字符 (包括 null terminator 需要 22)
	    timeStr->resize(21);
		// c++17以上返回无 const 版本
	    std::snprintf(timeStr->data(), 22, "%02d:%02d:%02d.%09lld",
	        tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, nanos);
	}

}
