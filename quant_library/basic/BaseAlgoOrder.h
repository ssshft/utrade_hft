#ifndef _BASE_ALGO_ORDER_H
#define _BASE_ALGO_ORDER_H

#include "DataStruct.h"
#include "PositionManager.h"
#include "OrderManager.h"
#include "PairManager.h"
#include "BasicInfoMgr.h"
#include <string>


struct BaseAlgoOrder {
    char sccId[stra::INST_ID_LEN]{""};
    char toAec[stra::NAME_LEN]{""};
    char clientOrderId[stra::ID_LEN]{""};
    stra::CommandType commandType{stra::CommandType_MIN};
    int64_t insertTime{0};
    int64_t updateTime{0};
    int64_t cancelOrderTime{0};

    stra::AlgoType algoType{stra::AlgoType_MIN};
    char algoStrategyName[stra::NAME_LEN]{""};
    int64_t algoOrderId{0};
    char pairInstrumentKey[stra::INST_KEY_LEN]{""};
    char baseAsset[stra::ASSET_LEN]{""};
    stra::OrderStatus algoOrderStatus{stra::OrderStatus_MIN};

    char activeInstrumentKey[stra::INST_KEY_LEN]{""};
    int activeAccountId{0};
    stra::DriveType activeDriveType;
    bool activeDepthMakerCheck;
    bool activeDepthTakerCheck;
    stra::CheckType activeDepthMakerCheckType;
    // unordered_map<string, int> mActiveDepthMakerCheckTarget;
    stra::CheckType activeDepthTakerCheckType;
    // unordered_map<string, int> mActiveDepthTakerCheckTarget;
    stra::OrderType activeOrderType;


    char passiveInstrumentKey[stra::INST_KEY_LEN];
    double passivePriceTakerPct;
    int passiveAccountId;
    stra::DriveType passiveDriveType;
    bool passiveDepthMakerCheck;
    bool passiveDepthTakerCheck;
    stra::CheckType passiveDepthMakerCheckType;
    // unordered_map<string, double> mPassiveDepthMakerCheckTarget;  // 固定参数
    stra::CheckType passiveDepthTakerCheckType;
    // unordered_map<string, double> mPassiveDepthTakerCheckTarget;
    stra::OrderType passiveOrderType;

    double passiveVolumePct;

    int64_t activeMakerCancelOrderTime;
    int64_t activeTakerCancelOrderTime;
    int64_t passiveMakerCancelOrderTime;
    int64_t passiveTakerCancelOrderTime;
    double activePassiveCancelOrderPct;
    double activeMakerCancelOrderPct;
    double activeTakerCancelOrderPct;
    double passiveMakerCancelOrderPct;
    double passiveTakerCancelOrderPct;

    double activeMakerFeeRate;
    double activeTakerFeeRate;
    double passiveMakerFeeRate;
    double passiveTakerFeeRate;
    double activeTakerSlippage;
    double activeMakerSlippage;
    double passiveTakerSlippage;
    double passiveMakerSlippage;

    bool activePriceTickFlag;
    int activePriceTickNum;

    bool passivePriceTickFlag;
    int passivePriceTickNum;

    double pairActiveTotalPrice;
    double pairTotalVolume;
    double pairPassiveTotalPrice;
    double pairPassiveTotalVolume;

    double activePriceTakerPct;
    double activePriceMakerPct;

    double passivePriceMakerPct;


    double makerTakerFs;
    double takerTakerFs;
    double maxMTOrderSize;
    double maxTTOrderSize;

    stra::TargetSpredPrice targetSpreadType;

    stra::ActiveVolumeCalcualteType activeVolumeCalcualteType;

    double ttTargetVolume;
    double mtTargetVolume;

    double minVolume;

    bool profitSwitch;
    double profitPct;

