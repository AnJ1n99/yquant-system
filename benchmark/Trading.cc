#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include "exchange/matcher/book_core.h"
#include "trading/order_gw/order_gateway.h"
#include "trading/strategy/trade_engine.h"

namespace {

struct TestSocket {
  int fd = -1;
  ~TestSocket() {
    if (fd >= 0) close(fd);
  }
};

bool ReceiveAll(int fd, void* buffer, size_t size) {
  auto* bytes = static_cast<char*>(buffer);
  size_t received = 0;
  while (received < size) {
    pollfd pending{fd, POLLIN, 0};
    if (poll(&pending, 1, 2000) <= 0) return false;
    const auto count =
        recv(fd, bytes + received, size - received, MSG_DONTWAIT);
    if (count <= 0) return false;
    received += static_cast<size_t>(count);
  }
  return true;
}

TEST(TradingWiring, GatewayConnectsAndPreservesSplitResponses) {
  TestSocket listener{socket(AF_INET, SOCK_STREAM, 0)};
  ASSERT_GE(listener.fd, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(
      bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
      0);
  ASSERT_EQ(listen(listener.fd, 1), 0);
  socklen_t address_size = sizeof(address);
  ASSERT_EQ(getsockname(listener.fd, reinterpret_cast<sockaddr*>(&address),
                        &address_size),
            0);

  exchange::ClientRequestLFQueue requests(16);
  exchange::ClientResponseLFQueue responses(16);
  trading::OrderGateway gateway(1, &requests, &responses, "127.0.0.1", "lo",
                                ntohs(address.sin_port));
  *requests.GetNextToWriteTo() = {
      exchange::ClientRequestType::NEW, 1, 0, 42, common::Side::BUY, 100, 10};
  requests.UpdateWriteIndex();
  gateway.start();
  pollfd pending{listener.fd, POLLIN, 0};
  ASSERT_GT(poll(&pending, 1, 2000), 0);
  TestSocket peer{accept(listener.fd, nullptr, nullptr)};
  ASSERT_GE(peer.fd, 0);
  exchange::OrderManagerClientRequest request{};
  ASSERT_TRUE(ReceiveAll(peer.fd, &request, sizeof(request)));
  EXPECT_EQ(request.seqNum, 1U);
  EXPECT_EQ(request.matching_engine_client_request.orderId_, 42U);
  EXPECT_EQ(request.matching_engine_client_request.quantity_, 10U);

  const exchange::OrderManagerClientResponse response{
      1,
      {exchange::ClientResponseType::ACCEPTED, 1, 0, 42, 7, common::Side::BUY,
       100, 0, 10}};
  const auto* bytes = reinterpret_cast<const char*>(&response);
  constexpr size_t kPrefixSize = sizeof(size_t) + 5;
  ASSERT_EQ(send(peer.fd, bytes, kPrefixSize, MSG_NOSIGNAL),
            static_cast<ssize_t>(kPrefixSize));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(responses.size(), 0U);
  ASSERT_EQ(send(peer.fd, bytes + kPrefixSize, sizeof(response) - kPrefixSize,
                 MSG_NOSIGNAL),
            static_cast<ssize_t>(sizeof(response) - kPrefixSize));

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  const exchange::MatchingEngineClientResponse* received = nullptr;
  while (!(received = responses.GetNextToRead()) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_NE(received, nullptr);
  EXPECT_EQ(received->type_, exchange::ClientResponseType::ACCEPTED);
  EXPECT_EQ(received->client_order_id_, 42U);
  EXPECT_EQ(received->remaining_quantity_, 10U);
  responses.UpdateReadIndex();
  gateway.stop();
}

TEST(TradingWiring, BookRebuildsFromCurrentMarketProtocol) {
  common::Logger logger("trading_book_test.log");
  auto book = std::make_unique<trading::MarketOrderBook>(0, &logger);
  EXPECT_EQ(book->getBBO()->bid_qty_, 0U);
  EXPECT_EQ(book->getBBO()->ask_qty_, 0U);

  exchange::MatchingEngineMarketUpdate update{
      exchange::MarketUpdateType::ADD, 0, 1, common::Side::BUY, 0, 10, 1};
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->bid_price_, 0);
  EXPECT_EQ(book->getBBO()->bid_qty_, 10U);

  update = {
      exchange::MarketUpdateType::ADD, 0, 2, common::Side::SELL, 100, 7, 1};
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->ask_price_, 100);
  EXPECT_EQ(book->getBBO()->ask_qty_, 7U);

  update.type_ = exchange::MarketUpdateType::MODIFY;
  update.quantity_ = 3;
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->ask_qty_, 3U);

