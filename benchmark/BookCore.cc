#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <memory>

#include "../exchange/matcher/matching_engine.h"

namespace {

using common::OrderId_INVALID;
using common::Price_INVALID;
using common::Priority_INVALID;
using common::Quantity_INVALID;
using common::Side;
using exchange::ClientResponseType;
using exchange::MarketUpdateType;

template <typename T>
void ExpectMessages(common::LFQueue<T>& queue,
                    std::initializer_list<T> expected_messages) {
  ASSERT_EQ(queue.size(), expected_messages.size());
  for (const auto& expected : expected_messages) {
    const auto* actual = queue.GetNextToRead();
    ASSERT_NE(actual, nullptr);
    // 两类出站协议都是紧凑布局，逐字节核对所有字段及消息顺序。
    EXPECT_EQ(std::memcmp(actual, &expected, sizeof(T)), 0)
        << "实际：" << actual->toString() << "，预期：" << expected.toString();
    queue.UpdateReadIndex();
  }
  EXPECT_EQ(queue.GetNextToRead(), nullptr);
}

class BookCoreOutputTest : public ::testing::Test {
 protected:
  exchange::ClientResponseLFQueue responses_{64};
  exchange::MatchingEngineMarketUpdateLFQueue updates_{64};
  common::Logger logger_{"BookCoreTest.log"};
  std::unique_ptr<exchange::BookCore> book_ =
      std::make_unique<exchange::BookCore>(0, exchange::kDefaultPriceBand,
                                           responses_, updates_, &logger_);
};

TEST_F(BookCoreOutputTest, AddAndCancel) {
  book_->Add(1, 10, Side::BUY, 100, 7);
  book_->Cancel(1, 10);

  ExpectMessages(responses_, {
                                 {ClientResponseType::ACCEPTED, 1, 0, 10, 1,
                                  Side::BUY, 100, Quantity_INVALID, 7},
                                 {ClientResponseType::CANCELED, 1, 0, 10, 1,
                                  Side::BUY, 100, Quantity_INVALID, 7},
                             });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, Side::BUY, 100, 7, 1},
                     {MarketUpdateType::CANCEL, 0, 1, Side::BUY, 100,
                      Quantity_INVALID, Priority_INVALID},
                 });
}

TEST_F(BookCoreOutputTest, UnknownCancelOnlyEmitsRejection) {
  book_->Cancel(1, 10);

  ExpectMessages(responses_, {
                                 {ClientResponseType::CANCEL_REJECTED, 1, 0, 10,
                                  OrderId_INVALID, Side::INVALID, Price_INVALID,
                                  Quantity_INVALID, Quantity_INVALID},
                             });
  ExpectMessages(updates_, {});
}

TEST_F(BookCoreOutputTest, PartialThenFullFill) {
  book_->Add(1, 10, Side::SELL, 100, 10);
  book_->Add(2, 20, Side::BUY, 101, 4);
  book_->Add(2, 21, Side::BUY, 101, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, Side::SELL, 100,
           Quantity_INVALID, 10},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, Side::BUY, 101,
           Quantity_INVALID, 4},
          {ClientResponseType::FILLED, 2, 0, 20, 2, Side::BUY, 100, 4, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, Side::SELL, 100, 4, 6},
          {ClientResponseType::ACCEPTED, 2, 0, 21, 3, Side::BUY, 101,
           Quantity_INVALID, 6},
          {ClientResponseType::FILLED, 2, 0, 21, 3, Side::BUY, 100, 6, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, Side::SELL, 100, 6, 0},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, Side::SELL, 100, 10, 1},
          {MarketUpdateType::TRADE, 0, 1, Side::SELL, 100, 4, Priority_INVALID},
          {MarketUpdateType::MODIFY, 0, 1, Side::SELL, 100, 6, 1},
          {MarketUpdateType::TRADE, 0, 1, Side::SELL, 100, 6, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, Side::SELL, 100, Quantity_INVALID,
           Priority_INVALID},
      });
}

