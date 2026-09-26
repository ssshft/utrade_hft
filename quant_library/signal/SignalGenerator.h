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
    double activeMakerFeeRate{0.0002};
    double activeTakerFeeRate{0.0006};
    double passiveMakerFeeRate{0.0002};
    double passiveTakerFeeRate{0.0006};
    double basicSlippage{0.0001};
    double slippagePctMinMove{0.5}; // 按最小变价计算的滑点比例
    double ttAddPercent{0.0003};   // 双taker额外价差要求：只对 TT 生效，且与执行成本反向叠加，
                                   // 抵消后使 TT 入口阈值 = Q*spreadAdjPct + direction*ttAddPercent，
                                   // 即 TT 比 MT 更难成交（与祖先 tt_add_percent 同向）
    double spreadAdjPct{0.8};     // 分位数调节比例（祖先 spread_adj_pct=0.8）。
                                  // 入口阈值 = Q*spreadAdjPct，|Q*adj| 越小越容易触发：
                                  // 祖先注释「在 spread 收缩时有更多的开平机会」
    double minSpreadSpan{0.0002};   // 最小分位数差值(套利空间门槛)（祖先 min_spread_span_org=0.0002）。
                                    // 低于此值 RecalcOrderParams 直接关掉全部开仓开关并 return
    double minSpreadTarget{0.0};   // 最小期望价差(绝对值)
    double openProfitPct{0.0001};   // 开仓期望利润
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
    std::string firstSignalDesc;
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

    // 根据实时价差与orderParams判断当前时刻是否有信号，在每个价差tick到来时调用
    SignalResult CheckSignal(const PairInfo& pi) const;

    bool CanOpen(const PairInfo& pi, std::string& reason) const;

    bool CanClose(const PairInfo& pi, std::string& reason) const;

private:
    SignalGenerator() = default;

    std::pair<double, double> CalcExpectSpread(double quantileBound, bool isTT, int direction) const;

    //按最小变价计的滑点
    double CalcMinMoveSlippage(double minMove, double price) const;

    FeeSlippageConfig m_cfg;
};
}