#include <gtest/gtest.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <barrier>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "../common/ringBuffer.h"

namespace {

// 超时在进程层处理，不给队列热循环增加计时和共享状态访问。
void TimeoutHandler(int) {
  constexpr char message[] = "队列测试超时，终止进程\n";
  (void)write(STDERR_FILENO, message, sizeof(message) - 1);
  _exit(124);
}

class TestDeadline final : public ::testing::EmptyTestEventListener {
 public:
  void OnTestStart(const ::testing::TestInfo&) override { alarm(120); }
  void OnTestEnd(const ::testing::TestInfo&) override { alarm(0); }
};

auto PinThread(int cpu) -> bool {
  if (cpu < 0 || cpu >= CPU_SETSIZE) return false;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
}

// 恢复调用线程的 CPU 集合，避免一个基准影响下一项测试。
class CpuPair final {
 public:
  CpuPair() {
    valid_ = sched_getaffinity(0, sizeof(original_), &original_) == 0;
    if (!valid_) return;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET(cpu, &original_)) continue;
      if (first == -1)
        first = cpu;
      else {
        second = cpu;
        break;
      }
    }
  }
  ~CpuPair() {
    if (valid_) {
      EXPECT_EQ(sched_setaffinity(0, sizeof(original_), &original_), 0)
          << "恢复 CPU 集合失败";
    }
  }
  int first = -1;
  int second = -1;

 private:
  cpu_set_t original_{};
  bool valid_ = false;
};

auto NowNs() -> int64_t {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) std::abort();
  return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

void Push(common::LFQueue<int64_t>& queue, int64_t value) {
  *queue.GetNextToWriteTo() = value;
  queue.UpdateWriteIndex();
}

auto Pop(common::LFQueue<int64_t>& queue) -> int64_t {
  const int64_t* value;
  while (!(value = queue.GetNextToRead())) CPU_PAUSE();
  const auto result = *value;
  queue.UpdateReadIndex();
  return result;
}

}  // namespace

// ==================== 正确性测试 ====================

TEST(LFQueueCorrectness, BasicPushPop) {
  common::LFQueue<int> q(8);

  auto* slot = q.GetNextToWriteTo();
  *slot = 42;
  q.UpdateWriteIndex();

  auto* val = q.GetNextToRead();
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(*val, 42);
  q.UpdateReadIndex();

  EXPECT_EQ(q.GetNextToRead(), nullptr);
}

TEST(LFQueueCorrectness, FillToCapacity) {
  common::LFQueue<int> q(8);

  int pushed = 0;
  while (auto* slot = q.TryGetNextToWriteTo()) {
    *slot = pushed;
    q.UpdateWriteIndex();
    ++pushed;
  }
  EXPECT_EQ(pushed, 7);
  EXPECT_TRUE(q.IsFull());

  for (int i = 0; i < pushed; ++i) {
    auto* val = q.GetNextToRead();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, i);
    q.UpdateReadIndex();
  }
  EXPECT_EQ(q.GetNextToRead(), nullptr);
}

TEST(LFQueueCorrectness, Wraparound) {
  common::LFQueue<int> q(4);

  for (int round = 0; round < 10; ++round) {
    for (int i = 0; i < 3; ++i) {
      auto* slot = q.GetNextToWriteTo();
      *slot = round * 100 + i;
      q.UpdateWriteIndex();
    }
    EXPECT_TRUE(q.IsFull());
    for (int i = 0; i < 3; ++i) {
      auto* val = q.GetNextToRead();
      ASSERT_NE(val, nullptr);
      EXPECT_EQ(*val, round * 100 + i);
      q.UpdateReadIndex();
    }
    EXPECT_EQ(q.GetNextToRead(), nullptr);
  }
}

