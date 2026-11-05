// Definitions for the stubbed-out pieces of the algo-order execution side.
// PairTradingContext.cpp does `new AlgoPairOrder()` / `new AlgoRebalanceOrder()`;
// nothing else from the execution side is linked in (the tests drive the strategy
// half only).
#include "basic/AlgoPairOrder.h"
#include "basic/AlgoRebalanceOrder.h"

AlgoPairOrder::AlgoPairOrder() = default;

// 与真实 AlgoRebalanceOrder::AlgoRebalanceOrder()（AlgoRebalanceOrder.cpp:8-11）一致：
// algoType 是 Rebalance、activeTrade 初始化为 -1（"尚未确定主动腿方向"）。
AlgoRebalanceOrder::AlgoRebalanceOrder() : BaseAlgoOrder() {
    activeTrade = -1;
    algoType = stra::AlgoType_Rebalance;
}
