#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <thread>

#include "macros.h"
#include "ringBuffer.h"
#include "thread_utils.h"
#include "time_utils.h"
#include "types.h"

// 日式输出格式 <timestamp> <type> <tag> <value>
// 14:23:45.123456 RDTSC T3_MatchingEngine_LFQueue_read 5200

namespace common {
// 用于记录日志的数据的无锁队列的最大大小
constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;  // 8MB 队列

// Type of logElement message
// 每种类型对应于`LogElement`联合体中的特定字段
enum class LogType : int8_t {
  CHAR = 0,
  INTEGER = 1,
  LONG_INTEGER = 2,
  LONG_LONG_INTEGER = 3,
  UNSIGNED_INTEGER = 4,
  UNSIGNED_LONG_INTEGER = 5,
  UNSIGNED_LONG_LONG_INTEGER = 6,
  FLOAT = 7,
  DOUBLE = 8,
  CONST_STRING = 9  // 常量字符串（字符串字面量），整个程序生命周期有效
};

struct LogElement {
  LogType type_ = LogType::CHAR;

  union {  // 编译器  避免虚拟分派开销  在无锁队列中高效存储
    char c;
    int i;
    long l;
    long long ll;
    unsigned u;
    unsigned long ul;
    unsigned long long ull;
    float f;
    double d;
    const char* str;  // 用于常量字符串指针
  } u_;
};

class Logger final {
 public:
  // Consumes from the lock free queue of log entries and writes to the output
  // log file. 消费无锁队列中的日志条目并写入输出日志文件
  auto flushQueue() noexcept {
    while (running_) {
      for (auto next = queue_.getNextToRead(); queue_.size() && next;
           next = queue_.getNextToRead()) {
        switch (next->type_) {
          case LogType::CHAR:
            file_ << next->u_.c;
            break;
          case LogType::INTEGER:
            file_ << next->u_.i;
            break;
          case LogType::LONG_INTEGER:
            file_ << next->u_.l;
            break;
          case LogType::LONG_LONG_INTEGER:
            file_ << next->u_.ll;
            break;
          case LogType::UNSIGNED_INTEGER:
            file_ << next->u_.u;
            break;
          case LogType::UNSIGNED_LONG_INTEGER:
            file_ << next->u_.ul;
            break;
          case LogType::UNSIGNED_LONG_LONG_INTEGER:
            file_ << next->u_.ull;
            break;
          case LogType::FLOAT:
            file_ << next->u_.f;
            break;
          case LogType::DOUBLE:
            file_ << next->u_.d;
            break;
          case LogType::CONST_STRING:
            file_ << next->u_.str;
            break;
        }
        queue_.updateReadIndex();
      }
      file_.flush();

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  explicit Logger(const std::string& fileName)
      : fileName_(fileName), queue_(LOG_QUEUE_SIZE) {
    file_.open(fileName);
    ASSERT(file_.is_open(), "Could not open log file:" + fileName);
    loggerThread_ = createAndStartThread(-1, "Common/Logger-" + fileName_,
                                         [this]() { flushQueue(); });
    ASSERT(loggerThread_ != nullptr, "Failed to start Logger thread.");
  }

  // 确保后台日志线程在对象销毁前完成所有待写入的日志，并正确释放资源
  ~Logger() {
    std::string time_str;
    GetCurrentTimeStr(time_str);
    std::cerr << time_str << " Flushing and closing Logger for " << fileName_
              << std::endl;

    while (queue_.size()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    running_ = false;
    loggerThread_->join();

    file_.close();
    GetCurrentTimeStr(time_str);
    std::cerr << time_str << " Logger for " << fileName_ << " exiting."
              << std::endl;
  }

  /// Overloaded methods to write different log entry types to the lock free
  /// queue. Creates a LogElement of the correct type and writes it to the lock
  /// free queue.
  auto pushValue(const LogElement& logElement) noexcept {
    *(queue_.getNextToWriteTo()) = logElement;
    queue_.updateWriteIndex();
  }

  auto pushValue(const char value) noexcept {
    pushValue(LogElement{LogType::CHAR, {.c = value}});
  }

  auto pushValue(const int value) noexcept {
    pushValue(LogElement{LogType::INTEGER, {.i = value}});
  }

  auto pushValue(const long value) noexcept {
    pushValue(LogElement{LogType::LONG_INTEGER, {.l = value}});
  }

  auto pushValue(const long long value) noexcept {
    pushValue(LogElement{LogType::LONG_LONG_INTEGER, {.ll = value}});
  }

  auto pushValue(const unsigned value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_INTEGER, {.u = value}});
  }

  auto pushValue(const unsigned long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_INTEGER, {.ul = value}});
  }

