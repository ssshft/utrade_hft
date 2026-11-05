#pragma once

#include "BaseAlgoOrder.h"
#include "json/nlohmann/json.hpp"


using namespace std;
using json = nlohmann::json;

// fund_verify移动  get_transfer
// get_transfer 属于AccountManager

struct AlgoRebalanceOrder : public BaseAlgoOrder {
    // 1 = 操作主动腿（报主动腿的单子），0 = 操作被动腿（报被动腿的单子），-1 = 未确定。
    // 它决定三件事：GetTargetPairOrder 拿 GetExpectActiveVolume 还是 GetExpectPassiveVolume、
    // OnSpread 里报 CreateActiveOrder 还是 CreateVolumePassiveOrder、
    // 以及 OnTimer 的结束判据读 pairTotalVolume 还是 pairPassiveTotalVolume。
    int activeTrade;

    // 阶梯终点（= *CLEndVolume / *CSEndVolume 写进去的那个值），**带符号**、
    // 且落在**账本坐标系**里（与 GetExpectActiveVolume 同源）。
    // 风控单的终点不是 0 而是"只平掉超出阈值的部分"之后的残量，所以
    // AlgoContext::OnTimer 的终结判据要读它：|持仓| - |残量| <= minSize 才算到位。
    // 人工建的 Rebalance 单不动这个字段 ⇒ 默认 0 ⇒ 判据退化成原来的 |持仓| < minSize。
    double targetResidualVolume{0.0};

    AlgoRebalanceOrder();
    PairOrder GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId);
    PairOrder CreatePairOrder(stra::TradingType tradingType);
    void PairOrderTrade(PairOrder& pairOrder);
    void UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder);
};
