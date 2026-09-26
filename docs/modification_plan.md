# `utrade_hft` 改动方案（承接前几轮的核对结论）

> 目标：让"根据实时价差行情开仓平仓"这条链路真正跑通，并对齐祖先 `pair_trading_c_gateio` 的总体逻辑。
>
> 按依赖顺序排，**Step 0 → Step 3 必须按序做**，否则后面看不到效果。
> 本文档只给方案与代码，**尚未改动任何源码**。

---

## 改动总览

| Step | 优先级 | 文件 | 改什么 |
|---|---|---|---|
| 0 | P0 | `SignalGenerator.cpp:9` | `endSpread` 方向反了（4 个信号全反） |
| 1 | P0 | 新增 `SpreadStatsBuilder.h/.cpp` + 4 处 | 补统计生产者（前置，不补则一切无效） |
| 2 | P0 | `SignalGenerator.cpp` | `RecalcOrderParams` 补 Stage 2/3/4 |
| 3 | P1 | `SignalGenerator.cpp` | `CheckSignal` 改用 Tema |
| 4 | P1 | `SignalGenerator.h` | 常量对齐 |
| 5 | P2 | `PairInfoManager.cpp` | `rt.valid` 按新鲜度判定 |

---

## Step 0 —— 修 `endSpread` 方向（1 行，但影响阶梯是否工作）

### 问题

`SignalGenerator.cpp:9`：

```cpp
double endSpread = startSpread - direction * 0.000005; // 5e-6 缓冲带
```

`direction` 取值：OL/CS = −1，OS/CL = +1。代入后：
- OL：`end = start + 5e-6`（end > start）
- OS：`end = start − 5e-6`（end < start）
- CL：`end = start − 5e-6`
- CS：`end = start + 5e-6`

**四个全部和祖先相反。** 祖先（`pair_info_manager.py` 的 `calculate_pair_order_params`）：

```
ttOLEndSpread = ttOLStartSpread − 0.000005     # end < start
ttOSEndSpread = ttOSStartSpread + 0.000005     # end > start
ttCLEndSpread = ttCLStartSpread + 0.000005     # end > start
ttCSEndSpread = ttCSStartSpread − 0.000005     # end < start
```

### 为什么这个是 P0

`AlgoPairOrder.cpp` 的阶梯插值对 start/end 的相对大小有硬性要求，抄四段实际代码：

```cpp
// OPEN_SHORT (:289-296)
if (tempSpread < startSpread)      targetActiveVolume = 0;
else if (tempSpread > endSpread)   targetActiveVolume = endVolume;   // +maxVolume
else 插值                                                             // 需要 end > start

// CLOSE_LONG (:457-464)
if (tempSpread < startSpread)      targetActiveVolume = startVolume; // −maxVolume
else if (tempSpread > endSpread)   targetActiveVolume = endVolume;   // 0
else 插值                                                             // 需要 end > start

// OPEN_LONG (:406-413)
if (tempSpread > startSpread)      targetActiveVolume = 0;
else if (tempSpread < endSpread)   targetActiveVolume = endVolume;   // −maxVolume
else 插值                                                             // 需要 end < start

// CLOSE_SHORT (:338-345)
if (tempSpread > startSpread)      targetActiveVolume = startVolume; // +maxVolume
else if (tempSpread < endSpread)   targetActiveVolume = endVolume;   // 0
else 插值                                                             // 需要 end < start
```

| 信号 | 阶梯要求 | 祖先 | C++ 现状 |
|---|---|---|---|
| OL | end < start | end = start − 5e-6 ✓ | start + 5e-6 ✗ |
| OS | end > start | end = start + 5e-6 ✓ | start − 5e-6 ✗ |
| CL | end > start | end = start + 5e-6 ✓ | start − 5e-6 ✗ |
| CS | end < start | end = start − 5e-6 ✓ | start + 5e-6 ✗ |

现在 `[startSpread, endSpread]` 这个插值区间落在了触发区（`tempSpread < startSpread` 或 `> startSpread`）的**外面**，
所以 `targetActiveVolume` 恒被夹到 `0` 或 `endVolume`，**阶梯插值根本不会生效**。

### 改法

```cpp
// SignalGenerator.cpp:9
// 原来：double endSpread = startSpread - direction * 0.000005;
double endSpread = startSpread + direction * 0.000005; // 5e-6 缓冲带，方向与 StartVolume→EndVolume 阶梯一致
```

