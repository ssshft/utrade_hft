/***
 * 配对交易---单个币对全量运行状态
 * 
 * 
 * PairInfoManager 管理所有对子状态
 * 
 * 
***/

#pragma once

#include "DataStruct.h"

namespace pt {

    enum PairCommandType {
        PairCmd_NONE = 0,
        PairCmd_STOP = 1, // 停止开仓
        PairCmd_CLOSE = 2, // 强制平仓
        PairCmd_RESUME = 3, // 恢复自动
        PairCmd_MODIFY = 4 // 修改参数
    };

    // 异常平仓类型
    enum AbnormalCloseType {
        AbnormalClose_NONE = 0,
        AbnormalClose_ADL = 1,
        AbnormalClose_SREAD_REGRESSION = 2,
        AbnormalClose_FUNDING_ABNORMAL = 3
    };

    struct InstrumentParam {
        double multiple{1.0};
        double minMove{0.0};
        double minVolume{0.0};
        int calcType{0}; //u本位/币本位
    };

    // 价差统计快照
    struct SpreadStats {
        double bidAskUQ; // spreadBidAsk 上分位数
        double bidAskDQ; // spreadBidAsk 下分位数
        double bidBidUQ;
        double bidBidDQ;
        double askBidUQ;
        double askBidDQ;
        double askAskUQ;
        double askAskDQ;
        int count{0};
        double avgDepthVolume{0};
        bool valid{false};

        bool IsValid() {
            return valid && count > 0 && !std::isnan(bidBidUQ);
        }
    };

    // 实时价差快照
    struct RealTimeSpread {
        double spreadBidAsk;
        double spreadBidBid;
        double spreadAskBid;
        double spreadAskAsk;

        double spreadBidAskTema;
        double spreadBidBidTema;
        double spreadAskBidTema;
        double spreadAskAskTema;

        double activeFundingRate;
        double passiveFundingRate;
        int64_t activeFundingRateTime{0};
        int64_t passiveFundingRateTime{0};
        int activeFundingInterval{8};
        int passiveFundingInterval{8};

        double activePriceTema;
        double passivePriceTema;

        int64_t lastGenerateTs{0};
        bool valid;
    };

    // 开平仓触发价差参数
    struct OrderParams {
        double ttOLStartSpread{0.0};
        double ttOLEndSptread{0.0};
        double ttOLStartVolume{0.0};
        double ttOLEndVolume{0.0};
        bool ttOLSwitch{false};

        double ttCLStartSpread{0.0};
        double ttCLEndSptread{0.0};
        double ttCLStartVolume{0.0};
        double ttCLEndVolume{0.0};
        bool ttCLSwitch{false};

        double ttOSStartSpread{0.0};
        double ttOSEndSptread{0.0};
        double ttOSStartVolume{0.0};
        double ttOSEndVolume{0.0};
        bool ttOSSwitch{false};

        double ttCSStartSpread{0.0};
        double ttCSEndSptread{0.0};
        double ttCSStartVolume{0.0};
        double ttCSEndVolume{0.0};
        bool ttCSSwitch{false};


        double mtOLStartSpread{0.0};
        double mtOLEndSptread{0.0};
        double mtOLStartVolume{0.0};
        double mtOLEndVolume{0.0};
        bool mtOLSwitch{false};

        double mtCLStartSpread{0.0};
        double mtCLEndSptread{0.0};
        double mtCLStartVolume{0.0};
        double mtCLEndVolume{0.0};
        bool mtCLSwitch{false};

        double mtOSStartSpread{0.0};
        double mtOSEndSptread{0.0};
        double mtOSStartVolume{0.0};
        double mtOSEndVolume{0.0};
        bool mtOSSwitch{false};

        double mtCSStartSpread{0.0};
        double mtCSEndSptread{0.0};
        double mtCSStartVolume{0.0};
        double mtCSEndVolume{0.0};
        bool mtCSSwitch{false};
    };

    // 异常平仓风险状态
    struct AbnormalCloseState {
        int tier1Times{0};
        int tier2Times{0};
        int tier3Times{0};
        int currentTier{0};
        bool triggered{false};
        int64_t startTime{0};
        double lastPositionValue{0.0};

    };


    // 单个交易对全量运行状态
    struct PairInfo {
        char pairInstrumentKey[stra::INST_KEY_LEN]{""};
        char activeInstrumentKey[stra::INST_KEY_LEN]{""};
        char passiveInstrumentKey[stra::INST_KEY_LEN]{""};
        int activeAccountId{0};
        int passiveAccountId{0};