TEST_F(BookCoreOutputTest, SellRemainderBecomesRestingOrder) {
  book_->Add(1, 10, Side::BUY, 100, 3);
  book_->Add(2, 20, Side::SELL, 99, 5);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, Side::BUY, 100,
           Quantity_INVALID, 3},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, Side::SELL, 99,
           Quantity_INVALID, 5},
          {ClientResponseType::FILLED, 2, 0, 20, 2, Side::SELL, 100, 3, 2},
          {ClientResponseType::FILLED, 1, 0, 10, 1, Side::BUY, 100, 3, 0},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, Side::BUY, 100, 3, 1},
          {MarketUpdateType::TRADE, 0, 1, Side::BUY, 100, 3, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, Side::BUY, 100, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::ADD, 0, 2, Side::SELL, 99, 2, 1},
      });
}

TEST_F(BookCoreOutputTest, FillsFollowMakerFifoOrder) {
  book_->Add(1, 10, Side::SELL, 100, 3);
  book_->Add(1, 11, Side::SELL, 100, 4);
  book_->Add(2, 20, Side::BUY, 100, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, Side::SELL, 100,
           Quantity_INVALID, 3},
          {ClientResponseType::ACCEPTED, 1, 0, 11, 2, Side::SELL, 100,
           Quantity_INVALID, 4},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 3, Side::BUY, 100,
           Quantity_INVALID, 6},
          {ClientResponseType::FILLED, 2, 0, 20, 3, Side::BUY, 100, 3, 3},
          {ClientResponseType::FILLED, 1, 0, 10, 1, Side::SELL, 100, 3, 0},
          {ClientResponseType::FILLED, 2, 0, 20, 3, Side::BUY, 100, 3, 0},
          {ClientResponseType::FILLED, 1, 0, 11, 2, Side::SELL, 100, 3, 1},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, Side::SELL, 100, 3, 1},
          {MarketUpdateType::ADD, 0, 2, Side::SELL, 100, 4, 2},
          {MarketUpdateType::TRADE, 0, 1, Side::SELL, 100, 3, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, Side::SELL, 100, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::TRADE, 0, 2, Side::SELL, 100, 3, Priority_INVALID},
          {MarketUpdateType::MODIFY, 0, 2, Side::SELL, 100, 1, 2},
      });
}

TEST(MatchingEngineOutputTest, SymbolsShareOutputQueuesWithoutCrossMatching) {
  exchange::ClientRequestLFQueue requests{64};
  exchange::ClientResponseLFQueue responses{64};
  exchange::MatchingEngineMarketUpdateLFQueue updates{64};
  exchange::MatchingEngine engine{&requests, &responses, &updates};
  const exchange::MatchingEngineClientRequest inputs[] = {
      {exchange::ClientRequestType::NEW, 1, 1, 10, Side::BUY, 100, 3},
      {exchange::ClientRequestType::NEW, 1, 2, 10, Side::SELL, 100, 4},
      {exchange::ClientRequestType::CANCELED, 1, 1, 10},
      {exchange::ClientRequestType::CANCELED, 1, 2, 10},
  };
  for (const auto& input : inputs) {
    engine.ProcessClientRequest(&input);
  }

  ExpectMessages(responses, {
                                {ClientResponseType::ACCEPTED, 1, 1, 10, 1,
                                 Side::BUY, 100, Quantity_INVALID, 3},
                                {ClientResponseType::ACCEPTED, 1, 2, 10, 1,
                                 Side::SELL, 100, Quantity_INVALID, 4},
                                {ClientResponseType::CANCELED, 1, 1, 10, 1,
                                 Side::BUY, 100, Quantity_INVALID, 3},
                                {ClientResponseType::CANCELED, 1, 2, 10, 1,
                                 Side::SELL, 100, Quantity_INVALID, 4},
                            });
  ExpectMessages(updates,
                 {
                     {MarketUpdateType::ADD, 1, 1, Side::BUY, 100, 3, 1},
                     {MarketUpdateType::ADD, 2, 1, Side::SELL, 100, 4, 1},
                     {MarketUpdateType::CANCEL, 1, 1, Side::BUY, 100,
                      Quantity_INVALID, Priority_INVALID},
                     {MarketUpdateType::CANCEL, 2, 1, Side::SELL, 100,
                      Quantity_INVALID, Priority_INVALID},
                 });
}

}  // namespace
