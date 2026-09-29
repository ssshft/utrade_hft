#include "RiskManager.h"
#include "../signal/SignalGenerator.h"

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


    // 对于持有多头仓位：平仓条件是 spreadAskBid - F_tt > ttCLStartSpread
    //   （或 spreadBidBid - F_mt > mtCLStartSpread），见 SignalGenerator::CheckSignalForSatisfy。
    //   ⚠️ 原先这里写的是 spreadAskAsk / ttCLStartSpread，轴写错了：ttCL 走 spreadAskBid。
    // 让步：降低 ttCLStartSpread（接受更低的价差也平仓）。
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


    // 持仓后价差持续未回归 -> 渐进式平仓
    //
    // 「回归」的定义（2026-09-29 重写）：**当前已经满足"按价差平仓"的条件**，
    //   即 SignalGenerator::CloseSpreadReached —— 与 CheckSignalForSatisfy 的
    //   ttCL / mtCL / ttCS / mtCS 四个分支同源、同轴、同成本口径。
    //
    // 为什么弃用原来的 openSmallSpread* / smallStats 判据：
    //   ① smallStats 全工程没有生产者（PairInfoManager::UpdateSmallStats 零调用者），
    //      于是 CaptureOpenSpreadSnapshot 的 smallStats.IsValid() 守卫永远早退、
    //      openSmallSpread* 恒为 NaN，下面两处 isnan 哨兵都进不去 —— isRegressed
    //      恒为 false，这条风控实际退化成了"持仓满 spreadNoRegressionDuration
    //      就无条件下强平"，与价差是否回归、是否盈利完全无关；
    //   ② 即使补上生产者，原来的轴也是错的：IsLong 比 spreadAskAsk、IsShort 比
    //      spreadBidBid，正好是**对方方向**的平仓轴（多头平仓走 ttCL=spreadAskBid /
    //      mtCL=spreadBidBid；空头平仓走 ttCS=spreadBidAsk / mtCS=spreadAskAsk）；
    //   ③ 祖先 Python 根本没有这条风控（cc_pricespread_gb_ltp.py 全仓库
    //      grep regress/force_close 零命中），其平仓条件全是"价差已经变好"；
    //      而 openSmallSpread* 的来源 *_q_open（:702/:708/:778-779）是**开仓**阈值，
    //      用途是把开仓门槛收紧到分位数以内，不是平仓基准。
    //
    // 现在的语义：持仓达到 minHoldDurationUs 之后，**连续** spreadNoRegressionDuration
    //   都没能满足"按价差平仓"的条件 -> 渐进式平仓。
    //   一旦某一 tick 观察到已满足（价差回归 / 已可止盈），计时清零并复位档位。
    RiskCheckResult RiskManager::CheckSpreadNoRegression(PairInfo& pi, int64_t nowUs) const {
        RiskCheckResult r;
        if (!pi.HasPosition()) {
            pi.positionStartTime = 0;
            pi.spreadNoRegressionStartTime = 0;
            return r;
        }

        // 持仓起点：第一次观察到有持仓（含建仓中的部分成交）的时刻
        if (pi.positionStartTime == 0) {
            pi.positionStartTime = nowUs;
        }

        // 最短持有期：不足则一律不触发，且**不启动**下面的"连续未回归"计时。
        // 必须放在回归判断之前 —— 建仓瞬间价差必然还没到平仓阈值，若从这一刻就开始
        // 跑表，持有期会被缩成 0，等于"刚开仓，24h 后无条件强平"。
        // 它同时是重启后的宽限期：positionStartTime 是运行态、重启重新起算。
        if (nowUs - pi.positionStartTime < m_cfg.minHoldDurationUs) {
            return r;
        }

        if (!pi.rtSpread.valid) {
            return r;
        }

        // orderParams 派生自 largeStats；统计还没建立时阈值是默认 0，
        // 拿它判"是否回归"没有意义 -> 不启动计时（等价于这条风控暂缓）
        if (!pi.largeStats.IsValid()) {
            return r;
        }

        // 回归 = 已经能满足按价差平仓的条件（与执行端同源同轴）
        if (SignalGenerator::Instance().CloseSpreadReached(pi)) {
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

        const int64_t noRegressionDuration = nowUs - pi.spreadNoRegressionStartTime;
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
            pi.positionStartTime = 0;
            // openSmallSpread* 自 2026-09-29 起已无人读取（"价差不回归"改用
            // SignalGenerator::CloseSpreadReached），这里保留复位只是为了
            // 不改变快照列布局；待快照格式统一迁移时一并删掉。
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