        InstrumentParam activeParam;
        InstrumentParam passiveParam;

        double pairTotalVolume{0};
        double pairActiveTotalPrice{-1.0};
        double pairPassiveTotalPrice{-1.0};
        double pairPassiveTotalVolume{0.0};
        double positionValue{0.0};

        double activeRealPosition{0.0};
        double passiveRealPosition{0.0};
        double activeAvgPrice{-1.0};
        double passiveAvgPrice{-1.0};

        double floatPnl{0.0};
        double totalPnl{0.0};
        double ttFloatPnl{0.0};
        double ttTotalPnl{0.0};
        double mtFloatPnl{0.0};
        double mtTotalPnl{0.0};

        // 强平与ADL
        double activeFloatPnl{0.0};
        double activeMarkPrice{-1.0};
        double activeLiquidPrice{-1.0};
        double activeAdlRank{0.0};
        double passiveFloatPnl{0.0};
        double passiveMarkPrice{-1.0};
        double passiveLiquidPrice{-1.0};
        double passiveAdlRank{0.0};


        // 0=正常；1=警告；2=危险
        int activeLiquidStatus{0};
        int passiveLiquidStatus{0};
        int activeMarginStatus{0};
        int passiveMarginStatus{0};

        // 价差统计
        SpreadStats largeStats; // 大周期，默认24h
        SpreadStats smallStats; // 小周期，默认1h

        // 实时价差
        RealTimeSpread rtSpread;

        double openSmallSpreadBidBidUQ;
        double openSmallSpreadAskAskDQ;

        // k线统计
        double activeDailyAmount{0.0};
        double passiveDailyAmount{0.0};
        double activeOI{0.0};
        double passiveOI{0.0};
        double activeOIUsdt{0.0};
        double passiveOIUsdt{0.0};
        double activeMeanClose{0.0};
        double passiveMeanClose{0.0};

        // 报单量参数，由SignalGenerator计算
        double ttTargetVolume{0.0};
        double mtTargetVolume{0.0};
        double maxVolume{0.0};
        double minVolume{0.0};
        double maxExposure{0.0};
        double maxLongVolume{0.0};
        double maxShortVolume{0.0};


        bool autoFlag{false}; // true 自动模式
        bool stopFlag{false}; // 停止开仓
        bool closeFlag{false}; // 强制平仓
        bool limitFlag{false}; // 持仓上限已达
        bool errorFlag{false}; // 发生错误
        bool manualFlag{false}; // 手工单

        bool profitSwitch{true}; // 平仓是否需要盈利保护
        double profitPct{0.0001}; // 平仓最低期望利润

        bool reBalance{false}; // 是否需要再平衡
        bool stopMarginTrade{false};

        OrderParams orderParams;

        // 状态时间戳
        int64_t satisfyTime{0}; // 最后满足开仓条件的时间
        int64_t modifyTime{0};  // 最后更新时间
        int64_t lastTinyCloseOnlyScanTime{0}; // 碎单扫描时间

        // 风控状态
        AbnormalCloseState adlClose;
        AbnormalCloseState spreadNoRegression;
        AbnormalCloseState fundingAbnormal;
        int64_t positionExceedThresholdStartTime{0}; // 持仓阈值开始时间
        int64_t spreadNoRegressionStartTime{0};    // 价差不会归开始时间

        char currentAlgoOrderId[stra::ID_LEN]{""};
        bool hasActiveAlgoOrder{false};

        void SetPairKey(const char* key) {
            strncpy(pairInstrumentKey, key, sizeof(pairInstrumentKey) - 1);
        }

        void SetActiveKey(const char* key) {
            strncpy(activeInstrumentKey, key, sizeof(activeInstrumentKey) - 1);
        }

        void SetPassiveKey(const char* key) {
            strncpy(passiveInstrumentKey, key, sizeof(passiveInstrumentKey) - 1);
        }

        bool HasPosition() const {
            return std::abs(pairTotalVolume) > 1e-9;
        }

        bool IsLong() const {
            return pairTotalVolume < -1e-9;
        }

        bool IsShort() const {
            return pairTotalVolume > 1e-9;
        }

        double CalcPositionValue() const {
            if (activeMeanClose <= 0 || std::isnan(activeMeanClose)) {
                return 0.0;
            }

            if (activeParam.calcType == 0) {
                return std::abs(pairTotalVolume) * activeMeanClose * activeParam.multiple;
            }
            else {
                return std::abs(pairTotalVolume) * activeParam.multiple;
            }
        }

    };

}