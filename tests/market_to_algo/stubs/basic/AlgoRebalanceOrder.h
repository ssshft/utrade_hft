#ifndef _ALGO_REBALANCE_ORDER_H
#define _ALGO_REBALANCE_ORDER_H

// Stub for quant_library/basic/AlgoRebalanceOrder.h.
//
// 策略侧从 2026-11-01 起会在 ProcessExposureRebalance 里 `new AlgoRebalanceOrder()`，
// 所以这一套件必须有一个同名头文件，否则 PairTradingContext.cpp 找不到它。
//
// 真实头多出来的东西这里一个都不要：它声明了 GetTargetPairOrder / CreatePairOrder /
// PairOrderTrade / UpdateAlgoPairOrderByPairOrder 四个方法，但那些只在执行侧
// （AlgoContext / BaseAlgoOrder::PairOrderTrade）被调用，本套件不链接执行侧。
// 这里只保留**策略侧会写到的字段**，名字和类型必须与真实头一致：
//   activeTrade          —— 操作哪条腿
//   isExposureRebalance  —— 这张单是不是"只平超出阈值的部分"
//   targetResidualVolume —— 目标残量（该腿自己的单位、带符号）
// 少写一个的后果是编译期就报错（好事），多写一个的后果是用例断言了不存在的东西。
//
// 构造函数的定义在 stubs.cpp（与 AlgoPairOrder 同一处），行为对齐真实 ctor：
// algoType 写成 AlgoType_Rebalance、activeTrade 初始化为 -1。

#include "BaseAlgoOrder.h"

struct AlgoRebalanceOrder : public BaseAlgoOrder {
    int activeTrade;
    bool isExposureRebalance{false};
    double targetResidualVolume{0.0};

    AlgoRebalanceOrder();
};

#endif
