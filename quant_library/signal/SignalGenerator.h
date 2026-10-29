/***
 * 核心思路：
 * StartSpread = 分位数边界 * spreadAdjPct - direction * (执行成本 - 缓冲)
 * 只有「实时价差 ∓ 执行成本」穿越 StartSpread 时才生成开仓信号
 *
 * 关键约束：执行成本 F 必须与算法单的 takerTakerFs / makerTakerFs 同源。
 * CheckSignal 与 AlgoPairOrder 的阶梯都用 (实时价差 ∓ F) 去比同一个 StartSpread，
 * 所以 F 在两侧精确抵消，抵消后只留下 -direction*buffer。
 * buffer 只对 TT 非零（ttAddPercent），方向与成本相反，使 TT 的入口比 MT 更极端，
 * 即双 taker 更难成交（与祖先 tt_add_percent 一致）。
 * 两侧 F 不同源就会错开一个 F，形成「信号触发但报不出单」的死区。
 * ****/

#pragma once

#include "../basic/PairInfo.h"
#include <unordered_map>


namespace pt {
// 全局配置，所有对子共享
struct FeeSlippageConfig {
    // double activeMakerFeeRate{0.0002};
    // double activeTakerFeeRate{0.0006};
    // double passiveMakerFeeRate{0.0002};
    // double passiveTakerFeeRate{0.0006};
    // double basicSlippage{0.0001};

    double activeMakerFeeRate{0};
    double activeTakerFeeRate{0};
    double passiveMakerFeeRate{0};
    double passiveTakerFeeRate{0};
    double basicSlippage{0};

    double slippagePctMinMove{0.5}; // 按最小变价计算的滑点比例
    double ttAddPercent{0.0003};   // 双taker额外价差要求：只对 TT 生效，且与执行成本反向叠加，
                                   // 抵消后使 TT 入口阈值 = Q*spreadAdjPct + direction*ttAddPercent，
                                   // 即 TT 比 MT 更难成交（与祖先 tt_add_percent 同向）
    double spreadAdjPct{0.8};     // 分位数调节比例（祖先 spread_adj_pct=0.8）。
                                  // 入口阈值 = Q*spreadAdjPct，|Q*adj| 越小越容易触发：
                                  // 祖先注释「在 spread 收缩时有更多的开平机会」
    double minSpreadSpan{0.000};   // 最小分位数差值(套利空间门槛)（祖先 min_spread_span_org=0.0002）。
                                    // 低于此值 RecalcOrderParams 直接关掉全部开仓开关并 return
    // 开仓阈值的"绝对地板"，单位就是价差本身（0.002 = 千2 = 20bp）。
    //   > 0 : RecalcOrderParams 末尾把 4 个开仓 StartSpread 夹到至少 ±(值+5e-6) 深
    //         （祖先 cc_pricespread_gb_ltp.py:781-782 的 min/max 夹逼）；TT 再深一个 ttAddPercent。
    //   <= 0: 这一层整个关掉，开仓阈值完全由分位数决定（2026-10 之前的行为）。
    // ⚠️ 量纲：夹的是 StartSpread，而它含 F ⇒ 这个值是**扣掉入场成本之后**的净额，
    //    不要再自己加一次手续费。三条恒等式（假设平到 0）：
    //        毛价差门槛 = 值 + 5e-6 + F
    //        净利       = 值 + 5e-6 − F
    //        该填的值   = 想净赚的 + F − 5e-6
    //    只覆盖**入场**那一笔 F；出场成本靠对侧分位 + profitPct 吸收。
    // ⚠️ 负值**不是**"更松的软门槛"，是"不设地板"：-0.001 只等于"允许在 +10bp 上开多"。
    // ⚠️ etc/config.json 里的同名键会覆盖这里的默认值：2026-10-26 文件里是 **0.0012**
    //    （按 F_mt = 7bp、目标净赚 5bp 算的），这里是兜底默认 0.002。
    //    改之前先确认文件里那份，否则改这里不生效。
    double minSpreadTarget{0.002};
    // 开仓的"预期往返净利"门槛（祖先 min_profit_target / except_profit_flag，
    // cc_pricespread_gb_ltp.py:266 + :862-876）。单位是**净额**，两边 F 都已扣。
    //   CheckSignalForSatisfy 里逐 tick 算：净利 = 预期出场裸价差 − 当前入场裸价差 − 2F
    //   出场裸价差由**对向**的平仓阈值反推（CL 判据 `spreadBidBid − F > start` ⇒ 触发时裸价差 = start + F），
    //   四个方向化简后都只剩一次 F：
    //     MT 开多：mtCLStartSpread − spreadAskAsk − F_mt
    //     MT 开空：spreadBidBid  − mtCSStartSpread − F_mt
    //     TT 同构，F 换成 F_tt、轴换成 bidAsk / askBid
    //   净利 >= 本值才允许开仓。
    // 它补的是地板管不到的那一半：地板只保证"入场够深"，管不了"出场线在哪"。
    //   两者同时成立 ⇒ 净利 = 地板 + 5e-6 − F + 出场线 >= 本值，
    //   等价于"出场线 >= 本值 − 地板 − 5e-6 + F"（当前配置下 = 5 − 12.05 + 7 = −0.05bp）。
    // <= 0 = 关掉这一层（与 minSpreadTarget 同一约定）。
    // ⚠️ 祖先的 min_profit_target 是**毛口径**（`_q_org` 与 `_n_org` 两边 F 抵消），这里故意改成
    //    净口径 —— 否则 4bp 的毛门槛盖不住十几 bp 的往返费。
    double minProfitTarget{0.0005};
    // ⚠️ **未接线**：只解析 + 打日志，RecalcOrderParams 不读它。
    // 祖先（cc_pricespread_gb_ltp.py:774-779）用它做"按期望平仓反推开仓价"的第二分支：
    //   entry = min(本向分位 + F, 反向分位 − F − openProfitPct)   // 开多；开空对称取 max
    // C++ 只搬了第一支。要接的话先注意：地板（minSpreadTarget=0.0012）生效时它**永远不可能**
    // 成为约束 —— 那一分支只在分位带宽窄到几个 bp 时才比第一支更深，而那时地板已经把它盖住了。
    double openProfitPct{0.0001};
    double openMaxFundingRate{0.002};  // 开仓最大资金费率绝对值

};

// 信号计算结果
struct SignalResult {
    bool hasSignal{false};
    bool ttOLSignal{false};
    bool ttOSSignal{false};
    bool ttCLSignal{false};
    bool ttCSSignal{false};
    bool mtOLSignal{false};
    bool mtOSSignal{false};
    bool mtCLSignal{false};
    bool mtCSSignal{false};
    //std::string firstSignalDesc;
};


class SignalGenerator {
public:
    static SignalGenerator& Instance() {
        static SignalGenerator inst;
        return inst;
    }

