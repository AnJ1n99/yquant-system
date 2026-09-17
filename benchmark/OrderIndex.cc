#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <random>
#include <unordered_map>

#include "../exchange/matcher/order_index.h"

namespace {

using common::ClientId;
using common::OrderId;
using common::Side;
using exchange::OrderIndex;
using exchange::OrderNode;

TEST(OrderIndexTest, FindMissingReturnsNullAndEraseMissingIsNoop) {
  OrderIndex index(16);
  EXPECT_EQ(index.Find(1, 10).order, nullptr);
  index.Erase(1, 10);
  EXPECT_EQ(index.Find(1, 10).order, nullptr);
}

TEST(OrderIndexTest, InsertFindErase) {
  OrderIndex index(16);
  OrderNode a;
  OrderNode b;
  index.Insert(1, 10, &a, Side::BUY, 7);
  index.Insert(2, 10, &b, Side::SELL, 9);  // 同订单号、不同客户端是不同的键。
  EXPECT_EQ(index.Find(1, 10).order, &a);
  EXPECT_EQ(index.Find(2, 10).order, &b);
  index.Erase(1, 10);
  EXPECT_EQ(index.Find(1, 10).order, nullptr);
  EXPECT_EQ(index.Find(2, 10).order, &b);
}

TEST(OrderIndexTest, InsertSameKeyOverwrites) {
  OrderIndex index(16);
  OrderNode a;
  OrderNode b;
  index.Insert(1, 10, &a, Side::BUY, 7);
  index.Insert(1, 10, &b, Side::SELL, 9);
  EXPECT_EQ(index.Find(1, 10).order, &b);
  index.Erase(1, 10);
  EXPECT_EQ(index.Find(1, 10).order, nullptr);  // 覆盖后只有一个条目。
}

// 条目同时携带挂单所在的侧与 tick：撤单路径完全依赖它，不再向价位存储
// 反查——价位存储是运行时多态的，无法由价位指针反推它所在的 tick。
TEST(OrderIndexTest, EntryCarriesSideAndTick) {
  OrderIndex index(16);
  OrderNode node;
  index.Insert(3, 42, &node, Side::SELL, 1234);

  const auto entry = index.Find(3, 42);
  EXPECT_EQ(entry.order, &node);
  EXPECT_EQ(entry.side, Side::SELL);
  EXPECT_EQ(entry.tick, 1234);
}

// 小容量下随机插入、删除，以 unordered_map 为参照逐步核对全部键。
// 16 个槽位、最多 8 个在表条目，让探测链频繁碰撞并绕过数组末尾，
// 覆盖回移删除的环形区间判断。
TEST(OrderIndexTest, RandomChurnMatchesOracle) {
  constexpr std::size_t kCapacity = 16;
  constexpr std::size_t kClients = 8;
  constexpr std::size_t kOrders = 4;
  constexpr std::size_t kMaxLive = kCapacity / 2;

  OrderIndex index(kCapacity);
  std::array<OrderNode, kClients * kOrders> nodes{};
  std::unordered_map<std::size_t, OrderNode*> oracle;
  std::mt19937 rng(20250915);
  std::uniform_int_distribution<std::size_t> pick(0, nodes.size() - 1);

  for (int step = 0; step < 20000; ++step) {
    const std::size_t k = pick(rng);
    const ClientId client_id = static_cast<ClientId>(k / kOrders);
    const OrderId client_order_id = k % kOrders;
    if (oracle.contains(k)) {
      index.Erase(client_id, client_order_id);
      oracle.erase(k);
    } else if (oracle.size() < kMaxLive) {
      index.Insert(client_id, client_order_id, &nodes[k], Side::BUY, 0);
      oracle[k] = &nodes[k];
    }
    for (std::size_t j = 0; j < nodes.size(); ++j) {
      const auto it = oracle.find(j);
      ASSERT_EQ(index.Find(static_cast<ClientId>(j / kOrders), j % kOrders).order,
                it == oracle.end() ? nullptr : it->second)
          << "step " << step << " key " << j;
    }
  }
}

// 按节点池容量装满索引：最大负载 0.5 下探测必须终止且全部可查回。
TEST(OrderIndexTest, HoldsPoolCapacityAtHalfLoad) {
  OrderIndex index(2 * common::kMaxOrderIds);
  OrderNode node;
  for (OrderId id = 0; id < common::kMaxOrderIds; ++id) {
    index.Insert(7, id, &node, Side::BUY, 0);
  }
  for (OrderId id = 0; id < common::kMaxOrderIds; ++id) {
    ASSERT_EQ(index.Find(7, id).order, &node);
  }
  for (OrderId id = 0; id < common::kMaxOrderIds; id += 2) {
    index.Erase(7, id);
  }
  for (OrderId id = 0; id < common::kMaxOrderIds; ++id) {
    ASSERT_EQ(index.Find(7, id).order, (id % 2 == 0) ? nullptr : &node);
  }
}

}  // namespace
