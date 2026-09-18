/*
 交易服务器是一个独立的进程（exchange_main），它充当交易系统中订单处理和市场数据分发的中央枢纽。
 它通过 TCP
 接收来自多个交易客户端的订单请求，在价格-时间优先级的订单簿中匹配订单， 并通过
 UDP 多播流发布市场更新。

 主要职责：
 1. 接受并验证来自多个客户的订单请求（新建、取消）
 2. 按股票代码维护订单簿，并按价格时间优先级排序。
 3. 执行确定性 FIFO 匹配逻辑 (序列器)
 4. 生成针对特定客户的响应（已接受、已完成、已取消等）
 5. 向所有订阅用户发布全市场更新（新增、修改、取消、交易）
 6. 提供用于市场数据恢复的定期快照流
 */

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

#include "../common/logging.h"
#include "../common/time_utils.h"
#include "market_data/market_data_publisher.h"
#include "matcher/matching_engine.h"
#include "order_manager/client_request.h"
#include "order_manager/client_response.h"
#include "order_manager/order_manager.h"

// 组件配置。
// OrderManager 的 iface 必须是真实网卡名，start() 会据此 bind 监听端口；
// 先用 lo 做本机联调，联通外部客户端时再换成 ens17。
constexpr const char* kOrderManagerIface = "lo";
constexpr int kOrderManagerPort = 12345;
// MarketDataPublisher 的组播接口与两组流地址：增量流沿用既有地址，快照流
// 使用相邻组播组与端口；与 OrderManager 一样先用 lo 做本机联调。
constexpr const char* kMarketDataIface = "lo";
constexpr const char* kMarketDataMcastAddr = "239.0.0.1";
constexpr int kMarketDataMcastPort = 12346;
constexpr const char* kSnapshotMcastAddr = "239.0.0.2";
constexpr int kSnapshotMcastPort = 12347;

// 主要组件，设为全局变量以便信号处理器访问
common::Logger* logger = nullptr;
exchange::MatchingEngine* matching_engine = nullptr;
exchange::MarketDataPublisher* market_data_publisher = nullptr;
exchange::OrderManager* order_manager = nullptr;

/// 外部信号触发时优雅关闭服务器
void signal_handler(int) {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(10s);  // 等待10秒以确保资源释放

  // 依次释放各组件资源
  delete logger;
  logger = nullptr;
  delete matching_engine;
  matching_engine = nullptr;
  delete market_data_publisher;
  market_data_publisher = nullptr;
  delete order_manager;
  order_manager = nullptr;

  std::this_thread::sleep_for(10s);  // 等待释放完成

  exit(EXIT_SUCCESS);
}

int main() {
  // main logger
  logger = new common::Logger("exchange_main.log");

  // 注册信号处理器
  // crtl+c pkill -2 pid 会优雅关机
  std::signal(SIGINT, signal_handler);

  // 主循环轮询间隔（单位写进类型里，避免再出现量纲歧义）
  constexpr auto kMainLoopSleep = std::chrono::milliseconds(100);

  // 无锁队列，用于订单服务器与匹配引擎、匹配引擎与市场数据发布器之间的通信
  exchange::ClientRequestLFQueue client_requests(common::kMaxClientUpdates);
  exchange::ClientResponseLFQueue client_responses(common::kMaxClientUpdates);
  exchange::MatchingEngineMarketUpdateLFQueue market_updates(
      common::kMaxMarketUpdates);

  std::string time_str_;

  // 启动匹配引擎
  common::GetCurrentTimeStr(time_str_);
  logger->log("%:% %() % Starting MatchingEngine..\n", __FILE__, __LINE__,
              __FUNCTION__, time_str_);
  matching_engine = new exchange::MatchingEngine(
      &client_requests, &client_responses, &market_updates);
  matching_engine->start();

  // config market data publisher
  common::GetCurrentTimeStr(time_str_);
  logger->log("%:% %() % Starting MarketDataPublisher..\n", __FILE__, __LINE__,
              __FUNCTION__, time_str_);
  market_data_publisher = new exchange::MarketDataPublisher(
      &market_updates, kMarketDataIface, kSnapshotMcastAddr, kSnapshotMcastPort,
      kMarketDataMcastAddr, kMarketDataMcastPort);

  // start market data publisher
  market_data_publisher->start();

  // config order server
  common::GetCurrentTimeStr(time_str_);
  logger->log("%:% %() % Starting OrderManager..\n", __FILE__, __LINE__,
              __FUNCTION__, time_str_);
  order_manager =
      new exchange::OrderManager(&client_requests, &client_responses,
                                 kOrderManagerIface, kOrderManagerPort);

  // start order server
  order_manager->start();

  // main loop
  while (true) {
    common::GetCurrentTimeStr(time_str_);
    logger->log("%:% %() % Sleeping for a few milliseconds..\n", __FILE__,
                __LINE__, __FUNCTION__, time_str_);
    std::this_thread::sleep_for(kMainLoopSleep);
  }
}
