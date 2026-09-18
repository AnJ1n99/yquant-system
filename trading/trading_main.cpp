#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

#include "market_data/market_data_consumer.h"
#include "order_gw/order_gateway.h"
#include "strategy/trade_engine.h"

namespace {

template <typename T>
T ParseArgument(const char* argument) {
  const std::string_view text(argument);
  T value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  ASSERT(error == std::errc{} && end == text.data() + text.size(),
         "无效参数：" + std::string(text));
  return value;
}

// RANDOM 模式由主线程独占请求队列的生产端，策略模式由 TradeEngine 独占。
void RunRandom(common::ClientId client_id,
               exchange::ClientRequestLFQueue& requests,
               trading::TradeEngine& trade_engine) {
  using namespace std::chrono_literals;
  common::OrderId order_id = 1;
  std::vector<exchange::MatchingEngineClientRequest> history;
  history.reserve(10000);
  std::array<common::Price, common::kMaxSymbols> base_prices{};
  for (auto& price : base_prices) price = (std::rand() % 100) + 100;

  for (size_t i = 0; i < 10000; ++i) {
    const common::SymbolId symbol_id = std::rand() % common::kMaxSymbols;
    const exchange::MatchingEngineClientRequest request{
        exchange::ClientRequestType::NEW,
        client_id,
        symbol_id,
        order_id++,
        std::rand() % 2 ? common::Side::BUY : common::Side::SELL,
        base_prices[symbol_id] + (std::rand() % 10) + 1,
        static_cast<common::Quantity>(std::rand() % 100 + 1)};
    *requests.GetNextToWriteTo() = request;
    requests.UpdateWriteIndex();
    std::this_thread::sleep_for(20ms);

    history.push_back(request);
    auto cancel = history[std::rand() % history.size()];
    cancel.type_ = exchange::ClientRequestType::CANCELED;
    *requests.GetNextToWriteTo() = cancel;
    requests.UpdateWriteIndex();
    std::this_thread::sleep_for(20ms);
    if (trade_engine.silentSeconds() >= 60) break;
  }
}

}  // namespace

int main(int argc, char** argv) {
  ASSERT(argc >= 3 && (argc - 3) % 5 == 0 &&
             (argc - 3) / 5 <= static_cast<int>(common::kMaxSymbols),
         "用法：trading_main CLIENT_ID RANDOM|MAKER|TAKER "
         "[CLIP THRESHOLD MAX_ORDER_SIZE MAX_POSITION MAX_LOSS] ...");
  const auto client_id = ParseArgument<common::ClientId>(argv[1]);
  ASSERT(client_id < common::kMaxNumClients, "客户端编号超出交易所范围");
  const auto algo_type = trading::StringToAlgoType(argv[2]);
  ASSERT(algo_type != trading::AlgoType::INVALID, "未知策略类型");
  std::srand(client_id);

  trading::TradeEngineCfgHashMap config{};
  for (int i = 3; i < argc; i += 5) {
    auto& entry = config.at((i - 3) / 5);
    entry = {ParseArgument<common::Quantity>(argv[i]),
             ParseArgument<double>(argv[i + 1]),
             {ParseArgument<common::Quantity>(argv[i + 2]),
              ParseArgument<common::Quantity>(argv[i + 3]),
              ParseArgument<double>(argv[i + 4])}};
    ASSERT(std::isfinite(entry.threshold_) && entry.threshold_ >= 0 &&
               std::isfinite(entry.risk_cfg_.max_loss_) &&
               entry.risk_cfg_.max_loss_ <= 0,
           "阈值必须非负，最大亏损必须非正，且均为有限数值");
  }

  exchange::ClientRequestLFQueue requests(common::kMaxClientUpdates);
  exchange::ClientResponseLFQueue responses(common::kMaxClientUpdates);
  exchange::MatchingEngineMarketUpdateLFQueue updates(
      common::kMaxMarketUpdates);
  trading::TradeEngine trade_engine(client_id, algo_type, config, &requests,
                                    &responses, &updates);
  // 端点与 exchange/exchange_main.cpp 保持一致。
  trading::OrderGateway gateway(client_id, &requests, &responses, "127.0.0.1",
                                "lo", 12345);
  trading::MarketDataConsumer market_data(
      client_id, &updates, "lo", "239.0.0.2", 12347, "239.0.0.1", 12346);

  trade_engine.initLastEventTime();
  trade_engine.start();
  market_data.start();
  gateway.start();

  if (algo_type == trading::AlgoType::RANDOM) {
    RunRandom(client_id, requests, trade_engine);
  }
  while (trade_engine.silentSeconds() < 60) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // 先关闭行情输入；策略排空输入后停止，再关闭仍在发送请求的网关。
  market_data.stop();
  trade_engine.stop();
  gateway.stop();
  return EXIT_SUCCESS;
}