TEST(LFQueueCorrectness, ConcurrentSPSC) {
  constexpr int kCount = 1'000'000;
  common::LFQueue<int64_t> q(1024);

  std::vector<int64_t> received;
  received.reserve(kCount);

  std::thread producer([&] {
    for (int64_t i = 0; i < kCount; ++i) {
      auto* slot = q.GetNextToWriteTo();
      *slot = i;
      q.UpdateWriteIndex();
    }
  });

  std::thread consumer([&] {
    for (int i = 0; i < kCount; ++i) {
      const int64_t* val;
      while (!(val = q.GetNextToRead())) {
        CPU_PAUSE();
      }
      received.push_back(*val);
      q.UpdateReadIndex();
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(static_cast<int>(received.size()), kCount);
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(received[i], i) << "FIFO 乱序 at index " << i;
  }
}

TEST(LFQueueCorrectness, SizeTracking) {
  common::LFQueue<int> q(8);
  EXPECT_EQ(q.size(), 0);

  auto* s1 = q.GetNextToWriteTo();
  *s1 = 1;
  q.UpdateWriteIndex();
  EXPECT_EQ(q.size(), 1);

  auto* s2 = q.GetNextToWriteTo();
  *s2 = 2;
  q.UpdateWriteIndex();
  EXPECT_EQ(q.size(), 2);

  q.GetNextToRead();
  q.UpdateReadIndex();
  EXPECT_EQ(q.size(), 1);

  q.GetNextToRead();
  q.UpdateReadIndex();
  EXPECT_EQ(q.size(), 0);
}

TEST(LFQueueCorrectness, PowerOf2Rounding) {
  EXPECT_EQ(common::LFQueue<int>(3).capacity(), 4);
  EXPECT_EQ(common::LFQueue<int>(5).capacity(), 8);
  EXPECT_EQ(common::LFQueue<int>(8).capacity(), 8);
  EXPECT_EQ(common::LFQueue<int>(9).capacity(), 16);
  EXPECT_EQ(common::LFQueue<int>(16).capacity(), 16);
}

TEST(LFQueueCorrectness, CapacityVsUsable) {
  common::LFQueue<int> q(8);
  EXPECT_EQ(q.capacity(), 8);

  int pushed = 0;
  while (q.TryGetNextToWriteTo()) {
    auto* slot = q.TryGetNextToWriteTo();
    *slot = pushed++;
    q.UpdateWriteIndex();
  }
  EXPECT_EQ(pushed, 7) << "可用槽位应为 capacity - 1（哨兵位）";
}

// ==================== 对端进度缓存 ====================

// 生产者缓存显示满时必须重新加载真实读索引，否则会永久误报满。
TEST(LFQueueCacheProgress, ProducerReloadsAfterConsumerDrains) {
  common::LFQueue<int> q(4);

  for (int i = 0; i < 3; ++i) {
    auto* slot = q.TryGetNextToWriteTo();
    ASSERT_NE(slot, nullptr) << "第 " << i << " 个槽位应可写";
    *slot = i;
    q.UpdateWriteIndex();
  }
  ASSERT_EQ(q.TryGetNextToWriteTo(), nullptr) << "应已满";

  const auto* value = q.GetNextToRead();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 0);
  q.UpdateReadIndex();

  // 生产者的缓存仍是陈旧的满值，必须重新加载才能看到这个空位。
  auto* slot = q.TryGetNextToWriteTo();
  ASSERT_NE(slot, nullptr) << "消费者已出队一条，生产者应重新加载并看到空位";
  *slot = 99;
  q.UpdateWriteIndex();
  EXPECT_TRUE(q.IsFull());
}

// 消费者缓存显示空时必须重新加载真实写索引，否则会永久误报空。
TEST(LFQueueCacheProgress, ConsumerReloadsAfterProducerPublishes) {
  common::LFQueue<int> q(4);

  ASSERT_EQ(q.GetNextToRead(), nullptr) << "初始应为空";

  auto* slot = q.TryGetNextToWriteTo();
  ASSERT_NE(slot, nullptr);
  *slot = 7;
  q.UpdateWriteIndex();

  // 消费者的缓存仍是陈旧的空值，必须重新加载才能看到新数据。
  const auto* value = q.GetNextToRead();
  ASSERT_NE(value, nullptr) << "生产者已发布，消费者应重新加载并看到数据";
  EXPECT_EQ(*value, 7);
  q.UpdateReadIndex();
  EXPECT_EQ(q.GetNextToRead(), nullptr);
}

// 单条交错进出跨多圈回绕：两侧缓存持续陈旧，按掩码比较若有误
// 将在索引越过 capacity 倍数时误判。轮数取 capacity 的非整倍数。
TEST(LFQueueCacheProgress, StaleCacheAcrossManyWraps) {
  common::LFQueue<int> q(4);

  for (int i = 0; i < 101; ++i) {
    auto* slot = q.TryGetNextToWriteTo();
    ASSERT_NE(slot, nullptr) << "第 " << i << " 轮写入失败";
    *slot = i;
    q.UpdateWriteIndex();

    const auto* value = q.GetNextToRead();
    ASSERT_NE(value, nullptr) << "第 " << i << " 轮读取失败";
    EXPECT_EQ(*value, i) << "第 " << i << " 轮数据错位";
    q.UpdateReadIndex();

    ASSERT_EQ(q.size(), 0u) << "第 " << i << " 轮结束后应为空";
  }
}

// 反复填满再排空，强制两侧缓存在每个边界都重新加载。
TEST(LFQueueCacheProgress, RepeatedFillDrainCycles) {
  common::LFQueue<int> q(8);
  constexpr int kUsable = 7;

  for (int round = 0; round < 20; ++round) {
    int pushed = 0;
    while (auto* slot = q.TryGetNextToWriteTo()) {
      *slot = round * 1000 + pushed;
      q.UpdateWriteIndex();
      ++pushed;
    }
    ASSERT_EQ(pushed, kUsable) << "第 " << round << " 轮可写槽位数不对";
    ASSERT_TRUE(q.IsFull());

    for (int i = 0; i < kUsable; ++i) {
      const auto* value = q.GetNextToRead();
      ASSERT_NE(value, nullptr) << "第 " << round << " 轮第 " << i << " 条缺失";
      EXPECT_EQ(*value, round * 1000 + i);
      q.UpdateReadIndex();
    }
    ASSERT_EQ(q.GetNextToRead(), nullptr) << "第 " << round << " 轮末应为空";
  }
}

// ==================== 已知缺陷验证 ====================

TEST(LFQueueDefect, D4_Capacity1AlwaysFull) {
  common::LFQueue<int> q1(1);
  EXPECT_EQ(q1.TryGetNextToWriteTo(), nullptr) << "LFQueue(1) 应永远满";
  EXPECT_TRUE(q1.IsFull());

  common::LFQueue<int> q0(0);
  EXPECT_EQ(q0.TryGetNextToWriteTo(), nullptr) << "LFQueue(0) 应永远满";
}

// ==================== 性能基准 ====================

TEST(LFQueueBenchmark, Throughput) {
  constexpr int kRuns = 7;
  constexpr std::size_t kItems = 5'000'000;
  constexpr std::size_t kWarmup = 100'000;
  constexpr std::size_t kCapacity = 65536;
  CpuPair cpus;
  if (cpus.second < 0) GTEST_SKIP() << "性能测试需要两个允许使用的 CPU";
  std::vector<double> ops;
  ops.reserve(kRuns);

  for (int r = 0; r < kRuns; ++r) {
    common::LFQueue<int64_t> q(kCapacity);
    std::barrier sync(2);
    int64_t consumer_sum = 0;
    int64_t finished = 0;
    int64_t started = 0;
    bool consumer_pinned = false;
    const bool producer_pinned = PinThread(cpus.first);
    std::thread consumer([&] {
      consumer_pinned = PinThread(cpus.second);
      sync.arrive_and_wait();
      if (!consumer_pinned || !producer_pinned) return;
      for (std::size_t i = 0; i < kWarmup; ++i) (void)Pop(q);
      sync.arrive_and_wait();
      // started 在第三次同步释放消费者前写入。
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < kItems; ++i) consumer_sum += Pop(q);
      finished = NowNs();
    });
    sync.arrive_and_wait();
    if (!consumer_pinned || !producer_pinned) {
      consumer.join();
      GTEST_SKIP() << "性能测试绑核失败";
    }
    for (std::size_t i = 0; i < kWarmup; ++i) Push(q, 0);
    sync.arrive_and_wait();
    started = NowNs();
    sync.arrive_and_wait();
    for (std::size_t i = 0; i < kItems; ++i) Push(q, static_cast<int64_t>(i));
    consumer.join();
    const auto elapsed = finished - started;
    const int64_t expected = static_cast<int64_t>(kItems) * (kItems - 1) / 2;
    ASSERT_EQ(consumer_sum, expected) << "数据校验失败，轮次 " << r;
    ASSERT_GT(elapsed, 0);
    ops.push_back(static_cast<double>(kItems) / elapsed * 1e9);
  }
  // 所有测量完成后再打印，日志 I/O 不进入计时区间。
  for (int r = 0; r < kRuns; ++r) {
    std::printf(
        "原始成绩：吞吐，轮次=%d，CPU=%d,%d，槽位=%zu，预热=%zu，消息=%zu，条/"
        "秒=%.6f\n",
        r + 1, cpus.first, cpus.second, kCapacity, kWarmup, kItems, ops[r]);
  }
  std::sort(ops.begin(), ops.end());
  std::printf(
      "\n吞吐量（CPU=%d,%d，槽位=%zu，消息=%zu，轮数=%d）\n"
      "  最小/中位/最大：%.2f / %.2f / %.2f 百万条/秒\n"
      "  KVM 环境仅观察方向，不据此推断可靠的提升倍数。\n",
      cpus.first, cpus.second, kCapacity, kItems, kRuns, ops.front() / 1e6,
      ops[kRuns / 2] / 1e6, ops.back() / 1e6);
}

