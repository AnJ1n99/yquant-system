#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>

#include "../exchange/matcher/matching_engine.h"

namespace {

using common::Side;
using exchange::ClientResponseType;
using exchange::MarketUpdateType;
using exchange::PriceBand;

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

// ---------------------------------------------------------------------------
// 用例只保留一个方向维度：同一组断言必须对买卖两侧都成立，
// 因此用例体内不允许出现方向相关的分支。
// 不适用的消息字段一律为零——这是新协议约定，期望消息按全字段逐字节核对。
// ---------------------------------------------------------------------------

std::string SideParamName(const ::testing::TestParamInfo<Side>& info) {
  return info.param == Side::BUY ? "Buy" : "Sell";
}

// 非零基价、非单位步长：价格与 tick 不再数值相等（1020 -> tick 4）。
constexpr PriceBand kScaledPriceBand{.base_price = 1000, .tick_size = 5};

class BookCoreTest : public ::testing::TestWithParam<Side> {
 protected:
  void SetUp() override { Build(exchange::kDefaultPriceBand); }

  /// 用给定价格带重建订单簿。
  ///
  /// @param band 新的价格带。
  void Build(const PriceBand& band) {
    book_ = std::make_unique<exchange::BookCore>(0, band, responses_, updates_);
  }

  exchange::ClientResponseLFQueue responses_{64};
  exchange::MatchingEngineMarketUpdateLFQueue updates_{64};
  std::unique_ptr<exchange::BookCore> book_;
};

TEST_P(BookCoreTest, AddAndCancel) {
  book_->Add(1, 10, Side::BUY, 100, 7);
  book_->Cancel(1, 10);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, Side::BUY, 100, 0, 7},
          {ClientResponseType::CANCELED, 1, 0, 10, 1, Side::BUY, 100, 0, 7},
      });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, Side::BUY, 100, 7, 1},
                     {MarketUpdateType::CANCEL, 0, 1, Side::BUY, 100, 0, 0},
                 });
}

TEST_P(BookCoreTest, UnknownCancelOnlyEmitsRejection) {
  book_->Cancel(1, 10);

  ExpectMessages(responses_, {
                                 {ClientResponseType::CANCEL_REJECTED, 1, 0, 10,
                                  0, Side{}, 0, 0, 0},
                             });
  ExpectMessages(updates_, {});
}

TEST_P(BookCoreTest, PartialThenFullFill) {
  Build(kScaledPriceBand);
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 1025 : 1015;
  book_->Add(1, 10, maker_side, 1020, 10);
  book_->Add(2, 20, taker_side, limit, 4);
  book_->Add(2, 21, taker_side, limit, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 1020, 0, 10},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, taker_side, limit, 0, 4},
          {ClientResponseType::FILLED, 2, 0, 20, 2, taker_side, 1020, 4, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 1020, 4, 6},
          {ClientResponseType::ACCEPTED, 2, 0, 21, 3, taker_side, limit, 0, 6},
          {ClientResponseType::FILLED, 2, 0, 21, 3, taker_side, 1020, 6, 0},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 1020, 6, 0},
      });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, maker_side, 1020, 10, 1},
                     {MarketUpdateType::TRADE, 0, 1, maker_side, 1020, 4, 0},
                     {MarketUpdateType::MODIFY, 0, 1, maker_side, 1020, 6, 1},
                     {MarketUpdateType::TRADE, 0, 1, maker_side, 1020, 6, 0},
                     {MarketUpdateType::CANCEL, 0, 1, maker_side, 1020, 0, 0},
                 });
}