改完对照：OL(dir=−1) → `start − 5e-6` ✓；OS(dir=+1) → `start + 5e-6` ✓；CL ✓；CS ✓。**一行搞定。**

> 注：这一条推翻了我上一轮的判断（当时说"±5e-6 不对称是故意的、和插值一致"）。
> 不对称确实是有意的，但 C++ 实现时**符号写反了**。

---

## Step 1 —— 补统计生产者（前置）

祖先的做法（`utility/data_helper.py:update_spread_df` + `utility/pair_info_manager.py:145`）：

```python
# 每个 tick：追加
temp_dbp_data_list.append(msg)
# 每 60s：合并进 24h 滚动窗口 → 求分位数
spread_df = concat(spread_df[generateTs > now − 24h], temp_dbp_data_list)
UQ = quantile(0.92) of [sba, sbb, sab, saa]
DQ = quantile(0.08) of 同上
spread_count = count(generateTs)
active_depth_volume = mean((activeBidVolume + activeAskVolume) / 2)
```

### 1.1 新增 `quant_library/signal/SpreadStatsBuilder.h`

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "basic/PairInfo.h"

namespace pt {

// 单个价差样本（对齐祖先 temp_dbp_data_list 的元素）
struct SpreadSample {
    int64_t ts{0};
    float sba{0.f}; // spreadBidAsk
    float sbb{0.f}; // spreadBidBid
    float sab{0.f}; // spreadAskBid
    float saa{0.f}; // spreadAskAsk
    float abv{0.f}; // activeBidVolume[0]
    float aav{0.f}; // activeAskVolume[0]
};

// 滚动窗口累积 + 分位数计算
// 对齐祖先 data_helper.py:update_spread_df
//        + pair_info_manager.py:update_pair_info_by_spread_df
class SpreadStatsBuilder {
public:
    void SetWindowUs(int64_t windowUs) { m_windowUs = windowUs; }

    void Add(const SpreadSample& s) { m_samples.push_back(s); }

    // 丢弃过期样本，返回窗口内样本数
    size_t Prune(int64_t nowUs);

    size_t Size() const { return m_samples.size(); }

    // 样本数 < minSamples 时返回 valid=false
    SpreadStats Build(double quantileUp, double quantileDn, size_t minSamples) const;

private:
    // 线性插值分位数，对齐 pandas Series.quantile 默认的 linear 方法
    static double Quantile(std::vector<double>& sorted, double q);

    std::deque<SpreadSample> m_samples;
    int64_t m_windowUs{24LL * 3600 * 1000000};
};

} // namespace pt
```

### 1.2 新增 `quant_library/signal/SpreadStatsBuilder.cpp`

```cpp
#include "SpreadStatsBuilder.h"

#include <algorithm>
#include <cmath>

