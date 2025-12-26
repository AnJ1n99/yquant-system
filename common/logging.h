#pragma once

#include "types.h"
#include <cstdint>
#include <fstream>
#include <thread>

#include "ringBuffer.h"
#include "logging.h"
#include "time_utils.h"
#include "thread_utils.h"

namespace Common {
    // 用于记录日志的数据的无锁队列的最大大小
    constexpr size_t LOG_QUEUE_SIZE = 8* 1024 * 1024; // 256K

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
        DOUBLE = 8
    };

    struct LogElement {
        LogType type_ = LogType::CHAR;

        union { // 编译器  避免虚拟分派开销  在无锁队列中高效存储
            char c;
            int i;
            long l;
            long long ll;
            unsigned u;
            unsigned long ul;
            unsigned long long ull;
            float f;
            double d;
        } u_;
    };

    class Logger final {
    public:
        // Consumes from the lock free queue of log entries and writes to the output log file.
        // 消费无锁队列中的日志条目并写入输出日志文件
        auto flushQueue() noexcept {
            while (running_) {
                for (auto next = queue_.getNextToRead(); queue_.size() && next; next = queue_.getNextToRead()) {
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
                    }
                    queue_.upadteReadIndex();
                }
                file_.flush();

                using namespace std::literals::chrono_literals;
                std::this_thread::sleep_for(10ms);
            }
        }

        explicit Logger(const std::string &fileName) 
                : fileName_(fileName)
                , queue_(LOG_QUEUE_SIZE) {
            
            file_.open(fileName);
            ASSERT(file_.is_open(), "Could not open log file:" + fileName);
            loggerThread_ = creatAndStartThread(-1, "Common/Logger" + fileName_, [this](){ flushQueue();});
            ASSERT(loggerThread_ != nullptr, "Failed to start Logger thread.");
        }

        ~Logger() {
            std::string time_str;
            std::cerr << Common::getCurrentTimeStr(&time_str) << " Flushing and closing Logger for " << fileName_ << std::endl;

            while (queue_.size()) {
                using namespace std::literals::chrono_literals;
                std::this_thread::sleep_for(1s);
            }

            running_ = false;
            loggerThread_->join();

        }

private:
        // File to which the log entries will be written.
        const std::string fileName_;
        std::ofstream file_;

        // Lock free queue of log elements from main logging thread to
        // background formatting and disk writer thread.
        LFQueue<LogElement> queue_;
        std::atomic<bool> running_ = {true};

        // background logging thread
        std::thread *loggerThread_ = nullptr;
    };
}