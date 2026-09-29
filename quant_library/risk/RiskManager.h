/****
 * 风险控制 - 监控三类异常并触发渐进式平仓
 * 1. ADL风险
 * 2. 价差不回归
 * 3. 资金费率异常
 * 
 * 渐进式平仓逻辑，三档（让利幅度是**绝对价差**，量纲说明见 RiskConfig）
 * t1: 让利 tier1ForgoProfit，立即尝试，等 tier1WaitUs
 * t2: 让利 tier2ForgoProfit，再次尝试，等 tier1WaitUs + tier2WaitUs
 * t3: 让利 tier3ForgoProfit，之后**持续以 tier3 重试**直到平掉
 *     （不再升级，也不再停手 —— 否则 tier3 一次没成交就永久卡住）
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
        // 最短持有期：持仓不足此值一律不触发，且**不启动**"连续未回归"计时。
        // 建仓瞬间（以及建仓中的部分成交阶段）价差必然还没回归，若从这里就开始跑表，
        // 等于把持有期缩短成 0 —— 只要价差一直没到平仓阈值，就会在
        // spreadNoRegressionDuration 到点的那一刻强平一个刚建的仓。
        // 这个门槛同时也是"重启后的宽限期"：positionStartTime 是运行态、重启重新起算，
        // 所以重启后至少还要再持满这段时间才会触发（安全侧）。
        int64_t minHoldDurationUs{1LL * 3600 * 1000000LL};  // 1h
        // 连续未回归持续时长：持满 minHoldDurationUs 之后，连续这么久都没能达到
        // "按价差平仓"的条件 -> 渐进式平仓
        int64_t spreadNoRegressionDuration{1LL * 24 * 3600 * 1000000LL};

        // 资金费率异常风控：下一个结算期的资金费亏损（USDT）超过阈值即视为异常。
        // 对齐祖先 report_helper.py:111 的 `funding_risk < -30`（固定的 30 USDT），
        // 所以这里是**绝对金额**，不再乘任何比例系数（原来是 0.005 * 30.0 = 0.15 USDT）。
        // ⚠️ 30 是按祖先 max_amount = 30000 的单对子持仓标定的；本工程 etc/config.json
        //    的 maxAmount 是 1000，要亏到 30 USDT 需要两腿费率差约 3%（很极端）。
        //    若想按持仓比例触发，应改成 0.005 * maxAmount 这类比例式 —— 需业务确认。
        double fundingAbnormalUsdt{30.0};

        // 碎单
        double tinyCloseThresholdUsdt{25};
        int64_t tinyCloseScanIntervalUs{60LL * 1000000LL};

        // 渐进式平仓等待时间
        int64_t tier1WaitUs{30LL * 60 * 1000000LL}; // 30 min
        int64_t tier2WaitUs{60LL * 60 * 1000000LL};
        // 目前**无使用处**：tier2 -> tier3 用的是 tier1WaitUs + tier2WaitUs。
        // tier3 是"持续重试"而不是"再等一轮"，所以这个值暂时不需要。
        int64_t tier3WaitUs{120LL * 60 * 1000000LL};

        // 渐进式平仓的让利幅度，单位是**绝对价差**，不是比例 ——
        // PairTradingContext::BuildAlgoOrderJson 里是直接 *pStartSpread -= forgoProfit。
        // 量纲必须与价差本身对齐：价差缓冲带是 5e-6（SignalGenerator.cpp:12/55/…）、
        // profitPct 默认 1e-4、祖先同位置的 modify_shift_pct 是 2e-4。
        // 原来的 0.01/0.03/0.05 比 profitPct 大 100 倍、比缓冲带大 1000~10000 倍，
        // 会把平仓触发价差直接推穿到反向。
        double tier1ForgoProfit{2e-5};
        double tier2ForgoProfit{5e-5};
        double tier3ForgoProfit{1e-4};
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