namespace pt {

size_t SpreadStatsBuilder::Prune(int64_t nowUs) {
    const int64_t cutoff = nowUs - m_windowUs;
    while (!m_samples.empty() && m_samples.front().ts < cutoff) {
        m_samples.pop_front();
    }
    return m_samples.size();
}

double SpreadStatsBuilder::Quantile(std::vector<double>& sorted, double q) {
    if (sorted.empty()) {
        return std::nan("");
    }
    if (sorted.size() == 1) {
        return sorted[0];
    }

    const double idx = q * static_cast<double>(sorted.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    if (hi >= sorted.size()) {
        hi = sorted.size() - 1;
    }

    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

SpreadStats SpreadStatsBuilder::Build(double quantileUp, double quantileDn, size_t minSamples) const {
    SpreadStats st;
    const size_t n = m_samples.size();
    st.count = static_cast<int>(n);

    if (n < minSamples) {
        st.valid = false;
        return st;
    }

    std::vector<double> sba, sbb, sab, saa;
    sba.reserve(n); sbb.reserve(n); sab.reserve(n); saa.reserve(n);

    double depthSum = 0.0;
    for (const auto& s : m_samples) {
        sba.push_back(s.sba);
        sbb.push_back(s.sbb);
        sab.push_back(s.sab);
        saa.push_back(s.saa);
        depthSum += (static_cast<double>(s.abv) + static_cast<double>(s.aav)) / 2.0;
    }

    std::sort(sba.begin(), sba.end());
    std::sort(sbb.begin(), sbb.end());
    std::sort(sab.begin(), sab.end());
    std::sort(saa.begin(), saa.end());

    st.bidAskUQ = Quantile(sba, quantileUp);
    st.bidAskDQ = Quantile(sba, quantileDn);
    st.bidBidUQ = Quantile(sbb, quantileUp);
    st.bidBidDQ = Quantile(sbb, quantileDn);
    st.askBidUQ = Quantile(sab, quantileUp);
    st.askBidDQ = Quantile(sab, quantileDn);
    st.askAskUQ = Quantile(saa, quantileUp);
    st.askAskDQ = Quantile(saa, quantileDn);
    st.avgDepthVolume = depthSum / static_cast<double>(n);
    st.valid = true;

    return st;
}

} // namespace pt
```

### 1.3 `PairTradingContext.h` —— 加配置与缓冲

配置（`PairTradingConfig`，放在 `spreadStatsUpdateIntervalSec` 附近）：

```cpp
    int spreadStatsWindowSec{86400};       // 24h 滚动窗口（祖先 spread_df_update_period）
    int spreadStatsUpdateIntervalSec{60};  // 60s 刷新（祖先 spread_df_update_timespan，原值 3600 偏大）
    int spreadStatsMinSamples{8640};       // 样本门槛（祖先 spread_count > 24*3600/5*0.5）
    int spreadSampleIntervalMs{200};       // 采样间隔，用于控制内存
    int spreadFreshnessSec{30};            // 行情新鲜度门槛（祖先 lastGenerateTs < 30s）
```

私有成员：

```cpp
    struct SpreadWindow {
        SpreadStatsBuilder builder;
        int64_t lastSampleTs{0};
    };
    std::unordered_map<std::string, SpreadWindow> m_spreadWindows;

    void AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata);
```

> 内存估算：24h / 200ms = 432,000 样本 × 24 B ≈ **10 MB / 对子**。
> 如果按每个 tick 全存（dbp 50ms 一帧），是 1.7M 样本 ≈ 41 MB。建议先按 200ms 采样。
> 注意：采样会略微偏离祖先的"逐 tick 等权"，若要求严格一致就把 `spreadSampleIntervalMs` 设 0（关闭降频）。

### 1.4 `PairTradingContext.cpp` —— `OnSpread` 推样本

```cpp
void PairTradingContext::OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    std::string pairKey(topic->__name);

    auto& pim = PairInfoManager::Instance();
    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pim.UpdateRtSpread(pairKey, pdata);

    AccumulateSpreadSample(pairKey, pdata);   // ← 新增

    ProcessPairSignal(*pi);
}

void PairTradingContext::AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata) {
    auto& win = m_spreadWindows[pairKey];
    win.builder.SetWindowUs(static_cast<int64_t>(m_cfg.spreadStatsWindowSec) * 1000000LL);

    const int64_t minGap = static_cast<int64_t>(m_cfg.spreadSampleIntervalMs) * 1000LL;
    if (minGap > 0 && win.lastSampleTs != 0 && pdata->generateTs - win.lastSampleTs < minGap) {
        return;
    }
    win.lastSampleTs = pdata->generateTs;

    SpreadSample s;
    s.ts  = pdata->generateTs;
    s.sba = static_cast<float>(pdata->spreadBidAsk);
    s.sbb = static_cast<float>(pdata->spreadBidBid);
    s.sab = static_cast<float>(pdata->spreadAskBid);
    s.saa = static_cast<float>(pdata->spreadAskAsk);
    s.abv = static_cast<float>(pdata->activeBidVolume[0]);
    s.aav = static_cast<float>(pdata->activeAskVolume[0]);
    win.builder.Add(s);
}
```

### 1.5 `PairTradingContext.cpp` —— `OnTimer` 算统计并立刻重算参数

把现有的「2. 全量重算 orderParams (每5分钟)」整块（`:500-506`）替换成：

```cpp
    // 2. 价差统计刷新 + 立刻全量重算 orderParams
    if (nowUs - m_lastSpreadStatsUpdateUs > m_cfg.spreadStatsUpdateIntervalSec * 1000000LL) {
        for (const auto& pk : pim.GetAllPairKeys()) {
            auto it = m_spreadWindows.find(pk);
            if (it == m_spreadWindows.end()) {
                continue;
            }

            it->second.builder.Prune(nowUs);

            SpreadStats st = it->second.builder.Build(
                m_cfg.quantileUp, m_cfg.quantileDn,
                static_cast<size_t>(m_cfg.spreadStatsMinSamples));

            if (!st.valid) {
                LOG_WARN("spread stats not ready pairKey:{} count:{}", pk, st.count);
                continue;
            }

            pim.UpdateLargeStats(pk, st);
            LOG_INFO("largeStats updated pairKey:{} count:{} sbbUQ:{} saaDQ:{}",
                     pk, st.count, st.bidBidUQ, st.askAskDQ);
        }
        m_lastSpreadStatsUpdateUs = nowUs;

        // 统计更新后立刻重算（SignalGenerator.h:67 的注释本意如此）
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            sg.RecalcOrderParams(*pi);
        }
    }
