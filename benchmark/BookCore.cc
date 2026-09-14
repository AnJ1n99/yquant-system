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
  std::unique_ptr<exchange::BookCore> book_ =
      std::make_unique<exchange::BookCore>(0, exchange::kDefaultPriceBand,
                                           responses_, updates_);
};

class BookCoreSideTest : public BookCoreOutputTest,
                         public ::testing::WithParamInterface<Side> {};

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

TEST_P(BookCoreSideTest, PartialThenFullFill) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 101 : 99;
  book_->Add(1, 10, maker_side, 100, 10);
  book_->Add(2, 20, taker_side, limit, 4);
  book_->Add(2, 21, taker_side, limit, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 100,
           Quantity_INVALID, 10},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, taker_side, limit,
           Quantity_INVALID, 4},
          {ClientResponseType::FILLED, 2, 0, 20, 2, taker_side, 100, 4, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 100, 4, 6},
          {ClientResponseType::ACCEPTED, 2, 0, 21, 3, taker_side, limit,
           Quantity_INVALID, 6},
          {ClientResponseType::FILLED, 2, 0, 21, 3, taker_side, 100, 6, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 100, 6, 0},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, maker_side, 100, 10, 1},
          {MarketUpdateType::TRADE, 0, 1, maker_side, 100, 4, Priority_INVALID},
          {MarketUpdateType::MODIFY, 0, 1, maker_side, 100, 6, 1},
          {MarketUpdateType::TRADE, 0, 1, maker_side, 100, 6, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, maker_side, 100, Quantity_INVALID,
           Priority_INVALID},
      });
}

TEST_P(BookCoreSideTest, RemainderBecomesRestingOrder) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 101 : 99;
  book_->Add(1, 10, maker_side, 100, 3);
  book_->Add(2, 20, taker_side, limit, 5);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 100,
           Quantity_INVALID, 3},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, taker_side, limit,
           Quantity_INVALID, 5},
          {ClientResponseType::FILLED, 2, 0, 20, 2, taker_side, 100, 3, 2},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 100, 3, 0},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, maker_side, 100, 3, 1},
          {MarketUpdateType::TRADE, 0, 1, maker_side, 100, 3, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, maker_side, 100, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::ADD, 0, 2, taker_side, limit, 2, 1},
      });
}

TEST_P(BookCoreSideTest, FillsFollowMakerFifoOrder) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  book_->Add(1, 10, maker_side, 100, 3);
  book_->Add(1, 11, maker_side, 100, 4);
  book_->Add(2, 20, taker_side, 100, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 100,
           Quantity_INVALID, 3},
          {ClientResponseType::ACCEPTED, 1, 0, 11, 2, maker_side, 100,
           Quantity_INVALID, 4},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 3, taker_side, 100,
           Quantity_INVALID, 6},
          {ClientResponseType::FILLED, 2, 0, 20, 3, taker_side, 100, 3, 3},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 100, 3, 0},
          {ClientResponseType::FILLED, 2, 0, 20, 3, taker_side, 100, 3, 0},
          {ClientResponseType::FILLED, 1, 0, 11, 2, maker_side, 100, 3, 1},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, maker_side, 100, 3, 1},
          {MarketUpdateType::ADD, 0, 2, maker_side, 100, 4, 2},
          {MarketUpdateType::TRADE, 0, 1, maker_side, 100, 3, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, maker_side, 100, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::TRADE, 0, 2, maker_side, 100, 3, Priority_INVALID},
          {MarketUpdateType::MODIFY, 0, 2, maker_side, 100, 1, 2},
      });
}

TEST_P(BookCoreSideTest, EmptyBookRestsOrder) {
  const Side side = GetParam();
  book_->Add(1, 10, side, 100, 7);

  ExpectMessages(responses_, {{ClientResponseType::ACCEPTED, 1, 0, 10, 1, side,
                               100, Quantity_INVALID, 7}});
  ExpectMessages(updates_, {{MarketUpdateType::ADD, 0, 1, side, 100, 7, 1}});
}

TEST_P(BookCoreSideTest, NonCrossingOrderLeavesMakerUntouched) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 99 : 101;
  book_->Add(1, 10, maker_side, 100, 3);
  book_->Add(2, 20, taker_side, limit, 5);
  book_->Cancel(1, 10);

  ExpectMessages(responses_, {
                                 {ClientResponseType::ACCEPTED, 1, 0, 10, 1,
                                  maker_side, 100, Quantity_INVALID, 3},
                                 {ClientResponseType::ACCEPTED, 2, 0, 20, 2,
                                  taker_side, limit, Quantity_INVALID, 5},
                                 {ClientResponseType::CANCELED, 1, 0, 10, 1,
                                  maker_side, 100, Quantity_INVALID, 3},
                             });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, maker_side, 100, 3, 1},
                     {MarketUpdateType::ADD, 0, 2, taker_side, limit, 5, 1},
                     {MarketUpdateType::CANCEL, 0, 1, maker_side, 100,
                      Quantity_INVALID, Priority_INVALID},
                 });
}

TEST_P(BookCoreSideTest, MultipleLevelsFollowPricePriorityAndStopAtLimit) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 101 : 99;
  const common::Price outside = taker_side == Side::BUY ? 103 : 97;
  // 先放较差价位，验证撮合按价格而非跨价位的到达顺序进行。
  book_->Add(1, 10, maker_side, limit, 3);
  book_->Add(1, 11, maker_side, 100, 2);
  book_->Add(1, 12, maker_side, outside, 4);
  book_->Add(2, 20, taker_side, limit, 8);
  book_->Cancel(1, 12);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, limit,
           Quantity_INVALID, 3},
          {ClientResponseType::ACCEPTED, 1, 0, 11, 2, maker_side, 100,
           Quantity_INVALID, 2},
          {ClientResponseType::ACCEPTED, 1, 0, 12, 3, maker_side, outside,
           Quantity_INVALID, 4},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 4, taker_side, limit,
           Quantity_INVALID, 8},
          {ClientResponseType::FILLED, 2, 0, 20, 4, taker_side, 100, 2, 6},
          {ClientResponseType::FILLED, 1, 0, 11, 2, maker_side, 100, 2, 0},
          {ClientResponseType::FILLED, 2, 0, 20, 4, taker_side, limit, 3, 3},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, limit, 3, 0},
          {ClientResponseType::CANCELED, 1, 0, 12, 3, maker_side, outside,
           Quantity_INVALID, 4},
      });
  ExpectMessages(
      updates_,
      {
          {MarketUpdateType::ADD, 0, 1, maker_side, limit, 3, 1},
          {MarketUpdateType::ADD, 0, 2, maker_side, 100, 2, 1},
          {MarketUpdateType::ADD, 0, 3, maker_side, outside, 4, 1},
          {MarketUpdateType::TRADE, 0, 2, maker_side, 100, 2, Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 2, maker_side, 100, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::TRADE, 0, 1, maker_side, limit, 3,
           Priority_INVALID},
          {MarketUpdateType::CANCEL, 0, 1, maker_side, limit, Quantity_INVALID,
           Priority_INVALID},
          {MarketUpdateType::ADD, 0, 4, taker_side, limit, 3, 1},
          {MarketUpdateType::CANCEL, 0, 3, maker_side, outside,
           Quantity_INVALID, Priority_INVALID},
      });
}

INSTANTIATE_TEST_SUITE_P(BothSides, BookCoreSideTest,
                         ::testing::Values(Side::BUY, Side::SELL));

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
