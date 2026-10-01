#ifndef _UTILITY_H
#define _UTILITY_H

// Stub for quant_library/basic/Utility.h.
//
// 真实那个头还会拖进 fmt / ConcurrentQueue / perf.h / DataStruct.h —— 本套件都不需要。
// 这里只保留被测源码真正用到的两样东西：
//   1. splitString      —— PairInfoManager::Init 拆 pairInstrumentKey 用
//   2. 三个 id 生成器    —— PairTradingContext::BuildAlgoOrderJson 用
//                          GenerateStrategyAlgoPairId()（AlgoContext.cpp:91/241 也调它）
//
// ⚠️ 之前这里只抄了 splitString，于是 PairTradingContext.cpp 报
//    "use of undeclared identifier 'GenerateStrategyAlgoPairId'"。
//    真实头里的三个生成器都是 `crypto::rdtscp()` 一行，照抄即可。
//    改生产代码里的 id 生成方式时记得同步这里（tests/algo_exec 的桩里已经有一份）。
#include <cstdint>
#include <string>
#include <vector>

#include "time_util.h"   // crypto::rdtscp()

using namespace std;

inline void splitString(const string& source, vector<string>& v, const string delimiters = " ") {
    string::size_type lastPos = source.find_first_not_of(delimiters, 0);
    string::size_type pos = source.find_first_of(delimiters, lastPos);
    while (string::npos != pos || string::npos != lastPos) {
        v.push_back(source.substr(lastPos, pos - lastPos));
        lastPos = source.find_first_not_of(delimiters, pos);
        pos = source.find_first_of(delimiters, lastPos);
    }
}

inline int64_t GenerateStrategyOrderId()    { return crypto::rdtscp(); }
inline int64_t GenerateStrategyPairId()     { return crypto::rdtscp(); }
inline int64_t GenerateStrategyAlgoPairId() { return crypto::rdtscp(); }

#endif
