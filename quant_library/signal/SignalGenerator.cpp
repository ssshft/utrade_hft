#include "SignalGenerator.h"

namespace pt {

std::pair<double, double> SignalGenerator::CalcExpectSpread(double quantileBound, bool isTT, int direction) const {
    // 执行成本与阶梯同源 -> 比较时精确抵消
    // buffer 反向叠加：抵消后残差为 -direction*buffer，即 TT 的入口要求比 MT 更极端 ttAddPercent
    // （祖先：tt_except_open_long_spread = mt_except_open_long_spread - tt_add_percent）
    double execCost = CalcExecCost(isTT);
    double buffer = isTT ? m_cfg.ttAddPercent : 0.0;
    double startSpread = quantileBound * m_cfg.spreadAdjPct - direction * (execCost - buffer);
    double endSpread = startSpread + direction * 0.000005; // 5e-6 缓冲带
    return {startSpread, endSpread};
}

double SignalGenerator::CalcMinMoveSlippage(double minMove, double price) const {
    if (price <= 0 || minMove <= 0) {
        return 0.0;
    }

    return minMove / price * m_cfg.slippagePctMinMove;
}

void SignalGenerator::RecalcOrderParams(PairInfo& pi) const {
    auto& op = pi.orderParams;
    const auto& ls = pi.largeStats;

    if (!ls.IsValid()) {
        return;
    }

    double spreadSpan = ls.bidBidUQ - ls.askAskDQ;
    if (spreadSpan < m_cfg.minSpreadSpan) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
        return;
    } 

    // 成本口径统一由 CalcExecCost(isTT) 提供（TT 用 taker 费率 + ttAddPercent 缓冲，
    // MT 用 maker 费率、无缓冲），不再在这里单独拼费率，避免与算法单侧的 Fs 不同源

    // TT 开多 Open Long
    {
        auto [start, end] = CalcExpectSpread(ls.bidAskDQ, true, -1);
        op.ttOLStartSpread = start;
        op.ttOLEndSpread = end;
        op.ttOLStartVolume = 0.0;
        op.ttOLEndVolume = -pi.maxVolume;
    }

    // TT 平多 Close Long
    {
        auto [start, end] = CalcExpectSpread(ls.askBidUQ, true, 1);
        op.ttCLStartSpread = start + (pi.profitSwitch ? pi.profitPct : 0.0);
        op.ttCLEndSpread = op.ttCLStartSpread + 0.000005;
        op.ttCLStartVolume = -pi.maxVolume;
        op.ttCLEndVolume = 0.0;
    }


    // TT 开空 Open Short
    {
        auto [start, end] = CalcExpectSpread(ls.askBidUQ, true, 1);
        op.ttOSStartSpread = start;
        op.ttOSEndSpread = end;
        op.ttOSStartVolume = 0.0;
        op.ttOSEndVolume = pi.maxVolume;
    }


    // TT 平空 Close Short
    {
        auto [start, end] = CalcExpectSpread(ls.bidAskDQ, true, -1);
        op.ttCSStartSpread = start - (pi.profitSwitch ? pi.profitPct : 0.0);
        op.ttCSEndSpread = op.ttCSStartSpread - 0.000005;
        op.ttCSStartVolume = pi.maxVolume;
        op.ttCSEndVolume = 0.0;
    }


    // MT 开多 Open Long
    {
        auto [start, end] = CalcExpectSpread(ls.askAskDQ, false, -1);
        op.mtOLStartSpread = start;
        op.mtOLEndSpread = end;
        op.mtOLStartVolume = 0.0;
        op.mtOLEndVolume = -pi.maxVolume;
    }

    // MT 平多 Close Long
    {
        auto [start, end] = CalcExpectSpread(ls.bidBidUQ, false, 1);
        op.mtCLStartSpread = start + (pi.profitSwitch ? pi.profitPct : 0.0);
        op.mtCLEndSpread = op.mtCLStartSpread + 0.000005;
        op.mtCLStartVolume = -pi.maxVolume;
        op.mtCLEndVolume = 0.0;
    }


    // MT 开空 Open Short
    {
        auto [start, end] = CalcExpectSpread(ls.bidBidUQ, false, 1);
        op.mtOSStartSpread = start;
        op.mtOSEndSpread = end;
        op.mtOSStartVolume = 0.0;
        op.mtOSEndVolume = pi.maxVolume;
    }


    // MT 平空 Close Short
    {
        auto [start, end] = CalcExpectSpread(ls.askAskDQ, false, -1);
        op.mtCSStartSpread = start - (pi.profitSwitch ? pi.profitPct : 0.0);
        op.mtCSEndSpread = op.mtCSStartSpread - 0.000005;
        op.mtCSStartVolume = pi.maxVolume;
        op.mtCSEndVolume = 0.0;
    }


    bool canOpenLong = (op.mtOLStartSpread < -m_cfg.minSpreadTarget) && !pi.closeFlag;
    bool canOpenShort = (op.mtOSStartSpread > m_cfg.minSpreadTarget) && !pi.closeFlag;

    op.ttOLSwitch = canOpenLong && pi.autoFlag;
    op.ttOSSwitch = canOpenShort && pi.autoFlag;
    op.mtOLSwitch = canOpenLong && pi.autoFlag;
    op.mtOSSwitch = canOpenShort && pi.autoFlag;


    // 始终开启，除非stopFlag且无持仓
    op.ttCLSwitch = op.ttCSSwitch = true;
    op.mtCLSwitch = op.mtCSSwitch = true;