TEST(LFQueueBenchmark, PingPongRTT) {
  constexpr int kRuns = 7;
  constexpr std::size_t kRoundTrips = 500'000;
  constexpr std::size_t kWarmup = 100'000;
  constexpr std::size_t kCapacity = 1024;
  CpuPair cpus;
  if (cpus.second < 0) GTEST_SKIP() << "性能测试需要两个允许使用的 CPU";
  std::vector<double> rtts;
  rtts.reserve(kRuns);

  for (int r = 0; r < kRuns; ++r) {
    common::LFQueue<int64_t> q_ping(kCapacity);
    common::LFQueue<int64_t> q_pong(kCapacity);
    std::barrier sync(2);
    bool responder_pinned = false;
    const bool requester_pinned = PinThread(cpus.first);
    std::thread responder([&] {
      responder_pinned = PinThread(cpus.second);
      sync.arrive_and_wait();
      if (!responder_pinned || !requester_pinned) return;
      for (std::size_t i = 0; i < kWarmup; ++i) Push(q_pong, Pop(q_ping));
      sync.arrive_and_wait();
      for (std::size_t i = 0; i < kRoundTrips; ++i) Push(q_pong, Pop(q_ping));
    });
    sync.arrive_and_wait();
    if (!responder_pinned || !requester_pinned) {
      responder.join();
      GTEST_SKIP() << "性能测试绑核失败";
    }
    bool valid = true;
    for (std::size_t i = 0; i < kWarmup; ++i) {
      Push(q_ping, static_cast<int64_t>(i));
      valid &= Pop(q_pong) == static_cast<int64_t>(i);
    }
    sync.arrive_and_wait();
    const auto started = NowNs();
    for (std::size_t i = 0; i < kRoundTrips; ++i) {
      Push(q_ping, static_cast<int64_t>(i));
      valid &= Pop(q_pong) == static_cast<int64_t>(i);
    }
    const auto elapsed = NowNs() - started;
    responder.join();
    // 即使消息损坏也完成协议并回收线程，然后报告失败。
    ASSERT_TRUE(valid) << "乒乓消息损坏，轮次 " << r;
    ASSERT_GT(elapsed, 0);
    rtts.push_back(static_cast<double>(elapsed) / kRoundTrips);
  }
  for (int r = 0; r < kRuns; ++r) {
    std::printf(
        "原始成绩：RTT，轮次=%d，CPU=%d,%d，槽位=%zu，预热=%zu，往返=%zu，纳秒/"
        "往返=%.6f\n",
        r + 1, cpus.first, cpus.second, kCapacity, kWarmup, kRoundTrips,
        rtts[r]);
  }
  std::sort(rtts.begin(), rtts.end());
  std::printf(
      "\n乒乓 RTT（CPU=%d,%d，槽位=%zu，往返=%zu，轮数=%d）\n"
      "  各轮平均 RTT 的最小/中位/最大：%.1f / %.1f / %.1f 纳秒\n"
      "  不是逐消息延迟分位数；计时起止均在请求线程。\n",
      cpus.first, cpus.second, kCapacity, kRoundTrips, kRuns, rtts.front(),
      rtts[kRuns / 2], rtts.back());
}

