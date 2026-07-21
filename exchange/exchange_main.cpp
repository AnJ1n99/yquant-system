/*
 交易服务器是一个独立的进程（exchange_main），它充当交易系统中订单处理和市场数据分发的中央枢纽。
 它通过 TCP
 接收来自多个交易客户端的订单请求，在价格-时间优先级的订单簿中匹配订单，并通过
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

#include "../common/logging.h"
#include "matcher/matching_engine.h"
#include "order_manager/client_request.h"
#include "order_manager/client_response.h"

// 主要组件，设为全局变量以便信号处理器访问
Common::Logger* logger = nullptr;

/// 外部信号触发时优雅关闭服务器
void signal_handler(int) {
  std::this_thread::sleep_for(
      std::chrono::seconds(10));  // 等待10秒以确保资源释放

  // 释放所有组件资源
  delete logger;
  logger = nullptr;
  delete matching_engine;
  matching_engine = nullptr;
  delete market_data_publisher;
  market_data_publisher = nullptr;
  delete order_server;
  order_server = nullptr;

  std::this_thread::sleep_for(std::chrono::seconds(10));  // 等待释放完成

  exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
  // main logger
  logger = new Common::Logger("exchange_main.log");

  // 注册信号处理器
  std::signal(SIGINT, signal_handler);

  // 主循环休息时间(ms)
  const int sleep_time = 100 * 1000;

  // 无锁队列，用于订单服务器与匹配引擎、匹配引擎与市场数据发布器之间的通信
  Exchange::ClientRequestLFQueue client_requests(Common::ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(
      Common::ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(Common::ME_MAX_MARKET_UPDATES);

  std::string time_str_;

  // 启动匹配引擎

  // config market data publisher

  // start market data publisher

  // config order server

  // start order server

  // main loop
  while (true) {
  }
}