    double ttOLStartSpread;
    double ttOLEndSpread;
    double ttOLStartVolume;
    double ttOLEndVolume;
    bool ttOLSwitch;
    double ttCLStartSpread;
    double ttCLEndSpread;
    double ttCLStartVolume;
    double ttCLEndVolume;
    bool ttCLSwitch;
    double ttOSStartSpread;
    double ttOSEndSpread;
    double ttOSStartVolume;
    double ttOSEndVolume;
    bool ttOSSwitch;
    double ttCSStartSpread;
    double ttCSEndSpread;
    double ttCSStartVolume;
    double ttCSEndVolume;
    bool ttCSSwitch;
    double mtOLStartSpread;
    double mtOLEndSpread;
    double mtOLStartVolume;
    double mtOLEndVolume;
    bool mtOLSwitch;
    double mtCLStartSpread;
    double mtCLEndSpread;
    double mtCLStartVolume;
    double mtCLEndVolume;
    bool mtCLSwitch;
    double mtOSStartSpread;
    double mtOSEndSpread;
    double mtOSStartVolume;
    double mtOSEndVolume;
    bool mtOSSwitch;
    double mtCSStartSpread;
    double mtCSEndSpread;
    double mtCSStartVolume;
    double mtCSEndVolume;
    bool mtCSSwitch;

    bool mtRebalanceSwitch; // 是否支持rebalance模式的创建被动腿订单
    bool ttRebalanceSwitch; // 是否支持rebalance模式的创建被动腿订单

    bool mtRebalanceFlag; // 当前是否进行rebalance模式的创建被动腿订单
    bool ttRebalanceFlag; // 当前是否进行rebalance模式的创建被动腿订单

    bool mtPriceTrendProtectFlag;  // 价格短期形成趋势是否停止不利交易
    bool ttPriceTrendProtectFlag;  // 价格短期形成趋势是否停止不利交易

    bool isManual;
    int64_t tradesDelayThreshold;

    // 影响主动腿报单, 若干秒重置一次
    int64_t systemDelayTimeSpan;
    int64_t exchangeDelayTimeSpan;
    bool systemDelayFlag;
    bool exchangeDelayFlag;
    bool fundVerifyFailedFlag;
    double mtSlipage;
    double ttSlipage;
    bool mtSlipageFlag;
    bool ttSlipageFlag;

    double mtSpread;
    double ttSpread;
    bool mtSpreadFlag;
    double ttSpreadFlag;

    double minOrderAmount;

    stra::InstrumentInfo activeInfo;
    stra::InstrumentInfo passiveInfo;

    PositionManager posMgrMakerTaker;
    PositionManager posMgrTakerTaker;
    OrderManager orderMgr;
    PairOrderManager pairOrderMgr;


    BaseAlgoOrder();
    void Init();
    void InitPositionMgr();
    void Update();
    void UpdateAlgoPairOrderByInsertQuantOrder(const stra::QuantOrder& order);
    void UpdateAlgoPairOrderByDeleteQuantOrder(const stra::QuantOrder& order, int64_t eventTime);
    void UpdateAlgoPairOrderByQuantOrder(const stra::QuantOrder& order, int64_t eventTime);
    void UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder, int64_t eventTime);
    double GetExpectActiveVolume();
    double GetExpectPassiveVolume();
    double GetLockedSpread();
    double GetActiveVolumeByPassiveVolume(double volume, double price);
    void CancelOrderOnSpread(const stra::QuantSpread& spread, int64_t eventTime);
    virtual void PairOrderTrade(PairOrder& pairOrder, int64_t eventTime);
    
    string GetLastestStatusInfo();
    void LoadFromFile(string filePath);
    void SaveToFile();
    string GeneratePubStr();
    string GeneratePubStrOnUpdate();


    virtual PairOrder GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId);
    virtual PairOrder CreatePairOrder(stra::TradingType tradingType);
    virtual PairOrder CreatePairOrder(stra::TradingType tradingType, stra::Direction activeDirection);

    bool passInfoParameterCheck();
};


#endif
