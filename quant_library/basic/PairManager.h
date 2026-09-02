#ifndef _PAIR_MANAGER_H
#define _PAIR_MANAGER_H

#include "DataStruct.h"
#include "PositionManager.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "securitymanager.h"

using namespace std;


struct PairOrder {
    int64_t pairId{-1};
    int64_t algoPairId{-1};
    char strategyName[stra::NAME_LEN]{""};
    char baseAsset[stra::ASSET_LEN]{""};
    double targetVolume{0.0};
    stra::TradingType tradingTypeOrder;
    stra::TradingType tradingTypeOffset;
    char activeInstrumentKey[stra::INST_KEY_LEN]{""};
    char activeInstrument[stra::INST_ID_LEN]{""};
    InstType activeInstType;
    ExchangeType activeExchangeType;
    Direction activeDirection;
    OrderType activeOrderType;
    stra::PriceType activePriceType;
    double activePricePct{0.0};
    int activeAccountId{-1};
    double activeTargetPrice{-1.0};
    double activeFrozenPrice{-1.0};
    double activeFrozenVolume{0.0};
    double activeLastPriceOnOrder{-1.0};
    double activeLastVolumeOnOrder{0.0};
    double activeTotalPriceOnOrder{-1.0};
    double activeTotalVolumeOnOrder{0.0};
    unordered_set<int64_t> sActiveOrder;  // 只保存未完成订单的orderId
    md::InstrumentInfo activeInfo;
    bool activePriceTickFlag{false};
    int activePriceTickNum{0};

    char passiveInstrumentKey[stra::INST_KEY_LEN]{""};
    char passiveInstrument[stra::INST_ID_LEN]{""};
    InstType passiveInstType;
    ExchangeType passiveExchangeType;
    Direction passiveDirection;
    OrderType passiveOrderType;
    stra::PriceType passivePriceType;
    double passivePricePct{0.0};
    int passiveAccountId{-1};
    double passiveTargetPrice{-1.0};
    double passiveFrozenPrice{-1.0};
    double passiveFrozenVolume{0.0};
    double passiveLastPriceOnOrder{-1.0};
    double passiveLastVolumeOnOrder{0.0};
    double passiveTotalPriceOnOrder{-1.0};
    double passiveTotalVolumeOnOrder{0.0};
    unordered_set<int64_t> sPassiveOrder;
    md::InstrumentInfo passiveInfo;
    bool passivePriceTickFlag{false};
    int passivePriceTickNum{-1};
    bool rebalanceFlag;
    double pairActiveTotalPrice{0.0};
    double pairTotalVolume{0.0};
    double pairPassiveTotalPrice{0.0};
    double pairPassiveTotalVolume{0.0};
    double pairActiveFeeAmount{0.0};
    double pairPassiveFeeAmount{0.0};
    double activeBidPrice1{0.0};
    double activeBidVolume1{0.0};
    double activeAskPrice1{0.0};
    double activeAskVolume1{0.0};
    double passiveBidPrice1{0.0};
    double passiveBidVolume1{0.0};
    double passiveAskPrice1{0.0};
    double passiveAskVolume1{0.0};

    int status{0}; // 状态: 0开始  1结束
    int64_t createTime{0};
    int64_t updateTime{0};

    double pairTargetSpread{0.0};
    double pairTargetSpreadProfit{0.0};

    double minOrderAmount{5};
    bool reduceOnly{false};

    char pairInstrumentKey[stra::INST_KEY_LEN]{""};

    sm::SecurityManager* smc{nullptr};

    void Init(sm::SecurityManager* s);
    stra::QuantOrder CreateActiveOrder(int64_t strategyOrderId);
    stra::QuantOrder CreateVolumePassiveOrder(int64_t strategyOrderId);
    stra::QuantOrder CreateOrginActiveOrder(int64_t strategyOrderId);
    stra::QuantOrder CreatePassiveOrder(int64_t strategyOrderId, PositionManager* posMgr);
    stra::QuantOrder CreatePassiveOrder(int64_t strategyOrderId);  // rebalance 
    double CalculatePassiveSlippage();
    void UpdatePairOrderByInsertOrder(const stra::QuantOrder& order);
    void UpdatePairOrderByDeleteOrder(const stra::QuantOrder& order);
    void UpdatePairOrderByOrder(const stra::QuantOrder& order);
    void OnTimer();
    string GetStr();
};


class PairOrderManager {
    public:
        PairOrderManager();
        ~PairOrderManager();
        void RecoveryFromFile(string filePath);
        void InsertPairOrderByPairOrder(const PairOrder& pairOrder);
        void DeletePairOrderByPairOrder(const PairOrder& pairOrder);
        PairOrder& SelectPairOrderByPairId(int64_t pairId);
        void DeletePairOrderByPairId(int64_t pairId);
        void UpdatePairOrderByOrder(const stra::QuantOrder& order);
        int GetSizeByOrderType(stra::TradingType tradingType);
        int GetSizeByOrderTypeAndActiveDirection(stra::TradingType tradingType, Direction activeDirection);
        unordered_map<int64_t, PairOrder>& GetAllPairOrders();

    private:
        unordered_map<int64_t, PairOrder> mPairOrder;
};

#endif
