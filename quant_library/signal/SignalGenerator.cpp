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

    // ---- 量字段：只依赖 pi.maxVolume，与分位数**无关** ----
    // 分开写之后：maxVolume 一变就重算，与分位数是否就绪无关。
    
    const double mv = pi.maxVolume;
    op.ttOLStartVolume = 0.0;   
    op.ttOLEndVolume = -mv;

    op.ttCLStartVolume = -mv;  
    op.ttCLEndVolume = 0.0;

    op.ttOSStartVolume = 0.0;  
    op.ttOSEndVolume = mv;

    op.ttCSStartVolume = mv;   
    op.ttCSEndVolume = 0.0;

    op.mtOLStartVolume = 0.0;  
    op.mtOLEndVolume = -mv;

    op.mtCLStartVolume = -mv;
    op.mtCLEndVolume = 0.0;
    
    op.mtOSStartVolume = 0.0;   
    op.mtOSEndVolume = mv;

    op.mtCSStartVolume = mv;    
    op.mtCSEndVolume = 0.0;
    

    if (!ls.IsValid()) {
        // 早退路径也必须过闸门。这里 4 个开关保持上一轮的值，而"刚重启 + 统计还没攒够"
        // 正是带着恢复的大仓位、最该只平不开的时刻 —— 漏了这条闸门在预热期等于不存在。
        ApplyNoOpenGate(op);
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
    }

    // TT 平多 Close Long
    {
        auto [start, end] = CalcExpectSpread(ls.askBidUQ, true, 1);
        op.ttCLStartSpread = start + (pi.profitSwitch ? pi.profitPct : 0.0);
        op.ttCLEndSpread = op.ttCLStartSpread + 0.000005;
    }


    // TT 开空 Open Short
    {
        auto [start, end] = CalcExpectSpread(ls.askBidUQ, true, 1);
        op.ttOSStartSpread = start;
        op.ttOSEndSpread = end;
    }


    // TT 平空 Close Short
    {
        auto [start, end] = CalcExpectSpread(ls.bidAskDQ, true, -1);
        op.ttCSStartSpread = start - (pi.profitSwitch ? pi.profitPct : 0.0);
        op.ttCSEndSpread = op.ttCSStartSpread - 0.000005;
    }


    // MT 开多 Open Long
    {
        auto [start, end] = CalcExpectSpread(ls.askAskDQ, false, -1);
        op.mtOLStartSpread = start;
        op.mtOLEndSpread = end;
    }

    // MT 平多 Close Long
    {
        auto [start, end] = CalcExpectSpread(ls.bidBidUQ, false, 1);
        op.mtCLStartSpread = start + (pi.profitSwitch ? pi.profitPct : 0.0);
        op.mtCLEndSpread = op.mtCLStartSpread + 0.000005;
    }


    // MT 开空 Open Short
    {
        auto [start, end] = CalcExpectSpread(ls.bidBidUQ, false, 1);
        op.mtOSStartSpread = start;
        op.mtOSEndSpread = end;
    }


    // MT 平空 Close Short
    {
        auto [start, end] = CalcExpectSpread(ls.askAskDQ, false, -1);
        op.mtCSStartSpread = start - (pi.profitSwitch ? pi.profitPct : 0.0);
        op.mtCSEndSpread = op.mtCSStartSpread - 0.000005;
    }


    // ---- 开仓阈值的"绝对地板"（祖先 cc_pricespread_gb_ltp.py:781-782）----
    // 分位数只回答"历史上有多少价差"，回答不了"多少价差才够覆盖两边的手续费"。
    // 所以再夹一道：开仓阈值朝有利方向至少 minSpreadTarget + 5e-6，且永不变正。
    //   minSpreadTarget <= 0     ⇒ 这一层整个关掉（2026-10 之前的行为）
    //   minSpreadTarget = 0.002  ⇒ StartSpread 至少 ∓20.05bp。单位就是价差本身，0.002 = 千2。
    //
    // ⚠️ 那个 5e-6 **不是可选的缓冲带**，去掉会让整个策略一单都不报：下面 :185 的开关是
    //    `canOpenLong = (mtOLStartSpread < -minSpreadTarget)`，**严格小于**。夹到正好
    //    `-minSpreadTarget` 时 `-x < -x` 为假 ⇒ canOpenLong/Short 全 false ⇒ 4 个开仓开关全关。
    //    所以夹逼值必须是 `-(minSpreadTarget + 5e-6)`，比门槛再深一点点才活。
    //
    // ⚠️ 量纲：它比的**不是实时价差**，是 StartSpread，而 StartSpread 里已经有 F 了
    // （CalcExpectSpread 的 `- direction*(execCost-buffer)`）。所以它是"扣掉入场成本之后"的
    // 净额口径，**不要再自己加一次手续费**。三条恒等式（平到 0 的假设下）：
    //     毛价差门槛 = minSpreadTarget + 5e-6 + F
    //     平到 0 净利 = minSpreadTarget + 5e-6 − F
    //     要填的值   = 想净赚的 + F − 5e-6
    // 例：F_mt = 8bp（零售）想净赚 20bp ⇒ 填 27.95bp = 0.002795；此时毛价差要 28.05bp。
    //
    // ⚠️ 它只覆盖**入场**那一笔 F。出场那一笔靠对侧分位 + profitPct 吸收，而 profitPct 只有 1bp、
    // openProfitPct 是死参数 ⇒ 出场成本实际是靠"价差回归到 0 或更远"来覆盖的。
    // 价差只回到一半就平仓，上面的净利要打折。
    //
    // 为什么光靠下面那两个 canOpen* 不够：它们只把 4 个开仓开关压成 false（"不参与"），
    // 阈值本身仍是分位数算出来的。分位数浅到 5bp 时开关一关，市场真冲到 25bp 也不会报单；
    // 夹逼是"把阈值钉在 20.05bp 上等着"。反过来，分位数比地板更深时不动它（只抬高不压低）。
    //
    // 为什么 End 必须一起改：End = Start ∓ 5e-6 是"量从 0 爬到 ±maxVolume"的阶梯，
    // 只夹 Start 会让 End 比 Start 更浅、阶梯反向，AlgoPairOrder 里 (End-Start) 还可能变成 0。
    //
    // TT 的地板再深一个 ttAddPercent：两腿都是 taker，本来就该要求更多；
    // 祖先也是 tt = mt(已夹逼) − tt_add_percent，不是两条各算各的。
    if (m_cfg.minSpreadTarget > 0.0) {
        const double floorMT = m_cfg.minSpreadTarget + 0.000005;
        const double floorTT = floorMT + m_cfg.ttAddPercent;

        if (op.ttOLStartSpread > -floorTT) {
            op.ttOLStartSpread = -floorTT;
            op.ttOLEndSpread = -floorTT - 0.000005;
        }
        if (op.mtOLStartSpread > -floorMT) {
            op.mtOLStartSpread = -floorMT;
            op.mtOLEndSpread = -floorMT - 0.000005;
        }
        if (op.ttOSStartSpread < floorTT) {
            op.ttOSStartSpread = floorTT;
            op.ttOSEndSpread = floorTT + 0.000005;
        }
        if (op.mtOSStartSpread < floorMT) {
            op.mtOSStartSpread = floorMT;
            op.mtOSEndSpread = floorMT + 0.000005;
        }
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


    // 流动性风险，禁止开仓 离强平价近时不开仓
    if (pi.activeLiquidStatus > 0 || pi.passiveLiquidStatus > 0 || pi.activeMarginStatus > 0 || pi.passiveMarginStatus > 0) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }

    // 资金费率过大：禁止开仓 有待考量
    double aFR = std::isnan(pi.rtSpread.activeFundingRate) ? 0.0 : pi.rtSpread.activeFundingRate;
    double pFR = std::isnan(pi.rtSpread.passiveFundingRate) ? 0.0 : pi.rtSpread.passiveFundingRate;
    if ( std::abs(aFR) > m_cfg.openMaxFundingRate || std::abs(pFR) > m_cfg.openMaxFundingRate) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }

    // 全局持仓闸门必须放**最后**：上面几段都在重写这 4 个开仓开关，压在前面会被它们打开。
    ApplyNoOpenGate(op);
}