  update.type_ = exchange::MarketUpdateType::CANCEL;
  update.quantity_ = 0;
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->ask_qty_, 0U);

  update = {exchange::MarketUpdateType::CLEAR, 0, 0, common::Side{}, 0, 0, 0};
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->bid_qty_, 0U);
  EXPECT_EQ(book->getBBO()->ask_qty_, 0U);

  update = {exchange::MarketUpdateType::ADD, 0, 3, common::Side::BUY, 0, 4, 1};
  book->onMarketUpdate(&update);
  EXPECT_EQ(book->getBBO()->bid_qty_, 4U);
}

TEST(TradingWiring, FillUsesExecutedQuantityAndSignedPosition) {
  common::Logger logger("trading_position_test.log");
  trading::PositionInfo position;
  exchange::MatchingEngineClientResponse response{
      exchange::ClientResponseType::FILLED,
      1,
      0,
      1,
      1,
      common::Side::SELL,
      100,
      7,
      3};
  position.addFill(&response, &logger);
  EXPECT_EQ(position.position_, -7);
  EXPECT_EQ(position.volume_, 7U);
  EXPECT_DOUBLE_EQ(position.total_pnl_, 0);

  response.side_ = common::Side::BUY;
  response.price_ = 90;
  response.executed_quantity_ = 7;
  position.addFill(&response, &logger);
  EXPECT_EQ(position.position_, 0);
  EXPECT_DOUBLE_EQ(position.real_pnl_, 70);
}

TEST(TradingWiring, OrdersUseCurrentRequestsAndRejectWithoutSide) {
  exchange::ClientRequestLFQueue requests(16);
  exchange::ClientResponseLFQueue responses(16);
  exchange::MatchingEngineMarketUpdateLFQueue updates(16);
  trading::TradeEngineCfgHashMap config{};
  config[0] = {10, 1, {100, 100, -1000}};
  auto engine = std::make_unique<trading::TradeEngine>(
      1, trading::AlgoType::RANDOM, config, &requests, &responses, &updates);
  common::Logger logger("trading_order_test.log");
  trading::PositionKeeper positions(&logger);
  trading::RiskManager risk(&positions, config);
  trading::OrderManager orders(&logger, engine.get(), risk);
  auto matcher = std::make_unique<exchange::BookCore>(
      0, exchange::kDefaultPriceBand, responses, updates);

  orders.moveOrders(0, -1, common::kMaxPriceLevels, 10);
  EXPECT_EQ(requests.GetNextToRead(), nullptr);
  orders.moveOrders(0, 0, std::nullopt, 10);
  const auto* request = requests.GetNextToRead();
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(request->type_, exchange::ClientRequestType::NEW);
  EXPECT_EQ(request->clientId_, 1U);
  EXPECT_EQ(request->symbolId_, 0U);
  EXPECT_EQ(request->side_, common::Side::BUY);
  EXPECT_EQ(request->price_, 0);
  EXPECT_EQ(request->quantity_, 10U);
  const auto order_id = request->orderId_;
  matcher->Add(request->clientId_, request->orderId_, request->side_,
               request->price_, request->quantity_);
  requests.UpdateReadIndex();
  ASSERT_NE(responses.GetNextToRead(), nullptr);
  auto response = *responses.GetNextToRead();
  EXPECT_EQ(response.type_, exchange::ClientResponseType::ACCEPTED);
  orders.onOrderUpdate(&response);
  responses.UpdateReadIndex();

  // 成交先于撤单到达交易所，但交易端尚未消费成交回报。
  matcher->Add(2, 1, common::Side::SELL, 0, 10);
  orders.moveOrders(0, std::nullopt, std::nullopt, 10);
  request = requests.GetNextToRead();
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(request->type_, exchange::ClientRequestType::CANCELED);
  EXPECT_EQ(request->orderId_, order_id);
  matcher->Cancel(request->clientId_, request->orderId_);
  requests.UpdateReadIndex();

  bool saw_rejection = false;
  for (auto* reply = responses.GetNextToRead(); reply;
       reply = responses.GetNextToRead()) {
    if (reply->client_id_ == 1) {
      orders.onOrderUpdate(reply);
      if (reply->type_ == exchange::ClientResponseType::CANCEL_REJECTED) {
        response = *reply;
        saw_rejection = true;
        EXPECT_EQ(static_cast<unsigned>(reply->side_), 0U);
      }
    }
    responses.UpdateReadIndex();
  }
  ASSERT_TRUE(saw_rejection);
  const auto* sides = orders.getOMOrderSideHashMap(0);
  EXPECT_EQ((*sides)[0].order_state_, trading::OMOrderState::DEAD);

  orders.moveOrders(0, 1, std::nullopt, 10);
  request = requests.GetNextToRead();
  ASSERT_NE(request, nullptr);
  EXPECT_NE(request->orderId_, order_id);
  requests.UpdateReadIndex();
  orders.onOrderUpdate(&response);
  EXPECT_EQ((*sides)[0].order_state_, trading::OMOrderState::PENDING_NEW);
}

}  // namespace
