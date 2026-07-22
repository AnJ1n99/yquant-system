#include "matching_engine_side_book.h"

namespace exchange {

MatchingEngineSideBook::MatchingEngineSideBook(
    common::Side side, common::MemPool<MatchingEngineOrder>* orderPool,
    common::MemPool<MatchingEngineOrdersAtPrice>* pricePool)
    : side_(side), order_pool_(orderPool), price_pool_(pricePool) {
  price_levels_.fill(nullptr);
}

MatchingEngineSideBook::~MatchingEngineSideBook() {
  // 释放所有价格档位对象（处理哈希碰撞链表）
  for (size_t priceIdx = 0; priceIdx < price_levels_.size(); ++priceIdx) {
    MatchingEngineOrdersAtPrice* bucket = price_levels_[priceIdx];
    while (bucket != nullptr) {
      MatchingEngineOrdersAtPrice* next = bucket->hash_next;
      price_pool_->deallocate(bucket);
      bucket = next;
    }
    price_levels_[priceIdx] = nullptr;
  }
  best_price_ = nullptr;
}

std::size_t MatchingEngineSideBook::priceToIndex(
    common::Price price) const noexcept {
  return static_cast<std::size_t>(price) % common::kMaxPriceLevels;
}

MatchingEngineOrdersAtPrice* MatchingEngineSideBook::getOrdersAtPrice(
    common::Price price) const noexcept {
  auto* bucket = price_levels_[priceToIndex(price)];
  while (bucket != nullptr) {
    if (bucket->price == price) {
      return bucket;
    }
    bucket = bucket->hash_next;
  }
  return nullptr;
}

common::Priority MatchingEngineSideBook::getNextPriority(
    common::Price price) const noexcept {
  const auto ordersAtPrice = getOrdersAtPrice(price);
  if (ordersAtPrice == nullptr) {
    return 1lu;
  }
  return ordersAtPrice->first_order->prev->priority + 1;
}

void MatchingEngineSideBook::addOrderAtPrice(
    MatchingEngineOrdersAtPrice* ordersAtPrice) noexcept {
  const auto isBid = (side_ == common::Side::BUY);

  // 将价格档位加入哈希表（链地址法处理碰撞）
  const auto idx = priceToIndex(ordersAtPrice->price);
  ordersAtPrice->hash_next = price_levels_[idx];
  price_levels_[idx] = ordersAtPrice;

  // 如果是空链表，直接设为头节点
  if (UNLIKELY(best_price_ == nullptr)) {
    best_price_ = ordersAtPrice;
    ordersAtPrice->prev = ordersAtPrice->next = ordersAtPrice;
  } else {
    // 买单：价格从高到低排序 (best bid是最高价)
    // 卖单：价格从低到高排序 (best ask是最低价)
    auto target = best_price_;
    auto shouldInsertBefore = [&]() {
      return isBid ? (ordersAtPrice->price > target->price)
                   : (ordersAtPrice->price < target->price);
    };

    if (shouldInsertBefore()) {
      target = best_price_->prev;
      best_price_ = ordersAtPrice;
    } else {
      target = best_price_->next;
      while (target != best_price_) {
        if (shouldInsertBefore()) {
          break;
        }
        target = target->next;
      }
      target = target->prev;
    }

    // 插入到target之后
    ordersAtPrice->next = target->next;
    ordersAtPrice->prev = target;
    target->next->prev = ordersAtPrice;
    target->next = ordersAtPrice;
  }
}

void MatchingEngineSideBook::removeOrderAtPrice(common::Price price) noexcept {
  auto ordersAtPrice = getOrdersAtPrice(price);
  if (UNLIKELY(ordersAtPrice == nullptr)) {
    return;
  }

  // 从双向循环链表中移除
  if (UNLIKELY(ordersAtPrice->next == ordersAtPrice)) {
    best_price_ = nullptr;
  } else {
    ordersAtPrice->prev->next = ordersAtPrice->next;
    ordersAtPrice->next->prev = ordersAtPrice->prev;
    if (ordersAtPrice == best_price_) {
      best_price_ = ordersAtPrice->next;
    }
  }
  ordersAtPrice->prev = ordersAtPrice->next = nullptr;

  // 从哈希表中移除
  const auto idx = priceToIndex(price);
  auto* bucket = price_levels_[idx];
  MatchingEngineOrdersAtPrice* prev_bucket = nullptr;

  while (bucket != nullptr) {
    if (bucket->price == price) {
      if (prev_bucket == nullptr) {
        price_levels_[idx] = bucket->hash_next;
      } else {
        prev_bucket->hash_next = bucket->hash_next;
      }
      bucket->hash_next = nullptr;
      break;
    }
    prev_bucket = bucket;
    bucket = bucket->hash_next;
  }

  price_pool_->deallocate(ordersAtPrice);
}

void MatchingEngineSideBook::addOrder(MatchingEngineOrder* order) noexcept {
  if (UNLIKELY(order->price == common::Price_INVALID || order->price < 0)) {
    return;
  }

  auto ordersAtPrice = getOrdersAtPrice(order->price);

  if (ordersAtPrice == nullptr) {
    // 价格档位不存在，创建新的
    order->next = order;
    order->prev = order;

    ordersAtPrice =
        price_pool_->allocate(order->price, order, nullptr, nullptr);

    addOrderAtPrice(ordersAtPrice);
  } else {
    // 价格档位已存在，插入到订单链表尾部（FIFO）
    auto firstOrder = ordersAtPrice->first_order;
    order->prev = firstOrder->prev;
    order->next = firstOrder;
    firstOrder->prev->next = order;
    firstOrder->prev = order;
  }
}

void MatchingEngineSideBook::removeOrder(MatchingEngineOrder* order) noexcept {
  auto ordersAtPrice = getOrdersAtPrice(order->price);
  if (UNLIKELY(ordersAtPrice == nullptr)) {
    return;
  }

  // 从循环链表中移除订单节点
  if (order->next == order) {
    // 链表只有一个节点，清空价位
    ordersAtPrice->first_order = nullptr;
    removeOrderAtPrice(order->price);
  } else {
    order->prev->next = order->next;
    order->next->prev = order->prev;
    // 如果移除的是头节点，更新头指针
    if (ordersAtPrice->first_order == order) {
      ordersAtPrice->first_order = order->next;
    }
  }

  order->prev = nullptr;
  order->next = nullptr;

  // 释放订单到内存池
  order_pool_->deallocate(order);
}

}  // namespace exchange