```

### 1.6 `CMakeLists.txt`

在 `quant_library/signal` 的源文件列表里加上 `SpreadStatsBuilder.cpp`。

### 1.7 `PairInfo.h` —— 顺手修默认值

`SpreadStats` 的 8 个 double 成员没有初始化，`IsValid()` 里只检查 `bidBidUQ`：

```cpp
    struct SpreadStats {
        double bidAskUQ{std::nan("")};
        double bidAskDQ{std::nan("")};
        double bidBidUQ{std::nan("")};
        double bidBidDQ{std::nan("")};
        double askBidUQ{std::nan("")};
        double askBidDQ{std::nan("")};
        double askAskUQ{std::nan("")};
        double askAskDQ{std::nan("")};
        int count{0};
        double avgDepthVolume{0};
        bool valid{false};

        bool IsValid() const {
            return valid && count > 0 && !std::isnan(bidBidUQ) && !std::isnan(askAskDQ);
        }
    };
```

`RealTimeSpread` 同理（`PairInfo.h:59-82` 全未初始化，`valid` 也未初始化）：

```cpp
    struct RealTimeSpread {
        double spreadBidAsk{std::nan("")};
        double spreadBidBid{std::nan("")};
        double spreadAskBid{std::nan("")};
        double spreadAskAsk{std::nan("")};
        double spreadBidAskTema{std::nan("")};
        double spreadBidBidTema{std::nan("")};
        double spreadAskBidTema{std::nan("")};
        double spreadAskAskTema{std::nan("")};
        double activeFundingRate{std::nan("")};
        double passiveFundingRate{std::nan("")};
        // ... 其余保持
        bool valid{false};
    };
```

---

## Step 2 —— `RecalcOrderParams` 补 Stage 2/3/4

### 2.1 需要先确认的一件事：祖先的信号层只有 4 路，不是 8 路

读 `pair_info_manager.py` 的 `calculate_pair_order_params` 会发现：

```python
OS_true = pair_info['mt_except_open_short_spread'] > 0.0
OL_true = pair_info['mt_except_open_long_spread'] < 0.0
pair_info.loc[auto_index & OS_true & close_flag, 'ttOSSwitch'] = True   # tt 和 mt 用同一个 OS_true
pair_info.loc[auto_index & OS_true & close_flag, 'mtOSSwitch'] = True
```

而且 `except_open_long_flag` / `except_close_long_flag` 这几个**触发标志**也全部是用 `mt_*` 口径算的。
**TT 和 MT 的区分不在信号层，而在算法单内部**——`AlgoPairOrder.cpp:174-255` 用
`takerTakerFs` / `makerTakerFs` 分别作用到各自的价差字段上，再由阶梯决定报不报。

也就是说：**祖先的信号层是 4 路（MT 口径），C++ 现在是 8 路。**
C++ 等于在算法单的门槛之外又加了一道 8 路的门，两道理应一致，不一致就会互相打架。

→ **这一条需要你定：**
- **选项 A（忠实祖先）**：信号层只算 4 路（MT 口径），tt/mt 开关由同一组条件驱动，TT 的实际区别交给算法单内部。改动小、语义清晰。
- **选项 B（保留 C++ 8 路）**：TT 继续用 BidAsk/AskBid 的实际交叉价差 + taker 费，各自算一套。更"精确"，但与祖先语义不同，且两道门可能互相拦截。

下面代码按**选项 A** 写；选 B 的话把 TT 那 4 段用 taker 费率单独算一遍即可（结构一样）。

### 2.2 新增两个私有辅助

`SignalGenerator.h` 的 `private:` 段加：

```cpp
    struct PairCost {
        double feesLong{0.0};   // 主动腿空 + 被动腿多
        double feesShort{0.0};  // 主动腿多 + 被动腿空
    };

    struct FundingAdj {
        double openLongFunding{0.0};   // open_long_funding_profit
        double openShortFunding{0.0};
        double adjLong{0.0};           // open_long_funding_profit_adj
        double adjShort{0.0};
    };

    PairCost CalcPairCost(const PairInfo& pi) const;
    FundingAdj CalcFundingAdj(const PairInfo& pi, int64_t nowUs) const;
    double CalcAdjProfit(const PairInfo& pi, double curOL, double curOS) const;