    void SetConfig(const FeeSlippageConfig& cfg) {
        m_cfg = cfg;
    }

    const FeeSlippageConfig& GetConfig() const {
        return m_cfg;
    }

    // 真实执行成本 F：TT = 两腿都 taker；MT = 主动腿 maker + 被动腿 taker；滑点只计主动腿一次。
    // 与 PairTradingContext::BuildAlgoOrderJson 写入算法单的 takerTakerFs / makerTakerFs 同源，
    // 是 CheckSignal 与 AlgoPairOrder 阶梯共用的唯一成本口径。
    double CalcExecCost(bool isTT) const {
        const double activeFee = isTT ? m_cfg.activeTakerFeeRate : m_cfg.activeMakerFeeRate;
        return activeFee + m_cfg.passiveTakerFeeRate + m_cfg.basicSlippage;
    }


    // 根据价差统计计算对子的期望价差参数，填充pi.orderParams中的StartSpread/EndSpread以及OL/OS/CL/CS Switch, 在每次largeStats/smallStats更新后调用
    void RecalcOrderParams(PairInfo& pi) const;

    // 根据实时价差与orderParams判断当前时刻是否有信号，在每个价差tick到来时调用。
    // 对子已被算法单占用时返回空（不允许同一对子并发两个算法单）。
    SignalResult CheckSignal(const PairInfo& pi) const;

