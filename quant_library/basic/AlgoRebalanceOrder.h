#pragma once

#include "BaseAlgoOrder.h"
#include "json/nlohmann/json.hpp"


using namespace std;
using json = nlohmann::json;

// fund_verify移动  get_transfer
// get_transfer 属于AccountManager

struct AlgoRebalanceOrder : public BaseAlgoOrder {
    int activeTrade;

    AlgoRebalanceOrder();
    PairOrder GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId);
    PairOrder CreatePairOrder(stra::TradingType tradingType);
    void PairOrderTrade(PairOrder& pairOrder, int64_t eventTime);
    void UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder, int64_t eventTime);
};
