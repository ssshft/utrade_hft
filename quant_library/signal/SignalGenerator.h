/***
 * 
 * 
 * 
 * 
 * 核心思路：
 * 期望开仓价差 = 分位数边界 - 手续费 - 滑点 - 额外缓冲
 * 只有实时价差穿越期望价差时才生成开仓信号
 * 
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
    double ttAddPercent{0.0003};   // 双taker额外价差要求
    double spreadAdjPct{0.95};    // 分位数调节比例
    double minSpreadSpan{0.0003};   // 最小分位数差值(套利空间门槛)
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


    // 根据价差统计计算对子的期望价差参数，填充pi.orderParams中的StartSpread/EndSpread以及OL/OS/CL/CS Switch, 在每次largeStats/smallStats更新后调用
    void RecalcOrderParams(PairInfo& pi) const;

    // 根据实时价差与orderParams判断当前时刻是否有信号，在每个价差tick到来时调用
    SignalResult CheckSignal(const PairInfo& pi) const;

    bool CanOpen(const PairInfo& pi, std::string& reason) const;

    bool CanClose(const PairInfo& pi, std::string reason) const;

private:
    SignalGenerator() = default;

    std::pair<double, double> CalcExpectSpread(double quantileBound, double activeFee, double passiveFee, bool isTaker, double slippage, double extraBuffer, int direction) const;

    //按最小变价计的滑点
    double CalcMinMoveSlippage(double minMove, double price) const;

    FeeSlippageConfig m_cfg;
};
}