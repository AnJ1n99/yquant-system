#include "risk_manager.h"

namespace trading {
  using namespace common;
  RiskManager::RiskManager(const PositionKeeper *position_keeper, const TradeEngineCfgHashMap &ticker_cfg) {
    for (SymbolId i = 0; i < kMaxSymbols; ++i) {
      ticker_risk_.at(i).position_info_ = position_keeper->getPositionInfo(i);
      ticker_risk_.at(i).risk_cfg_ = ticker_cfg[i].risk_cfg_;
    }
  }
}
