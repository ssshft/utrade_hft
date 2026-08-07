/****
 * 风险控制 - 监控三类异常并触发渐进式平仓
 * 1. ADL风险
 * 2. 价差不回归
 * 3. 资金费率异常
 * 
 * 渐进式平仓逻辑，三档
 * t1: 放弃1%利润，立即尝试，等待N秒
 * t2: 放弃3%利润，再次尝试，等待N秒
 * t3: 放弃5%利润，最终尝试
 * 
 * ****/

 #pragma once

 #include "../basic/PairInfo.h"

 namespace pt {

    struct RiskConfig {
        // ADL 风控
        double adlPositionThresholdUsdt{2000.0};  //持仓超过，才关注
        double adlHighRankThreshold{0.8};  //ADL 分位数超过此值视为高风险
        int64_t positionExceedDuration{4LL * 24 * 3600 * 1000000LL}; //持仓阈值超过4天(微妙)

        // 价差不回归风险
        int64_t spreadNoRegressionDuration{1LL * 24 * 3600 * 1000000LL};

        // 资金费率异常风控（当前资金费率 * 持仓金额超过阈值）
        double fundingAbnormalThreshold{0.005}; // 累计资金费率损失超maxAmount的0.5%
        double fundingMaxAmount{30.0};      // 对应max_amount

        // 碎单
        double tinyCloseThresholdUsdt{25};
        int64_t tinyCloseScanIntervalUs{60LL * 1000000LL};

        // 渐进式平仓等待时间
        int64_t tier1WaitUs{30LL * 60 * 1000000LL}; // 30 min
        int64_t tier2WaitUs{60LL * 60 * 1000000LL};
        int64_t tier3WaitUs{120LL * 60 * 1000000LL};

        // 渐进式平仓放弃利润比例
        double tier1ForgoProfit{0.01};
        double tier2ForgoProfit{0.03};
        double tier3ForgoProfit{0.05};
    };


    struct RiskCheckResult {
        bool hasRisk{false};
        bool needForceClose{false};
        bool isTinyClose{false};

        AbnormalCloseType riskType{AbnormalClose_NONE};
        int targetTier{0};
        double forgoProfit{0.0};

        std::string reason;
    };

    class RiskManager {
    public:
        static RiskManager& Instance() {
            static RiskManager inst;
            return inst;
        }

        void SetConfig(const RiskConfig& cfg) {
            m_cfg = cfg;
        }

        const RiskConfig& GetConfig() const {
            return m_cfg;
        }

        RiskCheckResult CheckRisk(PairInfo& pi, int64_t nowUs) const;

        bool CheckTinyClose(const PairInfo& pi) const;

        void OnAlgoFinished(PairInfo& pi, bool fullyFlat) const;

        double CalcForgoSpread(const PairInfo& pi, int tier) const;

        std::pair<int, bool> GetCurrentTier(const AbnormalCloseState& state, int64_t nowUs) const;

    private:
        RiskManager() = default;

        RiskCheckResult CheckADLRisk(PairInfo& pi, int64_t nowUs) const;

        RiskCheckResult CheckSpreadNoRegression(PairInfo& pi, int64_t nowUs) const;

        RiskCheckResult CheckFundingAbnormal(PairInfo& pi, int64_t nowUs) const;

        void AdvanceTier(AbnormalCloseState& state, int64_t nowUs) const;

        RiskConfig m_cfg;
    };

 }