  auto pushValue(const unsigned long long value) noexcept {
    pushValue(LogElement{LogType::UNSIGNED_LONG_LONG_INTEGER, {.ull = value}});
  }

  auto pushValue(const float value) noexcept {
    pushValue(LogElement{LogType::FLOAT, {.f = value}});
  }

  auto pushValue(const double value) noexcept {
    pushValue(LogElement{LogType::DOUBLE, {.d = value}});
  }

  // For constant string literals (high performance, single queue operation)
  // 用于字符串字面量（高性能，单次队列操作）
  // WARNING: Only use with string literals or static const char*, NOT temporary
  // strings! !：仅用于字符串字面量或静态 const char*，不要用于临时字符串！
  auto pushConstString(const char* value) noexcept {
    pushValue(LogElement{LogType::CONST_STRING, {.str = value}});
  }

  // For dynamic strings (char-by-char, safe for temporary strings)
  // 用于动态字符串（逐字符写入，适用于临时字符串）
  auto pushValue(const char* value) noexcept {
    if (UNLIKELY(!*value)) return;  // 快速路径：空字符串

    while (*value) {
      pushValue(*value);
      ++value;
    }
  }

  auto pushValue(const std::string& value) noexcept {
    pushValue(value.c_str());
  }

  // Parse the format string, substitute % with the variable number of arguments
  // passed and write the string to the lock free queue.
  /*
   * 简单例子：
   - `log("px=% py=%", 1, 2)` 会输出 `px=1 py=2`
   - `log("100%% ok")` 会输出 `100% ok`
   - `log("x=%")`（无参）会报“missing arguments”
   - `log("x=", 1)` 会报“extra arguments”
   */
  template <typename T, typename... A>
  auto log(const char* s, const T& value, A... args) noexcept {
    while (*s) {
      if (*s == '%') {
        if (UNLIKELY(*(s + 1) == '%')) {  // to allow %% -> % escape character.
          ++s;
        } else {
          pushValue(value);  // // substitute % with the value specified in the
                             // arguments.
          log(s + 1, args...);
          return;
        }
      }
      pushValue(*s++);
    }
    FATAL("extra arguments provided to log()");
  }

  /// Overload for case where no substitution in the string is necessary.
  /// Note that this is overloading not specialization. gcc does not allow
  /// inline specializations.
  auto log(const char* s) noexcept {
    while (*s) {
      if (*s == '%') {
        if (UNLIKELY(*(s + 1) == '%')) {  // to allow %% -> % escape character.
          ++s;
        } else {
          FATAL("missing arguments to log()");
        }
      }
      pushValue(*s++);
    }
  }

  // Deleted default, copy & move constructors and assignment-operators.
  Logger() = delete;

  Logger(const Logger&) = delete;

  Logger(const Logger&&) = delete;

  Logger& operator=(const Logger&) = delete;

  Logger& operator=(const Logger&&) = delete;

 private:
  // File to which the log entries will be written.
  const std::string fileName_;
  std::ofstream file_;

  // Lock free queue of log elements from main logging thread to
  // background formatting and disk writer thread.
  LFQueue<LogElement> queue_;
  std::atomic<bool> running_ = {true};

  // background logging thread
  std::thread* loggerThread_ = nullptr;
};
}  // namespace common
