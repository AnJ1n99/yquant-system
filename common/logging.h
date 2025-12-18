#pragma once

#include "types.h"

namespace Common {
    // 用于记录日志的数据的无锁队列的最大大小
    constexpr size_t LOG_QUEUE_SIZE = 8* 1024 * 1024;

}