TEST(LFQueueCorrectness, RejectInvalidCpu) {
  EXPECT_FALSE(PinThread(-1));
  EXPECT_FALSE(PinThread(CPU_SETSIZE));
}

TEST(LFQueueCorrectness, ProcessTimeout) {
  EXPECT_EXIT(
      {
        alarm(1);
        for (;;) pause();
      },
      ::testing::ExitedWithCode(124), "队列测试超时");
}

TEST(LFQueueCorrectness, UncommittedWriteIsInvisible) {
  common::LFQueue<int> q(2);
  auto* slot = q.TryGetNextToWriteTo();
  ASSERT_NE(slot, nullptr);
  *slot = 42;
  EXPECT_EQ(q.GetNextToRead(), nullptr);
  EXPECT_EQ(q.size(), 0);
  q.UpdateWriteIndex();
  const auto* value = q.GetNextToRead();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 42);
  q.UpdateReadIndex();
}

TEST(LFQueueCorrectness, UnreleasedReadCannotBeOverwritten) {
  common::LFQueue<int> q(2);
  auto* slot = q.TryGetNextToWriteTo();
  ASSERT_NE(slot, nullptr);
  *slot = 42;
  q.UpdateWriteIndex();
  const auto* value = q.GetNextToRead();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(q.TryGetNextToWriteTo(), nullptr);
  EXPECT_EQ(*value, 42);
  q.UpdateReadIndex();
  slot = q.TryGetNextToWriteTo();
  ASSERT_NE(slot, nullptr);
  *slot = 99;
  q.UpdateWriteIndex();
  value = q.GetNextToRead();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 99);
  q.UpdateReadIndex();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (std::signal(SIGALRM, TimeoutHandler) == SIG_ERR) return 1;
  ::testing::UnitTest::GetInstance()->listeners().Append(new TestDeadline);
  return RUN_ALL_TESTS();
}