    // 与 CheckSignal 同源，但**不因对子已被算法单占用而早退**。
    // 供 PairTradingContext::UpdateSatisfyTime 使用：祖先的 open_satisfy_index /
    // close_satisfy_index（cc_pricespread_gb_ltp.py:900-919）是在整个 pair_info 上算的，
    // 与有没有在跑的单无关，所以 satisfy_time 在报单期间仍会被刷新。
    // 若这里跟着 CheckSignal 一起早退，有单在跑的对子 satisfyTime 永不刷新，
    // CheckAlgoOrderTimeout 会把**每个**算法单都在 150s 后撤掉（包括刚发的风控强平单）。
    // 派发侧不需要这个版本：走到派发时 hasActiveAlgoOrder 必然为 false，
    // 此时两者完全等价。
    SignalResult CheckSignalForSatisfy(const PairInfo& pi) const;

    bool CanOpen(const PairInfo& pi, std::string& reason) const;

    bool CanClose(const PairInfo& pi, std::string& reason) const;

    // 当前是否已经满足"按价差平仓"的条件 —— 即 CheckSignalForSatisfy 里
    // ttCL / mtCL / ttCS / mtCS 四个平仓分支中的任意一个已经成立。
    //
    // 存在的意义是把"价差回归了没有"这个判断**唯一化**：RiskManager 的
    // "价差不回归"风控必须与执行端用同一个轴、同一个成本口径，否则会出现
    // "风控认为没回归、算法单认为该平了"（或反过来）的错位。
    // 轴的对应关系（与 CheckSignalForSatisfy 逐字一致）：
    //   多头持仓 -> ttCL 用 spreadAskBid、mtCL 用 spreadBidBid
    //   空头持仓 -> ttCS 用 spreadBidAsk、mtCS 用 spreadAskAsk
    // 注意 ttCLSwitch/ttCSSwitch/mtCLSwitch/mtCSSwitch 恒为 true
    // （RecalcOrderParams 里写死），所以这里不必再判开关。
    bool CloseSpreadReached(const PairInfo& pi) const;

    // ---- "只平不开"的两个来源 ----
    // 落点相同（4 个开仓开关压成 false），来源不同：
    //   ① m_globalPosExceeded —— **算出来的**：所有对子持仓市值之和超过 maxPositionValue
    //      （祖先 cc_pricespread_gb_ltp.py:832 的 just_close_flag）。由 OnTimer 第 0.5 步每轮刷新。
    //   ② m_closeOnly        —— **人给的**：配置文件里的 op.pairTrading.closeOnly，可热改。
    //      由 T_signal 轮询文件、OnTimer 第 0.6 步搬进来。
    //
    // 为什么状态挂在单例上、而不是当参数传进来：
    //   RecalcOrderParams 有 4 个生产调用点（Init / OnAlgoFinished / OnTimer 第 1、2 步），
    //   每个都会把 4 个开仓开关**重新算一遍**。只在 OnTimer 里压开关的话，别的调用点会
    //   立刻把它们打开 —— 闸门只在两次调用之间的缝隙里存在，等于没有。
    //
    // 默认都是 false = 不干预：冷启动第一轮 OnTimer 之前不误伤。
    void SetGlobalPositionExceeded(bool v) { m_globalPosExceeded = v; }
    bool GetGlobalPositionExceeded() const { return m_globalPosExceeded; }

    void SetCloseOnly(bool v) { m_closeOnly = v; }
    bool GetCloseOnly() const { return m_closeOnly; }

private:
    SignalGenerator() = default;

    // 把 4 个开仓开关压成 false（平仓开关不动 = 只平不开）。三个来源任一成立就压：
    //   ① 算出来的跨对子持仓超限 m_globalPosExceeded；② 全局急停 m_closeOnly；
    //   ③ 按对子的运维意图 pi.closeFlag（单对子紧急平仓就用它）。
    // 两个调用点必须都调：统计无效时 RecalcOrderParams 会早退，而早退路径上开关
    // 保持上一轮的值 —— 恰恰是"刚重启、带着恢复的大仓位、分位数还没攒够"的时刻，
    // 最需要这道闸门。closeFlag 只写在正常出口上会漏掉这一段。
    void ApplyNoOpenGate(const PairInfo& pi, OrderParams& op) const;

    std::pair<double, double> CalcExpectSpread(double quantileBound, bool isTT, int direction) const;

    //按最小变价计的滑点
    double CalcMinMoveSlippage(double minMove, double price) const;

    FeeSlippageConfig m_cfg;

    bool m_globalPosExceeded{false};
    bool m_closeOnly{false};
};
}