TEST_P(BookCoreTest, RemainderBecomesRestingOrder) {
  Build(kScaledPriceBand);
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 1025 : 1015;
  book_->Add(1, 10, maker_side, 1020, 3);
  book_->Add(2, 20, taker_side, limit, 5);
  book_->Cancel(2, 20);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 1020, 0, 3},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, taker_side, limit, 0, 5},
          {ClientResponseType::FILLED, 2, 0, 20, 2, taker_side, 1020, 3, 2},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 1020, 3, 0},
          {ClientResponseType::CANCELED, 2, 0, 20, 2, taker_side, limit, 0, 2},
      });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, maker_side, 1020, 3, 1},
                     {MarketUpdateType::TRADE, 0, 1, maker_side, 1020, 3, 0},
                     {MarketUpdateType::CANCEL, 0, 1, maker_side, 1020, 0, 0},
                     {MarketUpdateType::ADD, 0, 2, taker_side, limit, 2, 1},
                     {MarketUpdateType::CANCEL, 0, 2, taker_side, limit, 0, 0},
                 });
}

TEST_P(BookCoreTest, FillsFollowMakerFifoOrder) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  book_->Add(1, 10, maker_side, 100, 3);
  book_->Add(1, 11, maker_side, 100, 4);
  book_->Add(2, 20, taker_side, 100, 6);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 100, 0, 3},
          {ClientResponseType::ACCEPTED, 1, 0, 11, 2, maker_side, 100, 0, 4},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 3, taker_side, 100, 0, 6},
          {ClientResponseType::FILLED, 2, 0, 20, 3, taker_side, 100, 3, 3},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, 100, 3, 0},
          {ClientResponseType::FILLED, 2, 0, 20, 3, taker_side, 100, 3, 0},
          {ClientResponseType::FILLED, 1, 0, 11, 2, maker_side, 100, 3, 1},
      });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, maker_side, 100, 3, 1},
                     {MarketUpdateType::ADD, 0, 2, maker_side, 100, 4, 2},
                     {MarketUpdateType::TRADE, 0, 1, maker_side, 100, 3, 0},
                     {MarketUpdateType::CANCEL, 0, 1, maker_side, 100, 0, 0},
                     {MarketUpdateType::TRADE, 0, 2, maker_side, 100, 3, 0},
                     {MarketUpdateType::MODIFY, 0, 2, maker_side, 100, 1, 2},
                 });
}

TEST_P(BookCoreTest, EmptyBookRestsOrder) {
  const Side side = GetParam();
  book_->Add(1, 10, side, 100, 7);

  ExpectMessages(responses_, {{ClientResponseType::ACCEPTED, 1, 0, 10, 1, side,
                               100, 0, 7}});
  ExpectMessages(updates_, {{MarketUpdateType::ADD, 0, 1, side, 100, 7, 1}});
}

TEST_P(BookCoreTest, NonCrossingOrderLeavesMakerUntouched) {
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 99 : 101;
  book_->Add(1, 10, maker_side, 100, 3);
  book_->Add(2, 20, taker_side, limit, 5);
  book_->Cancel(1, 10);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, 100, 0, 3},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 2, taker_side, limit, 0, 5},
          {ClientResponseType::CANCELED, 1, 0, 10, 1, maker_side, 100, 0, 3},
      });
  ExpectMessages(updates_,
                 {
                     {MarketUpdateType::ADD, 0, 1, maker_side, 100, 3, 1},
                     {MarketUpdateType::ADD, 0, 2, taker_side, limit, 5, 1},
                     {MarketUpdateType::CANCEL, 0, 1, maker_side, 100, 0, 0},
                 });
}

