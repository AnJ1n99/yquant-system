#pragma once

#include <array>
#include <sstream>
#include <string_view>

#include "common/types.h"

namespace trading {

enum class AlgoType { INVALID, RANDOM, MAKER, TAKER };

inline AlgoType StringToAlgoType(std::string_view value) noexcept {
  if (value == "RANDOM") return AlgoType::RANDOM;
  if (value == "MAKER") return AlgoType::MAKER;
  if (value == "TAKER") return AlgoType::TAKER;
  return AlgoType::INVALID;
}

inline const char* AlgoTypeToString(AlgoType type) noexcept {
  switch (type) {
    case AlgoType::RANDOM:
      return "RANDOM";
    case AlgoType::MAKER:
      return "MAKER";
    case AlgoType::TAKER:
      return "TAKER";
    case AlgoType::INVALID:
      return "INVALID";
  }
  return "INVALID";
}

// 策略配置属于交易端；未配置的标的不允许新建订单。
struct RiskCfg {
  common::Quantity max_order_size_ = 0;
  common::Quantity max_position_ = 0;
  double max_loss_ = 0;

  auto toString() const {
    std::ostringstream out;
    out << "RiskCfg{max_order_size:" << max_order_size_
        << " max_position:" << max_position_ << " max_loss:" << max_loss_
        << "}";
    return out.str();
  }
};

struct TradeEngineCfg {
  common::Quantity clip_ = 0;
  double threshold_ = 0;
  RiskCfg risk_cfg_{};

  auto toString() const {
    std::ostringstream out;
    out << "TradeEngineCfg{clip:" << clip_ << " threshold:" << threshold_ << " "
        << risk_cfg_.toString() << "}";
    return out.str();
  }
};

using TradeEngineCfgHashMap = std::array<TradeEngineCfg, common::kMaxSymbols>;

}  // namespace trading