```

`SignalGenerator.cpp` 实现（对齐祖先 `:646-648`、`:655-687`、`:803-851`）：

```cpp
SignalGenerator::PairCost SignalGenerator::CalcPairCost(const PairInfo& pi) const {
    const auto& rt = pi.rtSpread;

    // 祖先：maker 滑点 = 0，taker 滑点 = pct * minMove / priceTema + basicSlippage
    const double slipAM = 0.0;
    const double slipPT = CalcMinMoveSlippage(pi.passiveParam.minMove, rt.passivePriceTema)
                        + m_cfg.basicSlippage;

    PairCost c;
    // long 侧：主动腿做 maker 卖、被动腿做 taker 买
    c.feesLong  =  m_cfg.activeMakerFeeRate + m_cfg.passiveTakerFeeRate + slipAM + slipPT;
    c.feesShort = -m_cfg.activeMakerFeeRate - m_cfg.passiveTakerFeeRate - slipAM - slipPT;
    return c;
}

SignalGenerator::FundingAdj SignalGenerator::CalcFundingAdj(const PairInfo& pi, int64_t nowUs) const {
    const auto& rt = pi.rtSpread;

    const double aFR = std::isnan(rt.activeFundingRate)  ? 0.0 : rt.activeFundingRate;
    const double pFR = std::isnan(rt.passiveFundingRate) ? 0.0 : rt.passiveFundingRate;
    const double aInt = rt.activeFundingInterval  > 0 ? static_cast<double>(rt.activeFundingInterval)  : 8.0;
    const double pInt = rt.passiveFundingInterval > 0 ? static_cast<double>(rt.passiveFundingInterval) : 8.0;

    FundingAdj f;
    // funding 赚钱不计入期望收益，亏钱按 1 期计
    f.openLongFunding  = std::min(aFR / aInt * 480.0 - pFR / pInt * 480.0, 0.0);
    f.openShortFunding = std::min(pFR / pInt * 480.0 - aFR / aInt * 480.0, 0.0);

    f.adjLong  = f.openLongFunding;
    f.adjShort = f.openShortFunding;

    // funding 绝对值大时，越靠近结算时间越要放大亏损预期
    const double maxAbsFR = std::max(std::abs(aFR), std::abs(pFR));
    if (maxAbsFR > 0.0015) {
        const int64_t nextTs = std::min(rt.activeFundingRateTime, rt.passiveFundingRateTime);
        const double hoursLeft = static_cast<double>(nextTs - nowUs) / 1.0e6 / 3600.0;

        double penalty = 0.0;
        if (hoursLeft > 0.0 && hoursLeft < 1.0) {
            penalty = 0.2 * hoursLeft * maxAbsFR;
        } else if (hoursLeft >= 1.0 && hoursLeft < 2.0) {
            penalty = 0.2 * maxAbsFR;
        } else if (hoursLeft > 2.0) {
            penalty = 0.3 * maxAbsFR;
        }

        if (penalty > 0.0) {
            f.adjLong  = std::min(-penalty, f.openLongFunding);
            f.adjShort = std::min(-penalty, f.openShortFunding);
        }
    }
    return f;
}

// 对齐祖先 :803-851：持仓越满，平仓盈利要求越松
double SignalGenerator::CalcAdjProfit(const PairInfo& pi, double curOL, double curOS) const {
    if (pi.pairActiveTotalPrice <= 0.0 || pi.pairPassiveTotalPrice <= 0.0) {
        return 0.0;
    }

    const double priceRatio = pi.pairPassiveTotalPrice / pi.pairActiveTotalPrice;

    double closeProfit = 0.0;
    if (pi.pairTotalVolume <= -pi.minVolume) {
        closeProfit = curOS - (priceRatio - 1.0 + pi.profitPct);
    } else if (pi.pairTotalVolume >= pi.minVolume) {
        closeProfit = -curOL + (priceRatio - 1.0 - pi.profitPct);
    }
    closeProfit = std::max(closeProfit, 0.0);

    // TODO: adjPct 的档位来源待确认（祖先按 positionValue / (leverage * accountPnl) 分档）
    const double adjPct = m_cfg.adjProfitPct;

    double adj = std::min(closeProfit * adjPct, pi.profitPct);
    return std::max(adj, 0.0);
}
```

`FeeSlippageConfig` 加一个字段：

```cpp
    double adjProfitPct{0.0};   // 平仓盈利要求的动态调节系数（0 = 退化为固定 profitPct）
