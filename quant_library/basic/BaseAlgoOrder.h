#ifndef _BASE_ALGO_ORDER_H
#define _BASE_ALGO_ORDER_H

#include "DataStruct.h"
#include "PositionManager.h"
#include "OrderManager.h"
#include "PairManager.h"
#include "dbp/include.h"
#include "time_util.h"
#include <string>
#include "securitymanager.h"
#include "LarkRebot.h"

// 算法单的角色：**建单时定，之后只读，永不随行为改变**。
// 唯一用途是 AlgoContext.cpp:1376 的结束判据 —— 这一单"平到 0"算不算结束：
//   OPEN_SIGNAL  —— **不算**。开仓单建出来那一刻仓位就是 0（还没成交），
//                   空仓是它的常态之一，结束了就再也开不了了。
//   CLOSE_SIGNAL —— 算。它是平仓信号建的，而 CL/CS 信号要求 IsLong/IsShort
//                   （SignalGenerator.cpp:308/313/328/333）⇒ 建单时必有仓位，
//                   "平到 0"对它只可能是终态，不可能是中间态。
//   RISK_CLOSE   —— 算，理由同上。
// 它**不决定**这一单能不能开仓。能不能开仓只看 4 个开仓开关（orderParams + 各道闸门）；
// 平仓单在运行期能不能回补，也只看那 4 个开关，跟本字段无关。
//
// ⚠️ 1376 是所有 algoType 共用的判据（Rebalance/Fishing 也走），所以**只有 PairTrading 的
//    建单路径写这个字段**，其它算法单必须保持默认 OPEN_SIGNAL，否则会被提前判成 FILLED。
enum class AlgoRole {
    OPEN_SIGNAL  = 0,   // 开仓信号（OL/OS）建的
    CLOSE_SIGNAL = 1,   // 平仓信号（CL/CS）建的
    RISK_CLOSE   = 2,   // 风控强平建的
};

struct BaseAlgoOrder {
    // char sccId[stra::INST_ID_LEN]{""};
    // char toAec[stra::NAME_LEN]{""};
    // char clientOrderId[stra::ID_LEN]{""};

    stra::CommandType commandType{stra::CommandType_MIN};
    int64_t insertTime{0};
    int64_t updateTime{0};
    int64_t cancelOrderTime{0};

    stra::AlgoType algoType{stra::AlgoType_MIN};
    stra::TradingType tradingTypeOrder{stra::MAKER_TAKER};
    char algoStrategyName[stra::NAME_LEN]{""};
    int64_t algoOrderId{0};
    char pairInstrumentKey[stra::INST_KEY_LEN]{""};
    char baseAsset[stra::ASSET_LEN]{""};
    stra::AlgoOrderStatus algoOrderStatus{stra::ALGO_OS_MIN};

    // 建单角色，见文件头 AlgoRole 的注释。只读：建单时写一次，之后任何地方都不许改
    // （尤其不许"反手之后升级成 OPEN_SIGNAL"——角色说的是它**是什么**，不是它**做了什么**）。
    // 不进 DB（Convert.h:88 逐字段拷）、不落盘（SaveToFile 是空实现）⇒ 加字段不动 schema。
    AlgoRole role{AlgoRole::OPEN_SIGNAL};

    char activeInstrumentKey[stra::INST_KEY_LEN]{""};
    int activeAccountId{0};
    stra::DriveType activeDriveType;
    bool activeDepthMakerCheck;
    bool activeDepthTakerCheck;
    stra::CheckType activeDepthMakerCheckType;
    // unordered_map<string, int> mActiveDepthMakerCheckTarget;
    stra::CheckType activeDepthTakerCheckType;
    // unordered_map<string, int> mActiveDepthTakerCheckTarget;
    OrderType activeOrderType;


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
    OrderType passiveOrderType;

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

    md::InstrumentInfo activeInfo;
    md::InstrumentInfo passiveInfo;

    PositionManager posMgrMakerTaker;
    PositionManager posMgrTakerTaker;
    OrderManager orderMgr;
    PairOrderManager pairOrderMgr;

    sm::SecurityManager* smc{nullptr};


    BaseAlgoOrder();
    void Init(sm::SecurityManager* s);
    void InitPositionMgr();
    void Update();
    void UpdateAlgoPairOrderByInsertQuantOrder(const stra::QuantOrder& order);
    void UpdateAlgoPairOrderByDeleteQuantOrder(const stra::QuantOrder& order);
    void UpdateAlgoPairOrderByQuantOrder(const stra::QuantOrder& order);
    virtual void UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder);
    double GetExpectActiveVolume();
    double GetExpectPassiveVolume();
    double GetLockedSpread();
    double GetActiveVolumeByPassiveVolume(double volume, double price);
    void CancelOrderOnSpread(const dbp::DbpData* pdata);
    virtual void PairOrderTrade(PairOrder& pairOrder);
    
    string GetLastestStatusInfo();
    void LoadFromFile(string filePath);
    void SaveToFile();
    string GeneratePubStr();


    virtual PairOrder GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId);
    virtual PairOrder CreatePairOrder(stra::TradingType tradingType);
    virtual PairOrder CreatePairOrder(stra::TradingType tradingType, Direction activeDirection);

    bool passInfoParameterCheck();
};


#endif
