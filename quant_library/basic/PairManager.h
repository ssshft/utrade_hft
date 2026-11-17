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
    char baseAsset[stra::ASSET_LEN]{"USDT"};
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

    // 被动腿报单"上次被拒"的时间（µs，0 = 从未被拒），只给 PairOrderTrade 的退避用。
    //
    // 为什么需要：被动腿报单被拒之后，AlgoContext::OnOrder 会立刻回到 PairOrderTrade
    // 重发；如果拒绝原因是本地的（连接池满 → no_stream_resources），重发同样立刻失败，
    // 于是形成 0.3ms 级的自旋 —— 实测同一个对子 9ms 内重发了 26 次，把账户
    // 30 笔/秒（strategy.ini maxpersec）的报单配额全烧在注定失败的请求上。
    //
    // 为什么是"被拒时间"而不是"上次尝试时间"：正常对冲是成交驱动的，一次主动腿成交
    // 就该立刻补一笔，不能被限流；只有"刚刚被拒过"才需要压住，否则连正常的成交驱动
    // 补单也会被误伤（也会让测试里连续两次 OnTimer 之间的补单消失）。
    //
    // 只对**被动腿**的拒绝打这个戳（见 AlgoContext::OnOrder）：主动腿被拒必须走完整的
    // PairOrderTrade —— 那里面还有"没有敞口就收口删 pairOrder"的收尾逻辑。
    //
    // 这个字段同时充当"待补发"标记：非 0 表示有一笔被动腿还没补出去。
    // 消费它的是 AlgoContext::OnSpread 的兜底扫描（50ms 级，先清标记再补），
    // 所以"主动腿已经全部成交、没有新成交来触发"的情况也不会拖到 OnTimer 的 1 秒。
    int64_t lastRejectUs{0};

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