```

### 2.3 主体重写

```cpp
void SignalGenerator::RecalcOrderParams(PairInfo& pi) const {
    auto& op = pi.orderParams;
    const auto& ls = pi.largeStats;
    const auto& rt = pi.rtSpread;

    if (!ls.IsValid()) {
        return;
    }

    const double qAdj = m_cfg.spreadAdjPct;

    // ---------- Stage 1: 分位数 ----------
    const double sbbUQ = ls.bidBidUQ;
    const double saaDQ = ls.askAskDQ;

    // 套利空间门槛（对齐祖先 spread_span_flag）
    const double spreadSpan = sbbUQ - saaDQ;
    if (spreadSpan < m_cfg.minSpreadSpan) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
        return;
    }

    // ---------- 成本与 funding ----------
    const PairCost   cost = CalcPairCost(pi);
    const FundingAdj fund = CalcFundingAdj(pi, crypto::getCurrentTime());
    const double openProfit = m_cfg.openProfitPct;

    // ---------- Stage 2: 期望锁定价差（对齐祖先 :690-726）----------
    // long 侧（主动腿空 + 被动腿多）→ 用 spreadAskAsk 的 DQ
    const double olQOrg  = saaDQ * qAdj + cost.feesLong;
    const double olQOpen = saaDQ * qAdj + cost.feesLong + 2.0 * fund.openLongFunding;
    const double olQ     = saaDQ * qAdj + cost.feesLong + 0.5 * fund.openLongFunding;
    // short 侧（主动腿多 + 被动腿空）→ 用 spreadBidBid 的 UQ
    const double osQOrg  = sbbUQ * qAdj + cost.feesShort;
    const double osQOpen = sbbUQ * qAdj + cost.feesShort - 2.0 * fund.openShortFunding;
    const double osQ     = sbbUQ * qAdj + cost.feesShort - 0.5 * fund.openShortFunding;

    // ---------- Stage 3: 开仓目标价差（两路 min/max，对齐祖先 :774-782）----------
    double mtOLTarget = std::min(olQOpen, osQOrg + 2.0 * fund.adjLong  - openProfit);
    double mtOSTarget = std::max(osQOpen, olQOrg - 2.0 * fund.adjShort + openProfit);
    // min_spread_target 夹逼（硬性要求 |target| ≥ 5e-5）
    mtOLTarget = std::min(mtOLTarget, -(m_cfg.minSpreadTarget + 0.00005));
    mtOSTarget = std::max(mtOSTarget,  (m_cfg.minSpreadTarget + 0.00005));

    // ---------- adj_profit（对齐祖先 :849-855）----------
    const double curOL = rt.spreadAskAsk + cost.feesLong;
    const double curOS = rt.spreadBidBid + cost.feesShort;
    const double adjProfit = CalcAdjProfit(pi, curOL, curOS);

    // ---------- 写入 orderParams ----------
    // 右侧扣掉本侧成本，使 CheckSignal 只做「价差 vs 价差」比较
    op.mtOLStartSpread = mtOLTarget - cost.feesLong;
    op.mtOSTartSpread  = mtOSTarget - cost.feesShort;
    op.mtCLStartSpread = (osQ - adjProfit) - cost.feesShort;
    op.mtCSStartSpread = (olQ + adjProfit) - cost.feesLong;

    // EndSpread 方向与阶梯一致（见 Step 0）
    op.mtOLEndSpread = op.mtOLStartSpread - 0.000005;
    op.mtOSEndSpread = op.mtOSStartSpread + 0.000005;
    op.mtCLEndSpread = op.mtCLStartSpread + 0.000005;
    op.mtCSEndSpread = op.mtCSStartSpread - 0.000005;

    op.mtOLStartVolume = 0.0;          op.mtOLEndVolume = -pi.maxVolume;
    op.mtOSStartVolume = 0.0;          op.mtOSEndVolume =  pi.maxVolume;
    op.mtCLStartVolume = -pi.maxVolume; op.mtCLEndVolume = 0.0;
    op.mtCSStartVolume =  pi.maxVolume; op.mtCSEndVolume = 0.0;

    // 选项 A：TT 复用 MT 口径
    op.ttOLStartSpread = op.mtOLStartSpread;
    op.ttOLEndSpread   = op.mtOLEndSpread;
    op.ttOSStartSpread = op.mtOSStartSpread;
    op.ttOSEndSpread   = op.mtOSEndSpread;
    op.ttCLStartSpread = op.mtCLStartSpread;
    op.ttCLEndSpread   = op.mtCLEndSpread;
    op.ttCSStartSpread = op.mtCSStartSpread;
    op.ttCSEndSpread   = op.mtCSEndSpread;

    op.ttOLStartVolume = 0.0;           op.ttOLEndVolume = -pi.maxVolume;
    op.ttOSStartVolume = 0.0;           op.ttOSEndVolume =  pi.maxVolume;
    op.ttCLStartVolume = -pi.maxVolume; op.ttCLEndVolume = 0.0;
    op.ttCSStartVolume =  pi.maxVolume; op.ttCSEndVolume = 0.0;

    // ---------- 开关（对齐祖先 calculate_pair_order_params 后半段）----------
    const bool canOpenLong  = (mtOLTarget < -m_cfg.minSpreadTarget) && !pi.closeFlag;
    const bool canOpenShort = (mtOSTarget >  m_cfg.minSpreadTarget) && !pi.closeFlag;

    op.ttOLSwitch = op.mtOLSwitch = canOpenLong  && pi.autoFlag;
    op.ttOSSwitch = op.mtOSSwitch = canOpenShort && pi.autoFlag;

    op.ttCLSwitch = op.ttCSSwitch = true;
    op.mtCLSwitch = op.mtCSSwitch = true;

    // 手动模式：全部关掉
    if (!pi.autoFlag) {
        op.ttOLSwitch = op.ttOSSwitch = op.ttCLSwitch = op.ttCSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = op.mtCLSwitch = op.mtCSSwitch = false;
    }

    // 流动性 / margin 风险，禁止开仓
    if (pi.activeLiquidStatus > 0 || pi.passiveLiquidStatus > 0 ||
        pi.activeMarginStatus > 0 || pi.passiveMarginStatus > 0) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }

    // 资金费率过大，禁止开仓
    const double aFR = std::isnan(rt.activeFundingRate)  ? 0.0 : rt.activeFundingRate;
    const double pFR = std::isnan(rt.passiveFundingRate) ? 0.0 : rt.passiveFundingRate;
    if (std::abs(aFR) > m_cfg.openMaxFundingRate || std::abs(pFR) > m_cfg.openMaxFundingRate) {
        op.ttOLSwitch = op.ttOSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = false;
    }
}
```

> ⚠️ 注意：`mtCLStartSpread` / `mtCSStartSpread` 同时被 `AlgoPairOrder.cpp:600-623`
> 用来算 `pairTargetSpreadProfit`（如 `ttOSStartSpread - ttCSStartSpread`）。
> 扣掉成本后这两个值的含义变了，**这条链要一起核对**。

---

## Step 3 —— `CheckSignal` 改用 Tema

祖先的触发侧用的是 `*_n_org`，而 `*_n_org` 是拿 **Tema** 算的（`targetSpreadType = NOW_MEAN`）。
C++ 现在用瞬时值，且 `rt.spreadXxxTema` 在信号层**一次没被读过**（只有 `AlgoPairOrder` 拆单时读）。

改 `SignalGenerator.cpp:177-215`，把 8 行的 `rt.spreadXxx` 换成对应的 Tema：

```cpp
    // OL / CS 走 AskAsk 的 Tema；OS / CL 走 BidBid 的 Tema
    if (op.ttOLSwitch && rt.spreadAskAskTema < op.ttOLStartSpread && vol > op.ttOLEndVolume + 1e-9) { ... }
    if (op.ttOSSwitch && rt.spreadBidBidTema > op.ttOSStartSpread && vol < op.ttOSEndVolume - 1e-9) { ... }
    if (op.ttCLSwitch && pi.IsLong()  && rt.spreadBidBidTema > op.ttCLStartSpread && vol < op.ttCLEndVolume - 1e-9) { ... }
    if (op.ttCSSwitch && pi.IsShort() && rt.spreadAskAskTema < op.ttCSStartSpread && vol > op.ttCSEndVolume + 1e-9) { ... }
    if (op.mtOLSwitch && rt.spreadAskAskTema < op.mtOLStartSpread && vol > op.mtOLEndVolume + 1e-9) { ... }
    if (op.mtOSSwitch && rt.spreadBidBidTema > op.mtOSStartSpread && vol < op.mtOSEndVolume - 1e-9) { ... }
    if (op.mtCLSwitch && pi.IsLong()  && rt.spreadBidBidTema > op.mtCLStartSpread && vol < op.mtCLEndVolume - 1e-9) { ... }
    if (op.mtCSSwitch && pi.IsShort() && rt.spreadAskAskTema < op.mtCSStartSpread && vol > op.mtCSEndVolume + 1e-9) { ... }