// 只平不开：把 4 个开仓开关压成 false，平仓开关（ttCL/ttCS/mtCL/mtCS）不动。
// 两个来源任一成立就压：算出来的持仓超限 / 人给的配置开关。
void SignalGenerator::ApplyNoOpenGate(OrderParams& op) const {
    if (!m_globalPosExceeded && !m_closeOnly) {
        return;
    }

    op.ttOLSwitch = op.ttOSSwitch = false;
    op.mtOLSwitch = op.mtOSSwitch = false;
}

// 用实时价差与orderParams比较，生成信号
SignalResult SignalGenerator::CheckSignal(const PairInfo& pi) const {
    if (pi.hasActiveAlgoOrder) {
        return SignalResult{};
    }

    return CheckSignalForSatisfy(pi);
}

SignalResult SignalGenerator::CheckSignalForSatisfy(const PairInfo& pi) const {
    SignalResult result;

    if (!pi.rtSpread.valid) {
        return result;
    }

    if (pi.stopFlag && !pi.HasPosition()) {
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

    // ---- 开仓的"预期往返净利"门槛（祖先 min_profit_target / except_profit_flag）----
    // 地板（minSpreadTarget）只保证"入场够深"，管不了"出场线在哪"。这里把出场线也算进来：
    //     净利 = 预期出场裸价差 − 当前入场裸价差 − 2F
    // 出场裸价差由**对向**的平仓阈值反推 —— CL 的判据是 `spreadBidBid − F > mtCLStartSpread`，
    // 触发那一刻裸价差就是 `mtCLStartSpread + F`。代入后每个方向都只剩一次 F，与地板的净额口径一致。
    // 只在开仓四个方向上生效；平仓不受它约束（否则会挡掉止损）。
    //
    // ⚠️ 别把下面四行"化简"成 `op.ttOSStartSpread − op.ttCSStartSpread` 这种阈值差。
    //    **在触发边界上两者确实相等**：触发判据是 `spreadAskBid − F_tt > ttOSStartSpread`，
    //    取等号代入，F_tt 抵消，结果就是 `ttOSStartSpread − ttCSStartSpread`。
    //    但两者用的输入不同 —— 这才是不能替换的原因：
    //      · 阈值差：只含**分位数**。它随 RecalcOrderParams 重算（默认 24h 窗口、60s 一次），
    //        是**慢变量**，一分钟才动一下，且与此刻的价差无关；
    //      · 下面四行：含**瞬时价差** `rt.spreadXxx`，每 tick 都动。
    //    所以两者相差的正是"这一笔比阈值深多少"：
    //        gate = (开仓线 − 平仓线) + (实时价差 − F − 开仓线)
    //               └ 分位数预留的净利 ┘   └──── 实时超额深度 ────┘
    //    写成阈值差就等于只保留左半：分位带宽不够时一刀切禁开（哪怕市场冲得再深），
    //    带宽够时又永远放行（哪怕这一笔刚好卡在阈值上、实际一分不赚）。
    //    这里选逐 tick 版：判据要回答"**这一笔**能不能赚"，不是"这套配置理论上能不能赚"。
    //    （要阈值差那版的话，该放进 RecalcOrderParams 和 canOpen* 一起，不是放在逐 tick 的比较里。）
    const bool profitOn = m_cfg.minProfitTarget > 0.0;
    const double minProfit  = m_cfg.minProfitTarget;
    const bool mtOLProfitOk = !profitOn || (op.mtCLStartSpread - rt.spreadAskAsk - F_mt) >= minProfit;
    const bool mtOSProfitOk = !profitOn || (rt.spreadBidBid - op.mtCSStartSpread - F_mt) >= minProfit;
    const bool ttOLProfitOk = !profitOn || (op.ttCLStartSpread - rt.spreadBidAsk - F_tt) >= minProfit;
    const bool ttOSProfitOk = !profitOn || (rt.spreadAskBid - op.ttCSStartSpread - F_tt) >= minProfit;

    if (op.ttOLSwitch && ttOLProfitOk && rt.spreadBidAsk + F_tt < op.ttOLStartSpread && vol > op.ttOLEndVolume + 1e-9) {
        result.ttOLSignal = true;
        result.hasSignal = true;
    }

    if (op.ttOSSwitch && ttOSProfitOk && rt.spreadAskBid - F_tt > op.ttOSStartSpread && vol < op.ttOSEndVolume - 1e-9) {
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

    if (op.mtOLSwitch && mtOLProfitOk && rt.spreadAskAsk + F_mt < op.mtOLStartSpread && vol > op.mtOLEndVolume + 1e-9) {
        result.mtOLSignal = true;
        result.hasSignal = true;
    }

    if (op.mtOSSwitch && mtOSProfitOk && rt.spreadBidBid - F_mt > op.mtOSStartSpread && vol < op.mtOSEndVolume - 1e-9) {
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

    return result;
}

bool SignalGenerator::CanOpen(const PairInfo& pi, std::string& reason) const {
    // 配置里的"只平不开"（可热改）。放在最前面：它是人按的急停，
    // 比 stopFlag/closeFlag 这些按对子的状态优先级更高，日志里也应该先看到它。
    // 注意 RecalcOrderParams 已经把 4 个开仓开关压掉了，这里再判一次是为了：
    //   ① 日志里能说出原因（否则只看到"没有信号"，看不出是人关的）；
    //   ② 覆盖"开关刚被压、信号还没重算"的那一个 tick 的空窗。
    if (m_closeOnly) {
        reason = "closeOnly = true (配置)";
        return false;
    }

    if (pi.stopFlag) {
        reason = "stopFlag = true";
        return false;
    }

    if (pi.closeFlag) {
        reason = "closeFlag = true";
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

    if (pi.stopFlag) {
        reason = "stopFlag = true";
        return false;
    }

    // 持仓已小于主动腿最小报单量（碎单）：交易所侧再也报不出平仓单了。
    if (pi.activeParam.minVolume > 0.0 && std::abs(pi.pairTotalVolume) < pi.activeParam.minVolume) {
        reason = "position below activeParam.minVolume (dust)";
        return false;
    }

    return true;
}

bool SignalGenerator::CloseSpreadReached(const PairInfo& pi) const {
    if (!pi.HasPosition() || !pi.rtSpread.valid) {
        return false;
    }

    const auto& op = pi.orderParams;
    const auto& rt = pi.rtSpread;

    // 与 CheckSignalForSatisfy(:176-177) 完全同源的成本口径
    const double F_tt = CalcExecCost(true);
    const double F_mt = CalcExecCost(false);

    // 四个平仓分支逐字对应 CheckSignalForSatisfy 的 ttCL / mtCL / ttCS / mtCS。
    // 刻意**不**带 vol 条件（op.ttCLEndVolume 那一类）：
    // 那些条件判的是"还有没有仓位可平"，与"价差有没有回归"是两件事；
    // 风控这里只问后者，前者由 CanClose / HasPosition 负责。
    if (pi.IsLong()) {
        return (rt.spreadAskBid - F_tt > op.ttCLStartSpread) ||
               (rt.spreadBidBid - F_mt > op.mtCLStartSpread);
    }

    if (pi.IsShort()) {
        return (rt.spreadBidAsk + F_tt < op.ttCSStartSpread) ||
               (rt.spreadAskAsk + F_mt < op.mtCSStartSpread);
    }

    return false;
}

}