TEST_P(BookCoreTest, MultipleLevelsFollowPricePriorityAndStopAtLimit) {
  Build(kScaledPriceBand);
  const Side taker_side = GetParam();
  const Side maker_side = taker_side == Side::BUY ? Side::SELL : Side::BUY;
  const common::Price limit = taker_side == Side::BUY ? 1025 : 1015;
  const common::Price outside = taker_side == Side::BUY ? 1035 : 1005;
  // 先放较差价位，验证撮合按价格而非跨价位的到达顺序进行。
  book_->Add(1, 10, maker_side, limit, 3);
  book_->Add(1, 11, maker_side, 1020, 2);
  book_->Add(1, 12, maker_side, outside, 4);
  book_->Add(2, 20, taker_side, limit, 8);
  book_->Cancel(1, 12);

  ExpectMessages(
      responses_,
      {
          {ClientResponseType::ACCEPTED, 1, 0, 10, 1, maker_side, limit, 0, 3},
          {ClientResponseType::ACCEPTED, 1, 0, 11, 2, maker_side, 1020, 0, 2},
          {ClientResponseType::ACCEPTED, 1, 0, 12, 3, maker_side, outside, 0,
           4},
          {ClientResponseType::ACCEPTED, 2, 0, 20, 4, taker_side, limit, 0, 8},
          {ClientResponseType::FILLED, 2, 0, 20, 4, taker_side, 1020, 2, 6},
          {ClientResponseType::FILLED, 1, 0, 11, 2, maker_side, 1020, 2, 0},
          {ClientResponseType::FILLED, 2, 0, 20, 4, taker_side, limit, 3, 3},
          {ClientResponseType::FILLED, 1, 0, 10, 1, maker_side, limit, 3, 0},
          {ClientResponseType::CANCELED, 1, 0, 12, 3, maker_side, outside, 0,
           4},
      });
  ExpectMessages(
      updates_, {
                    {MarketUpdateType::ADD, 0, 1, maker_side, limit, 3, 1},
                    {MarketUpdateType::ADD, 0, 2, maker_side, 1020, 2, 1},
                    {MarketUpdateType::ADD, 0, 3, maker_side, outside, 4, 1},
                    {MarketUpdateType::TRADE, 0, 2, maker_side, 1020, 2, 0},
                    {MarketUpdateType::CANCEL, 0, 2, maker_side, 1020, 0, 0},
                    {MarketUpdateType::TRADE, 0, 1, maker_side, limit, 3, 0},
                    {MarketUpdateType::CANCEL, 0, 1, maker_side, limit, 0, 0},
                    {MarketUpdateType::ADD, 0, 4, taker_side, limit, 3, 1},
                    {MarketUpdateType::CANCEL, 0, 3, maker_side, outside, 0, 0},
                });
}

INSTANTIATE_TEST_SUITE_P(BothSides, BookCoreTest,
                         ::testing::Values(Side::BUY, Side::SELL),
                         SideParamName);

TEST(MatchingEngineOutputTest, SymbolsShareOutputQueuesWithoutCrossMatching) {
  exchange::ClientRequestLFQueue requests{64};
  exchange::ClientResponseLFQueue responses{64};
  exchange::MatchingEngineMarketUpdateLFQueue updates{64};
  exchange::MatchingEngine engine{&requests, &responses, &updates};
  const exchange::MatchingEngineClientRequest inputs[] = {
      {exchange::ClientRequestType::NEW, 1, 1, 10, Side::BUY, 100, 3},
      {exchange::ClientRequestType::NEW, 1, 2, 10, Side::SELL, 100, 4},
      {exchange::ClientRequestType::CANCELED, 1, 1, 10, Side{}, 0, 0},
      {exchange::ClientRequestType::CANCELED, 1, 2, 10, Side{}, 0, 0},
  };
  for (const auto& input : inputs) {
    engine.ProcessClientRequest(&input);
  }

  ExpectMessages(
      responses,
      {
          {ClientResponseType::ACCEPTED, 1, 1, 10, 1, Side::BUY, 100, 0, 3},
          {ClientResponseType::ACCEPTED, 1, 2, 10, 1, Side::SELL, 100, 0, 4},
          {ClientResponseType::CANCELED, 1, 1, 10, 1, Side::BUY, 100, 0, 3},
          {ClientResponseType::CANCELED, 1, 2, 10, 1, Side::SELL, 100, 0, 4},
      });
  ExpectMessages(updates,
                 {
                     {MarketUpdateType::ADD, 1, 1, Side::BUY, 100, 3, 1},
                     {MarketUpdateType::ADD, 2, 1, Side::SELL, 100, 4, 1},
                     {MarketUpdateType::CANCEL, 1, 1, Side::BUY, 100, 0, 0},
                     {MarketUpdateType::CANCEL, 2, 1, Side::SELL, 100, 0, 0},
                 });
}

}  // namespace