    if (!pi.autoFlag) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.ttCLSwitch = op.ttCSSwitch = false;
    }

    // 流动性风险，禁止开仓
    if (pi.activeLiquidStatus > 0 || pi.passiveLiquidStatus > 0 || pi.activeMarginStatus > 0 || pi.passiveMarginStatus > 0) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }

    // 资金费率过大：禁止开仓
    double aFR = std::isnan(pi.rtSpread.activeFundingRate) ? 0.0 : pi.rtSpread.activeFundingRate;
    double pFR = std::isnan(pi.rtSpread.passiveFundingRate) ? 0.0 : pi.rtSpread.passiveFundingRate;
    if ( std::abs(aFR) > m_cfg.openMaxFundingRate || std::abs(pFR) > m_cfg.openMaxFundingRate) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }
}

// 用实时价差与orderParams比较，生成信号
SignalResult SignalGenerator::CheckSignal(const PairInfo& pi) const {
    SignalResult result;

    if (!pi.rtSpread.valid) {
        return result;
    }

    if (pi.stopFlag && !pi.HasPosition()) {
        return result;
    }

    if (pi.hasActiveAlgoOrder) {
        return result;
    }

    const auto& op = pi.orderParams;
    const auto& rt = pi.rtSpread;
    double vol = pi.pairTotalVolume;

    // 与 AlgoPairOrder::GetTargetPairOrder 的 tempSpread 完全同源：
    // OL/CS 用 (价差 + F)，OS/CL 用 (价差 - F)；F 与算法单的 takerTakerFs/makerTakerFs 一致。
    // 两侧同源时 F 与 StartSpread 里的执行成本精确抵消，闸门与阶梯不会错开
    const double F_tt = CalcExecCost(true);
    const double F_mt = CalcExecCost(false);

    if (op.ttOLSwitch && rt.spreadBidAsk + F_tt < op.ttOLStartSpread && vol > op.ttOLEndVolume + 1e-9) {
        result.ttOLSignal = true;
        result.hasSignal = true;
    }

    if (op.ttOSSwitch && rt.spreadAskBid - F_tt > op.ttOSStartSpread && vol < op.ttOSEndVolume - 1e-9) {
        result.ttOSSignal = true;
        result.hasSignal = true;
    }

    if (op.ttCLSwitch && pi.IsLong() && rt.spreadAskBid - F_tt > op.ttCLStartSpread && vol < op.ttCLEndVolume - 1e-9) {
        result.ttCLSignal = true;
        result.hasSignal = true;
    }

    if (op.ttCSSwitch && pi.IsShort() && rt.spreadBidAsk + F_tt < op.ttCSStartSpread && vol > op.ttCSEndVolume + 1e-9) {
        result.ttCSSignal = true;
        result.hasSignal = true;
    }

    if (op.mtOLSwitch && rt.spreadAskAsk + F_mt < op.mtOLStartSpread && vol > op.mtOLEndVolume + 1e-9) {
        result.mtOLSignal = true;
        result.hasSignal = true;
    }

    if (op.mtOSSwitch && rt.spreadBidBid - F_mt > op.mtOSStartSpread && vol < op.mtOSEndVolume - 1e-9) {
        result.mtOSSignal = true;
        result.hasSignal = true;
    }

    if (op.mtCLSwitch && pi.IsLong() && rt.spreadBidBid - F_mt > op.mtCLStartSpread && vol < op.mtCLEndVolume - 1e-9) {
        result.mtCLSignal = true;
        result.hasSignal = true;
    }

    if (op.mtCSSwitch && pi.IsShort() && rt.spreadAskAsk + F_mt < op.mtCSStartSpread && vol > op.mtCSEndVolume + 1e-9) {
        result.mtCSSignal = true;
        result.hasSignal = true;
    }

    if (result.hasSignal) {
        LOG_INFO("");
    }

    return result;
}

bool SignalGenerator::CanOpen(const PairInfo& pi, std::string& reason) const {
    if (pi.stopFlag) {
        reason = "stopFlag = true";
        return false;
    }

    if (pi.closeFlag) {
        reason = "closeFlag = true";
        return false;
    }

    if (pi.limitFlag) {
        reason = "limitFlag = true";
        return false;
    }

    if (pi.errorFlag) {
        reason = "errorFlag = true";
        return false;
    }

    // 暂时注释，等待数据落入持仓量和成交量数据
    /*
    if (!std::isnan(pi.activeOIUsdt) && pi.activeOIUsdt < 500000) {
        reason = "activeOI too small";
        return false;
    }

    if (!std::isnan(pi.passiveOIUsdt) && pi.passiveOIUsdt < 500000) {
        reason = "passiveOI too small";
        return false;
    }

    // 成交量/OI 比率
    if (!std::isnan(pi.activeOI) && pi.activeOI > 0 && pi.activeDailyAmount / pi.activeOIUsdt < 0.5) {
        reason = "active turnover/OI ratio too low";
        return false;
    }
    */

    double s = pi.rtSpread.spreadAskAsk;
    if (!std::isnan(s) && std::abs(s) > 0.1) {
        reason = "spreadAskAsk abnormal > 10%";
        return false;
    }

    return true;
}

bool SignalGenerator::CanClose(const PairInfo& pi, std::string& reason) const {
    if (!pi.HasPosition()) {
        reason = "no position";
        return false;
    }
    return true;
}

}