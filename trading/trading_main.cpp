#include <csignal>

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"

#include "common/logging.h"

/// Main components.
common::Logger *logger = nullptr;
trading::TradeEngine *trade_engine = nullptr;
trading::MarketDataConsumer *market_data_consumer = nullptr;
trading::OrderGateway *order_gateway = nullptr;

/// ./trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
int main(int argc, char **argv) {
  if(argc < 3) {
    FATAL("USAGE trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...");
  }

  const common::ClientId client_id = atoi(argv[1]);
  srand(client_id);

  const auto algo_type = trading::StringToAlgoType(argv[2]);

  logger = new common::Logger("trading_main_" + std::to_string(client_id) + ".log");

  const int sleep_time = 20 * 1000;

  // The lock free queues to facilitate communication between order gateway <-> trade engine and market data consumer -> trade engine.
  exchange::ClientRequestLFQueue client_requests(common::kMaxClientUpdates);
  exchange::ClientResponseLFQueue client_responses(common::kMaxClientUpdates);
  exchange::MatchingEngineMarketUpdateLFQueue market_updates(common::kMaxMarketUpdates);

  std::string time_str;

  trading::TradeEngineCfgHashMap ticker_cfg;

  // Parse and initialize the TradeEngineCfgHashMap above from the command line arguments.
  // [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
  size_t next_ticker_id = 0;
  for (int i = 3; i < argc; i += 5, ++next_ticker_id) {
    ticker_cfg.at(next_ticker_id) = {static_cast<common::Quantity>(std::atoi(argv[i])), std::atof(argv[i + 1]),
                                     {static_cast<common::Quantity>(std::atoi(argv[i + 2])),
                                      static_cast<common::Quantity>(std::atoi(argv[i + 3])),
                                      std::atof(argv[i + 4])}};
  }

  common::GetCurrentTimeStr(time_str);
  logger->log("%:% %() % Starting Trade Engine...\n", __FILE__, __LINE__, __FUNCTION__, time_str);
  trade_engine = new trading::TradeEngine(client_id, algo_type,
                                          ticker_cfg,
                                          &client_requests,
                                          &client_responses,
                                          &market_updates);
  trade_engine->start();

  const std::string order_gw_ip = "127.0.0.1";
  const std::string order_gw_iface = "lo";
  const int order_gw_port = 12345;

  common::GetCurrentTimeStr(time_str);
  logger->log("%:% %() % Starting Order Gateway...\n", __FILE__, __LINE__, __FUNCTION__, time_str);
  order_gateway = new trading::OrderGateway(client_id, &client_requests, &client_responses, order_gw_ip, order_gw_iface, order_gw_port);
  order_gateway->start();

  const std::string mkt_data_iface = "lo";
  const std::string snapshot_ip = "233.252.14.1";
  const int snapshot_port = 20000;
  const std::string incremental_ip = "233.252.14.3";
  const int incremental_port = 20001;

  common::GetCurrentTimeStr(time_str);
  logger->log("%:% %() % Starting Market Data Consumer...\n", __FILE__, __LINE__, __FUNCTION__, time_str);
  market_data_consumer = new trading::MarketDataConsumer(client_id, &market_updates, mkt_data_iface, snapshot_ip, snapshot_port, incremental_ip, incremental_port);
  market_data_consumer->start();

  usleep(10 * 1000 * 1000);

  trade_engine->initLastEventTime();

  // For the random trading algorithm, we simply implement it here instead of creating a new trading algorithm which is another possibility.
  // Generate random orders with random attributes and randomly cancel some of them.
  if (algo_type == trading::AlgoType::RANDOM) {
    common::OrderId order_id = client_id * 1000;
    std::vector<exchange::MatchingEngineClientRequest> client_requests_vec;
    std::array<common::Price, common::kMaxSymbols> ticker_base_price;
    for (size_t i = 0; i < common::kMaxSymbols; ++i)
      ticker_base_price[i] = (rand() % 100) + 100;
    for (size_t i = 0; i < 10000; ++i) {
      const common::SymbolId ticker_id = rand() % common::kMaxSymbols;
      const common::Price price = ticker_base_price[ticker_id] + (rand() % 10) + 1;
      const common::Quantity qty = 1 + (rand() % 100) + 1;
      const common::Side side = (rand() % 2 ? common::Side::BUY : common::Side::SELL);

      exchange::MatchingEngineClientRequest new_request{exchange::ClientRequestType::NEW, client_id, ticker_id, order_id++, side,
                                            price, qty};
      trade_engine->sendClientRequest(&new_request);
      usleep(sleep_time);

      client_requests_vec.push_back(new_request);
      const auto cxl_index = rand() % client_requests_vec.size();
      auto cxl_request = client_requests_vec[cxl_index];
      cxl_request.type_ = exchange::ClientRequestType::CANCELED;
      trade_engine->sendClientRequest(&cxl_request);
      usleep(sleep_time);

      if (trade_engine->silentSeconds() >= 60) {
        common::GetCurrentTimeStr(time_str);
        logger->log("%:% %() % Stopping early because been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                    time_str, trade_engine->silentSeconds());

        break;
      }
    }
  }

  while (trade_engine->silentSeconds() < 60) {
    common::GetCurrentTimeStr(time_str);
    logger->log("%:% %() % Waiting till no activity, been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                time_str, trade_engine->silentSeconds());

    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(30s);
  }

  trade_engine->stop();
  market_data_consumer->stop();
  order_gateway->stop();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(10s);

  delete logger;
  logger = nullptr;
  delete trade_engine;
  trade_engine = nullptr;
  delete market_data_consumer;
  market_data_consumer = nullptr;
  delete order_gateway;
  order_gateway = nullptr;

  std::this_thread::sleep_for(10s);

  exit(EXIT_SUCCESS);
}
