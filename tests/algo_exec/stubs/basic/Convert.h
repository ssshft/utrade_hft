#pragma once
// 真实的 Convert.h 把 QuantOrder / PairOrder / AlgoOrder 写进 contentQueue 落盘。
// 这是纯副作用（不影响任何被测状态），测试里换成 no-op。
// 签名保持与真实一致，避免调用点悄悄改变重载解析。
#include "DataStruct.h"
#include "PairManager.h"
#include "AlgoPairOrder.h"

inline void WriteQuantOrder(const stra::QuantOrder&, const dbp::DbpData*) {}
inline void WritePairOrder(const PairOrder&, const dbp::DbpData*) {}
inline void WriteAlgoOrder(BaseAlgoOrder*) {}
