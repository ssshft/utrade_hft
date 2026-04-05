#ifndef _ALGO_PAIR_ORDER_H
#define _ALGO_PAIR_ORDER_H

#include "BaseAlgoOrder.h"
#include "json/nlohmann/json.hpp"


using namespace std;
using json = nlohmann::json;

// fund_verify移动  get_transfer
// get_transfer 属于AccountManager

struct AlgoPairOrder : public BaseAlgoOrder{
    AlgoPairOrder();
    PairOrder GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId);
    PairOrder CreatePairOrder(stra::TradingType tradingType);
};


#endif