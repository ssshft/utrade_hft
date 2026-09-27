#include "RiskManager.h"

namespace pt {

    // 计算当前的挡位
    std::pair<int, bool> RiskManager::GetCurrentTier(const AbnormalCloseState& state, int64_t nowUs) const {
        if (!state.triggered) {
            return {0, false};
        }

        int tier = state.currentTier;
        int64_t elapsed = nowUs - state.startTime;

        if (tier == 1 && elapsed > m_cfg.tier1WaitUs && state.tier1Times > 0) {
            return {2, true};
        }

        if (tier == 2 && elapsed > m_cfg.tier1WaitUs + m_cfg.tier2WaitUs && state.tier2Times > 0) {
            return {3, true};
        }

        if (tier == 1 && state.tier1Times == 0) {
            return {1, true};
        }

        if (tier == 2 && state.tier2Times == 0) {
            return {2, true};
        }

        // tier3 是最后一档：不再升级，也不再停手。
        // 这里刻意**不**判 tier3Times —— 原来的 `tier == 3 && state.tier3Times == 0`
        // 会让 tier3 尝试过一次之后恒落到末尾的 return {tier, false}，
        // needForceClose 恒 false，强平彻底停止，直到持仓归零才由
        // OnAlgoFinished(fullyFlat=true) 复位；若 tier3 也没成交就永久卡住。
        if (tier == 3) {
            return {3, true};
        }

        return {tier, false};
    }


    void RiskManager::AdvanceTier(AbnormalCloseState& state, int64_t nowUs) const {
        if (!state.triggered) {
            state.triggered = true;
            state.currentTier = 1;
            state.startTime = nowUs;
            state.tier1Times = 0;
            state.tier2Times = 0;
            state.tier3Times = 0;
            return;
        }

        int64_t elapsed = nowUs - state.startTime;
        if (state.currentTier == 1 && elapsed > m_cfg.tier1WaitUs && state.tier1Times > 0) {
            state.currentTier = 2;
        } else if (state.currentTier == 2 && elapsed > m_cfg.tier1WaitUs + m_cfg.tier2WaitUs && state.tier2Times > 0) {
            state.currentTier = 3;
        }
    }


    // 对于持有多头仓位：平仓条件为spreadAskAsk > ttCLStartSpread
    // 让步：降低ttCLStartSptred(接受更低的价差也平仓)。
    // 返回值是**绝对价差**让利幅度（量纲与 orderParams 里的 StartSpread 一致，
    // 调用方直接做 *pStartSpread -= forgoProfit），不是利润比例。
    // 注：pi 目前未参与计算，保留形参是为了后续按持仓/波动缩放让利幅度。
    double RiskManager::CalcForgoSpread(const PairInfo& pi, int tier) const {
        (void)pi; // 当前未参与计算，保留形参以便后续按持仓/波动缩放让利幅度
        switch (tier) {
            case 1: 
                return m_cfg.tier1ForgoProfit;
            case 2: 
                return m_cfg.tier2ForgoProfit;
            case 3: 
                return m_cfg.tier3ForgoProfit;
            default:
                return 0.0;
        }
    }

    bool RiskManager::CheckTinyClose(const PairInfo& pi) const {
        if (!pi.HasPosition()) {
            return false;
        }

        double val = pi.CalcPositionValue();
        if (std::isnan(val) || val <= 0.0) {
            return false;
        }

        return val < m_cfg.tinyCloseThresholdUsdt;
    }

    // 持仓阈值持续4天 + ADL分位数过高 -》渐进式平仓
    RiskCheckResult RiskManager::CheckADLRisk(PairInfo& pi, int64_t nowUs) const {
        RiskCheckResult r;
        if (!pi.HasPosition()) {
            return r;
        }

        double posVal = pi.CalcPositionValue();
        if (std::isnan(posVal) || posVal < m_cfg.adlPositionThresholdUsdt) {
            pi.positionExceedThresholdStartTime = 0;
            return r;
        }

        // 注意这里是 == 不是 =：写成赋值会恒为 0（恒 false），
        // positionExceedThresholdStartTime 永不初始化，于是
        // holdDuration = nowUs - 0 是天文数字，恒大于 positionExceedDuration(4天)，
        // "持仓超阈值持续 4 天"这个前置条件会完全失效。
        if (pi.positionExceedThresholdStartTime == 0) {
            pi.positionExceedThresholdStartTime = nowUs;
            return r;
        }

        int64_t holdDuration = nowUs - pi.positionExceedThresholdStartTime;
        if (holdDuration < m_cfg.positionExceedDuration) {
            return r;
        }

        double adlRank = pi.IsLong() ? pi.activeAdlRank : pi.passiveAdlRank;
        if (adlRank < m_cfg.adlHighRankThreshold) {
            return r;
        }

        r.hasRisk = true;
        r.riskType = AbnormalClose_ADL;
        AdvanceTier(pi.adlClose, nowUs);

        auto [tier, shouldAttempt] = GetCurrentTier(pi.adlClose, nowUs);
        r.targetTier = tier;
        r.forgoProfit = CalcForgoSpread(pi, tier);
        r.needForceClose = shouldAttempt;

        // LOG_INFO("r")
        return r;
    }


