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
        AbnormalClose_SPREAD_REGRESSION = 2,
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
        // 默认 NaN 而非 0：统计尚未建立时必须让 IsValid() 为 false。
        // 若默认 0，会被当成“有效且分位数为 0”，从而在无统计时放出假信号
        double bidAskUQ{std::nan("")}; // spreadBidAsk 上分位数
        double bidAskDQ{std::nan("")}; // spreadBidAsk 下分位数
        double bidBidUQ{std::nan("")};
        double bidBidDQ{std::nan("")};
        double askBidUQ{std::nan("")};
        double askBidDQ{std::nan("")};
        double askAskUQ{std::nan("")};
        double askAskDQ{std::nan("")};
        int count{0};
        double avgDepthVolume{0};
        bool valid{false};

        bool IsValid() const {
            return valid && count > 0 && !std::isnan(bidBidUQ);
        }
    };

    // 实时价差快照
    struct RealTimeSpread {
        double spreadBidAsk{0.0};
        double spreadBidBid{0.0};
        double spreadAskBid{0.0};
        double spreadAskAsk{0.0};

        double spreadBidAskTema{0.0};
        double spreadBidBidTema{0.0};
        double spreadAskBidTema{0.0};
        double spreadAskAskTema{0.0};

        double activeFundingRate{0.0};
        double passiveFundingRate{0.0};
        int64_t activeFundingRateTime{0};
        int64_t passiveFundingRateTime{0};
        int activeFundingInterval{8};
        int passiveFundingInterval{8};

        double activePriceTema{0.0};
        double passivePriceTema{0.0};

        int64_t lastGenerateTs{0};
        bool valid{false};
    };

    // 开平仓触发价差参数
    struct OrderParams {
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

        // 建仓瞬间的小周期分位数快照（价差不回归风控的基准）。
        // 必须显式初始化成 NaN：PairInfo 是默认初始化的（PairInfoManager.cpp:12），
        // 没有初值拿到的就是栈上垃圾。而两个读取方都把 std::isnan 当"尚未快照"的哨兵
        // （RiskManager::CheckSpreadNoRegression 与 OnAlgoOrderUpdate 的快照判断），
        // 一个非 NaN 的垃圾值会让哨兵失效 —— 快照永不写入，且用垃圾基准去判"是否回归"。
        // 这两个字段是 PairInfo 里唯二原先没有初值的字段。
        double openSmallSpreadBidBidUQ{std::nan("")};
        double openSmallSpreadAskAskDQ{std::nan("")};

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


        bool autoFlag{true}; // true 自动模式
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
        // 最后一次"把参数推给算法单"的时间，等价于祖先的 pair_info['modify_time']，
        // 供 ProcessModify 算改参周期。**不能复用上面的 modifyTime**：
        // PairInfoManager 在 UpdateRtSpread / UpdateOnPosition / UpdateOnAlgoOrder 等
        // 七处都会刷 modifyTime，等于每个价差 tick 刷一次，改参周期永远到不了。
        int64_t algoModifyTime{0};
        int64_t lastTinyCloseOnlyScanTime{0}; // 碎单扫描时间

        // 风控状态
        AbnormalCloseState adlClose;
        AbnormalCloseState spreadNoRegression;
        AbnormalCloseState fundingAbnormal;
        int64_t positionExceedThresholdStartTime{0}; // 持仓阈值开始时间
        int64_t spreadNoRegressionStartTime{0};    // 价差不会归开始时间

        // 当前在跑的算法单是不是 ProcessRisk 发出去的风控强平单。
        // 用途只有一个：让 ProcessRisk 别在报单后的下一个 tick 就把自己刚发的强平单撤掉。
        // ProcessRisk 的写法是"对子被算法单占着就先撤掉它、下一轮再报强平单"，
        // 而强平期间 needForceClose 会一直为 true —— 如果占着它的正是上一轮刚报出去的
        // 那张强平单，这个撤单就把强平单自己撤掉了，强平永远发不出去。
        // ⚠️ 这个标记**不是**强平单的免死金牌：CheckAlgoOrderTimeout 对它照常生效，
        //    长时间不成交一样会被撤掉、再按新档位重报（2026-09-27 确认）。
        // 由 ProcessRisk 在报单成功后置位，PairInfoManager::ClearActiveAlgoOrder 清除。
        bool riskCloseOrderInFlight{false};

        char currentAlgoOrderId[stra::ID_LEN]{""};
        bool hasActiveAlgoOrder{false};

        // ---- 启动对账用：该腿是否已收到过 pubsub::Position 推送 ----
        // ⚠️ 不能用 avgPrice > 0 当"推送到位"的判据：空仓时交易所给的 avgPrice 就是 0，
        //    那样空仓的对子会永远等不到推送到位，启动闸门永远打不开。
        // 运行态标记，不进快照。
        bool activePushArrived{false};
        bool passivePushArrived{false};

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

        // 腿价：与 PairInfoManager::RecalcVolumeParams(:347-354) 用同一套兜底。
        // K 线统计（activeMeanClose）目前没有写入方（UpdateKlineStats 全仓库无调用者），
        // 不退回实时腿价的话本函数恒返回 0 —— CheckTinyClose / CheckADLRisk /
        // CheckFundingAbnormal 三个风控检查会全部失效（风控链路整条是死的）。
        double LegPrice() const {
            double p = activeMeanClose;
            if (std::isnan(p) || p <= 0.0) {
                p = rtSpread.activePriceTema;
            }
            return (std::isnan(p) || p <= 0.0) ? 0.0 : p;
        }

        // 带符号的持仓市值（祖先 cc_pricespread_gb_ltp.py:790/793 的 positionValue）。
        // 符号由 pairTotalVolume 承载（负 = 多，见 IsLong），祖先的资金费公式
        // （report_helper.py:107-109）正是靠这个符号区分多空的，所以不能取绝对值。
        double CalcSignedPositionValue() const {
            if (activeParam.calcType != 0) {
                return pairTotalVolume * activeParam.multiple;
            }

            const double px = LegPrice();
            if (px <= 0.0) {
                return 0.0;
            }

            return pairTotalVolume * px * activeParam.multiple;
        }

        // 市值（无方向）：只用于"金额大小"类门槛（碎单、ADL 持仓阈值）
        double CalcPositionValue() const {
            return std::abs(CalcSignedPositionValue());
        }

    };

}