```

同时补行情新鲜度判定（Step 5）：

```cpp
    if (!pi.rtSpread.valid) {
        return result;
    }

    const int64_t nowUs = crypto::getCurrentTime();
    if (pi.rtSpread.lastGenerateTs <= 0 ||
        nowUs - pi.rtSpread.lastGenerateTs > static_cast<int64_t>(m_cfg.spreadFreshnessSec) * 1000000LL) {
        return result;   // 行情断档，不出信号
    }
```

---

## Step 4 —— 常量对齐

`SignalGenerator.h:20-34`：

| 字段 | 现值 | 改成（祖先） |
|---|---|---|
| `activeMakerFeeRate` | 0.0002 | **−0.00015**（GATEIO maker 返佣，符号是关键） |
| `activeTakerFeeRate` | 0.0006 | 0.0002 |
| `passiveMakerFeeRate` | 0.0002 | 0.00002 |
| `passiveTakerFeeRate` | 0.0006 | 0.0002 |
| `basicSlippage` | 0.0001 | 0.00025 |
| `slippagePctMinMove` | 0.5 | 1.0 |
| `ttAddPercent` | 0.0003 | 0.0003 ✓ |
| `spreadAdjPct` | 0.95 | **0.8** |
| `minSpreadSpan` | 0.0003 | **0.0002** |
| `minSpreadTarget` | 0.0 | 0.0 ✓ |
| `openProfitPct` | 0.0001 | **0.0004** |
| `openMaxFundingRate` | 0.002 | 0.002 ✓ |

`PairTradingContext.h`：

| 字段 | 现值 | 改成 |
|---|---|---|
| `quantileUp` | 0.9 | **0.92** |
| `quantileDn` | 0.1 | **0.08** |
| `spreadStatsUpdateIntervalSec` | 3600 | **60** |
| `algoOrderTimeoutMs` | 30000（未使用） | **150000** 并接上使用 |
| `signalRecalcIntervalSec` | 300 | 由 Step 1.5 的统计周期接管 |

`PairInfo.h:233`：

| 字段 | 现值 | 改成 |
|---|---|---|
| `profitPct` | 0.0001 | 0.00025 |

> ⚠️ 费率/滑点是**交易所相关**的，上面是祖先在 GATEIO/BINANCE 上的取值。
> 如果 C++ 要跑别的交易所，这几个值必须按实际费率重填，不能照抄。

---

## Step 5 —— `rt.valid` 按新鲜度判定

`PairInfoManager.cpp:155` 现在是无条件 `rt.valid = true`，而 `dbsnap.h:289-300` 明确会算
`spreadEffective`（断档 > 500ms 或两腿时间差过大时为 false）。两个选择：

- **最小改法**：保留 `rt.valid = true`，只在 `CheckSignal` 里按 `lastGenerateTs` 判新鲜度（Step 3 已含）。
- **更完整**：把 `pdata->spreadEffective` 也带上（`DbpData` 有该字段），`rt.valid = pdata->spreadEffective`。

建议先做最小改法，`spreadEffective` 之后单独一轮。

---

## 待你定的两件事

1. **TT 怎么处理**（Step 2.1 的选项 A / B）
   - A：信号层 4 路（MT 口径），TT 复用；忠实祖先，改动小。
   - B：保留 8 路，TT 用 BidAsk/AskBid + taker 费；更精确但语义偏离祖先，且与算法单内部的门槛重复。
2. **`adjProfitPct` 的档位来源**
   祖先按 `positionValue / (leverage * accountPnl)` 分 0.95 / 0.9 两档 → 0.2 / 0.1。
   C++ 侧对应的量（`pi.totalPnl` / `pi.maxExposure` / 杠杆）需要你确认怎么映射；
   暂时把 `adjProfitPct` 默认 0，`adj_profit` 退化为 0（等于当前行为），不影响其它改动。

---

## 建议执行顺序

```
Step 0（1 行）→ Step 1（统计生产者）→ 跑起来看 largeStats 有没有值
              → Step 2（补 Stage 2/3/4）→ Step 3（Tema）→ Step 4（常量）
```

Step 0 和 Step 1 做完就应该能看到 `RecalcOrderParams` 不再在第 25 行 early-return，
`orderParams` 的 32 个字段开始有真实值。**先验证到这一步，再往下改。**
