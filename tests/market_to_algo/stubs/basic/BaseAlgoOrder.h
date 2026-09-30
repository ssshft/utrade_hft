#ifndef _BASE_ALGO_ORDER_H
#define _BASE_ALGO_ORDER_H

// Stub for quant_library/basic/BaseAlgoOrder.h.
//
// The real header pulls PositionManager.h / OrderManager.h / PairManager.h /
// dbp/include.h / time_util.h / securitymanager.h and declares ~30 methods. The
// strategy side only *writes fields* on the freshly built algo order, so this stub
// keeps the field set identical (same names, same types, same order) and replaces the
// four manager members with opaque placeholders. Field order matters: it is the
// contract BuildAlgoOrderJson writes against, and the assertions in the suite read
// the same names back.
//
// Every member is value-initialised here (the real header leaves many of them
// indeterminate). That only makes the *initial* state deterministic; it does not
// change which fields BuildAlgoOrderJson writes, which is what the tests assert on.

#include "DataStruct.h"
#include "PositionManager.h"
#include "OrderManager.h"
#include "PairManager.h"
#include "dbp/include.h"
#include "time_util.h"
#include "securitymanager.h"
#include <string>

struct BaseAlgoOrder {
    stra::CommandType commandType{stra::CommandType_MIN};
    int64_t insertTime{0};
    int64_t updateTime{0};
    int64_t cancelOrderTime{0};

    stra::AlgoType algoType{stra::AlgoType_MIN};
    char algoStrategyName[stra::NAME_LEN]{""};
    int64_t algoOrderId{0};
    char pairInstrumentKey[stra::INST_KEY_LEN]{""};
    char baseAsset[stra::ASSET_LEN]{""};
    stra::AlgoOrderStatus algoOrderStatus{stra::ALGO_OS_MIN};

    char activeInstrumentKey[stra::INST_KEY_LEN]{""};
    int activeAccountId{0};
    stra::DriveType activeDriveType{stra::DriveType_MIN};
    bool activeDepthMakerCheck{false};
    bool activeDepthTakerCheck{false};
    stra::CheckType activeDepthMakerCheckType{stra::CheckType_MIN};
    stra::CheckType activeDepthTakerCheckType{stra::CheckType_MIN};
    OrderType activeOrderType{OT_MIN};

    char passiveInstrumentKey[stra::INST_KEY_LEN]{""};
    double passivePriceTakerPct{0.0};
    int passiveAccountId{0};
    stra::DriveType passiveDriveType{stra::DriveType_MIN};
    bool passiveDepthMakerCheck{false};
    bool passiveDepthTakerCheck{false};
    stra::CheckType passiveDepthMakerCheckType{stra::CheckType_MIN};
    stra::CheckType passiveDepthTakerCheckType{stra::CheckType_MIN};
    OrderType passiveOrderType{OT_MIN};

    double passiveVolumePct{0.0};

    int64_t activeMakerCancelOrderTime{0};
    int64_t activeTakerCancelOrderTime{0};
    int64_t passiveMakerCancelOrderTime{0};
    int64_t passiveTakerCancelOrderTime{0};
    double activePassiveCancelOrderPct{0.0};
    double activeMakerCancelOrderPct{0.0};
    double activeTakerCancelOrderPct{0.0};
    double passiveMakerCancelOrderPct{0.0};
    double passiveTakerCancelOrderPct{0.0};

    double activeMakerFeeRate{0.0};
    double activeTakerFeeRate{0.0};
    double passiveMakerFeeRate{0.0};
    double passiveTakerFeeRate{0.0};
    double activeTakerSlippage{0.0};
    double activeMakerSlippage{0.0};
    double passiveTakerSlippage{0.0};
    double passiveMakerSlippage{0.0};

    bool activePriceTickFlag{false};
    int activePriceTickNum{0};
    bool passivePriceTickFlag{false};
    int passivePriceTickNum{0};

