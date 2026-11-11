// Definitions for the stubbed-out pieces of the algo-order execution side.
// PairTradingContext.cpp does `new AlgoPairOrder()` / `new AlgoRebalanceOrder()`;
// nothing else from the execution side is linked in (the tests drive the strategy
// half only).
#include "basic/AlgoPairOrder.h"
#include "basic/AlgoRebalanceOrder.h"

// 与真实 AlgoPairOrder::AlgoPairOrder()（AlgoPairOrder.cpp:9）一致：algoType 是 PairTrading。
// ⚠️ 这条以前是 `= default`，能过是因为 BuildAlgoOrderJson 里**自己又写了一遍** algoType；
//    后来生产代码把那行删掉、改成只靠构造函数（commit 2c96237），桩没跟上 ⇒ A1 直接红。
//    教训与 isExposureRebalance 那次一样：桩必须与真实构造函数**逐字段**对齐，
//    生产代码"删掉一行、改由构造函数负责"时，桩不会报错，只会让断言变成假绿/假红。
AlgoPairOrder::AlgoPairOrder() : BaseAlgoOrder() {
    algoType = stra::AlgoType_PairTrading;
}

// 与真实 AlgoRebalanceOrder::AlgoRebalanceOrder()（AlgoRebalanceOrder.cpp:8-11）一致：
// algoType 是 Rebalance、activeTrade 初始化为 -1（"尚未确定主动腿方向"）。
AlgoRebalanceOrder::AlgoRebalanceOrder() : BaseAlgoOrder() {
    activeTrade = -1;
    algoType = stra::AlgoType_Rebalance;
}
