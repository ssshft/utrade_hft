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

        if (tier == 3 && state.tier3Times == 0) {
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
    // 让步：降低ttCLStartSptred(接受更低的价差也平仓)，返回的forgoSpread > 0表示让步幅度，调用方自行决定
    double RiskManager::CalcForgoSpread(const PairInfo& pi, int tier) const {
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

        if (pi.positionExceedThresholdStartTime = 0) {
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
    // 回归判断：当前实时价差未超过小周期分位数（开仓方向反转）
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


    // 当前周期资金费率 * 持仓量的预期费用超过阈值（即当前资金费用过大）
    RiskCheckResult RiskManager::CheckFundingAbnormal(PairInfo& pi, int64_t nowUs) const {
        RiskCheckResult r;
        if (!pi.HasPosition()) {
            return r;
        }

        double aFR = std::isnan(pi.rtSpread.activeFundingRate) ? 0.0 : pi.rtSpread.activeFundingRate;
        double pFR = std::isnan(pi.rtSpread.passiveFundingRate) ? 0.0 : pi.rtSpread.passiveFundingRate;


        // 净资金费用 正值=亏损
        double netFundingCost = 0.0;
        if (pi.IsLong()) {
            netFundingCost = aFR - (-pFR); // active支付，passive收取
        } else if (pi.IsShort()) {
            netFundingCost = (-aFR) - pFR; // active收取，passive支付
            netFundingCost = -netFundingCost;
        }

        double posVal = pi.CalcPositionValue();
        double fundingLoss = netFundingCost * posVal;
        double threshold = m_cfg.fundingAbnormalThreshold * m_cfg.fundingMaxAmount;

        if (fundingLoss < threshold) {
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