    // 持仓后价差持续未回归1天-》渐进式平仓
    // 回归判断：当前实时价差越过建仓时的小周期分位数（说明价差已经回到开仓时的有利侧）
    //
    // ⚠️ 与祖先的差异（有意保留，2026-09-26 确认）：
    //   祖先 cc_pricespread_gb_ltp.py:896-898 的 unqualified_spread_flag 用的是
    //   **腿价比偏离**（|positionValue| > target_amount*5 且
    //   pairPassiveTotalPrice/pairActiveTotalPrice - 1 偏离 ∓0.0003），
    //   而且它的用途是"禁止开仓 + 压掉开仓开关"（:906/:966/:1069），**不是强平**。
    //   这里保留分位数判据、并把它用作 1 天后的强平触发，是 C++ 侧的设计。
    //
    // ⚠️ 依赖 openSmallSpread* 两个字段：
    //   它们是建仓瞬间的快照，由 PairTradingContext::CaptureOpenSpreadSnapshot 写、
    //   完全平仓时由 OnAlgoFinished 复位成 NAN。这两个字段原先在 PairInfo 里没有初值
    //   （栈上垃圾），垃圾只要不是 NaN 就会让下面的 isnan 哨兵失效 ——
    //   快照永不写入，且拿垃圾基准判"是否回归"。已在 PairInfo.h 里显式初始化为 NAN。
    //
    // ⚠️ 已知口径问题（未改）：这里的轴是 spreadAskAsk / spreadBidBid，
    //   而平仓触发价差用的轴是 ttCL=spreadAskBid、mtCL=spreadBidBid、ttCS=spreadBidAsk、
    //   mtCS=spreadAskAsk —— 与 ttCL/ttCS 并不同轴，只是近似。
    RiskCheckResult RiskManager::CheckSpreadNoRegression(PairInfo& pi, int64_t nowUs) const {
        RiskCheckResult r;
        if (!pi.HasPosition()) {
            pi.spreadNoRegressionStartTime = 0;
            return r;
        }

        if (!pi.rtSpread.valid) {
            return r;
        }

        // 判断价差是否回归；回归=越过平仓阈值方向
        bool isRegressed = false;
        if (pi.IsLong()) {
            // 多头持仓：若spreadAskAsk 已超过小周期askAskUQ, 说明价差回归
            if (!std::isnan(pi.openSmallSpreadAskAskDQ)) {
                isRegressed = pi.rtSpread.spreadAskAsk > pi.openSmallSpreadAskAskDQ;
            }
        } else if (pi.IsShort()) {
            // 空头持仓：若spreadBidBid 已低于小周期bidBidDQ, 说明价差回归
            if (!std::isnan(pi.openSmallSpreadBidBidUQ)) {
                isRegressed = pi.rtSpread.spreadBidBid < pi.openSmallSpreadBidBidUQ;
            }
        }

        if (isRegressed) {
            pi.spreadNoRegressionStartTime = 0;
            if (pi.spreadNoRegression.triggered) {
                // 价差已回归，重置风控状态
                pi.spreadNoRegression = AbnormalCloseState();
            }
            return r;
        }

        // 价差未回归，开始/继续计时
        if (pi.spreadNoRegressionStartTime == 0) {
           pi.spreadNoRegressionStartTime = nowUs;
           return r; 
        }

        int64_t noRegressionDuration = nowUs - pi.spreadNoRegressionStartTime;
        if (noRegressionDuration < m_cfg.spreadNoRegressionDuration) {
            return r;
        }

        r.hasRisk = true;
        r.riskType = AbnormalClose_SPREAD_REGRESSION;
        AdvanceTier(pi.spreadNoRegression, nowUs);

        auto [tier, shouldAttempt] = GetCurrentTier(pi.spreadNoRegression, nowUs);
        r.targetTier = tier;
        r.forgoProfit = CalcForgoSpread(pi, tier);
        r.needForceClose = shouldAttempt;

        // LOG_INFO spreadBidBid spreadAskAsk
        return r;
    }