    double pairActiveTotalPrice{0.0};
    double pairTotalVolume{0.0};
    double pairPassiveTotalPrice{0.0};
    double pairPassiveTotalVolume{0.0};

    double activePriceTakerPct{0.0};
    double activePriceMakerPct{0.0};
    double passivePriceMakerPct{0.0};

    double makerTakerFs{0.0};
    double takerTakerFs{0.0};
    double maxMTOrderSize{0.0};
    double maxTTOrderSize{0.0};

    stra::TargetSpredPrice targetSpreadType{stra::TargetSpredPrice_MIN};
    stra::ActiveVolumeCalcualteType activeVolumeCalcualteType{stra::ActiveVolumeCalcualteType_MIN};

    double ttTargetVolume{0.0};
    double mtTargetVolume{0.0};
    double minVolume{0.0};

    bool profitSwitch{false};
    double profitPct{0.0};

    double ttOLStartSpread{0.0};
    double ttOLEndSpread{0.0};
    double ttOLStartVolume{0.0};
    double ttOLEndVolume{0.0};
    bool ttOLSwitch{false};
    double ttCLStartSpread{0.0};
    double ttCLEndSpread{0.0};
    double ttCLStartVolume{0.0};
    double ttCLEndVolume{0.0};
    bool ttCLSwitch{false};
    double ttOSStartSpread{0.0};
    double ttOSEndSpread{0.0};
    double ttOSStartVolume{0.0};
    double ttOSEndVolume{0.0};
    bool ttOSSwitch{false};
    double ttCSStartSpread{0.0};
    double ttCSEndSpread{0.0};
    double ttCSStartVolume{0.0};
    double ttCSEndVolume{0.0};
    bool ttCSSwitch{false};
    double mtOLStartSpread{0.0};
    double mtOLEndSpread{0.0};
    double mtOLStartVolume{0.0};
    double mtOLEndVolume{0.0};
    bool mtOLSwitch{false};
    double mtCLStartSpread{0.0};
    double mtCLEndSpread{0.0};
    double mtCLStartVolume{0.0};
    double mtCLEndVolume{0.0};
    bool mtCLSwitch{false};
    double mtOSStartSpread{0.0};
    double mtOSEndSpread{0.0};
    double mtOSStartVolume{0.0};
    double mtOSEndVolume{0.0};
    bool mtOSSwitch{false};
    double mtCSStartSpread{0.0};
    double mtCSEndSpread{0.0};
    double mtCSStartVolume{0.0};
    double mtCSEndVolume{0.0};
    bool mtCSSwitch{false};

    bool mtRebalanceSwitch{false};
    bool ttRebalanceSwitch{false};
    bool mtRebalanceFlag{false};
    bool ttRebalanceFlag{false};

    bool mtPriceTrendProtectFlag{false};
    bool ttPriceTrendProtectFlag{false};

    bool isManual{false};
    int64_t tradesDelayThreshold{0};

    int64_t systemDelayTimeSpan{0};
    int64_t exchangeDelayTimeSpan{0};
    bool systemDelayFlag{false};
    bool exchangeDelayFlag{false};
    bool fundVerifyFailedFlag{false};
    double mtSlipage{0.0};
    double ttSlipage{0.0};
    bool mtSlipageFlag{false};
    bool ttSlipageFlag{false};

    double mtSpread{0.0};
    double ttSpread{0.0};
    bool mtSpreadFlag{false};
    double ttSpreadFlag{0.0};

    double minOrderAmount{0.0};

    md::InstrumentInfo activeInfo;
    md::InstrumentInfo passiveInfo;

    PositionManager posMgrMakerTaker;
    PositionManager posMgrTakerTaker;
    OrderManager orderMgr;
    PairOrderManager pairOrderMgr;

    sm::SecurityManager* smc{nullptr};

    BaseAlgoOrder() = default;
};

#endif