    // 下一个结算期的资金费亏损超过阈值（即当前资金费率对持仓明显不利）
    RiskCheckResult RiskManager::CheckFundingAbnormal(PairInfo& pi, int64_t nowUs) const {
        RiskCheckResult r;
        if (!pi.HasPosition()) {
            return r;
        }

        // RealTimeSpread 的这两个字段默认 0.0（不是 NaN），isnan 只是防御性写法
        const double aFR = std::isnan(pi.rtSpread.activeFundingRate) ? 0.0 : pi.rtSpread.activeFundingRate;
        const double pFR = std::isnan(pi.rtSpread.passiveFundingRate) ? 0.0 : pi.rtSpread.passiveFundingRate;

        // 照搬祖先 report_helper.py:107-109 的公式：
        //     funding_risk = positionValue * (passiveFundingRate - activeFundingRate)
        // positionValue 是**带符号**的（祖先 :790/793），方向完全由它的符号承载，
        // 所以既不能取绝对值，也不能再对结果取负。
        // 与祖先 :664 的 open_long_funding_profit = aFR - pFR 同向：
        //   positionValue < 0（多）-> funding_risk = |pv| * (aFR - pFR)
        // 符号约定：> 0 为收取（盈利），< 0 为支付（亏损）。
        //
        // 原实现有两个错：① `aFR - (-pFR)` 把 pFR 的符号写反（应为 -pFR）；
        // ② 空头分支的 `netFundingCost = -netFundingCost` 自我抵消，两分支结果相同，
        // 多空方向被抹平；再叠加 CalcPositionValue() 的 std::abs，方向信息彻底丢失。
        const double fundingRisk = pi.CalcSignedPositionValue() * (pFR - aFR);

        // 阈值对齐祖先 report_helper.py:111 的固定 30 USDT
        if (fundingRisk > -m_cfg.fundingAbnormalUsdt) {
            return r;
        }

        //触发资金费率异常
        r.hasRisk = true;
        r.riskType = AbnormalClose_FUNDING_ABNORMAL;
        AdvanceTier(pi.fundingAbnormal, nowUs);

        auto [tier, shouldAttempt] = GetCurrentTier(pi.fundingAbnormal, nowUs);
        r.targetTier = tier;
        r.forgoProfit = CalcForgoSpread(pi, tier);
        r.needForceClose = shouldAttempt;

        // LOG_INFO spreadBidBid spreadAskAsk
        return r;
    }

    RiskCheckResult RiskManager::CheckRisk(PairInfo& pi, int64_t nowUs) const {
        if (CheckTinyClose(pi)) {
            RiskCheckResult r;
            r.hasRisk = true;
            r.isTinyClose = true;
            r.needForceClose = true;
            r.reason = "tiny position < " + std::to_string(m_cfg.tinyCloseThresholdUsdt) + " USDT";
            return r;
        }

        RiskCheckResult adl = CheckADLRisk(pi, nowUs);
        if (adl.needForceClose) {
            return adl;
        }

        RiskCheckResult snr = CheckSpreadNoRegression(pi, nowUs);
        if (snr.needForceClose) {
            return snr;
        }

        RiskCheckResult fa = CheckFundingAbnormal(pi, nowUs);
        if (fa.needForceClose) {
            return fa;
        }

        // 有风险但不需要立即发单，等待挡位升级
        if (adl.hasRisk) {
            return adl;
        }

        if (snr.hasRisk) {
            return snr;
        }

        if (fa.hasRisk) {
            return fa;
        }


        return RiskCheckResult{};
    }

    void RiskManager::OnAlgoFinished(PairInfo& pi, bool fullyFlat) const {
        if (fullyFlat) {
            // 完全平仓
            pi.adlClose = AbnormalCloseState();
            pi.spreadNoRegression = AbnormalCloseState();
            pi.fundingAbnormal = AbnormalCloseState();

            pi.positionExceedThresholdStartTime = 0;
            pi.spreadNoRegressionStartTime = 0;
            pi.openSmallSpreadBidBidUQ = NAN;
            pi.openSmallSpreadAskAskDQ = NAN;
            return;
        }

        auto incrTimes = [](AbnormalCloseState& s) {
            switch (s.currentTier) {
                case 1:
                    s.tier1Times++;
                    break;
                case 2:
                    s.tier2Times++;
                    break;
                case 3:
                    s.tier3Times++;
                    break;


            }
        };

        if (pi.adlClose.triggered) {
            incrTimes(pi.adlClose);
        }

        if (pi.spreadNoRegression.triggered) {
            incrTimes(pi.spreadNoRegression);
        }

        if (pi.fundingAbnormal.triggered) {
            incrTimes(pi.fundingAbnormal);
        }
    }


}