/***
 *
 * 价差统计生产者 --- 滚动窗口累积 + 分位数计算
 *
 * 对齐祖先 pair_trading_c_gateio：
 *   utility/data_helper.py          : update_spread_df()          -- 24h 滚动窗口累积
 *   utility/pair_info_manager.py    : update_pair_info_by_spread_df() -- 8 个分位数 + count + 深度
 *
 * 用法（由 PairTradingContext 驱动，本类不持有配置）：
 *   OnSpread  : win.builder.SetWindowUs(...); win.builder.Add(sample);
 *   OnTimer   : win.builder.Prune(nowUs);
 *               SpreadStats st = win.builder.Build(0.92, 0.08, 8640);
 *               if (st.valid) PairInfoManager::UpdateLargeStats(pairKey, st);
 *
 * 说明：
 *   - 只负责"攒样本 + 算分位数"，不做任何 PairInfo / 配置耦合，方便单测。
 *   - 分位数用线性插值，对齐 pandas Series.quantile 的默认 linear 方法
 *     （不是 include/statistic.h 里的 nearest-rank，两者在 n 较小时差别明显）。
 *
 * ***/
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "../basic/PairInfo.h"

namespace pt {

// 单个价差样本
// 字段刻意收窄成 float：24h @200ms ≈ 43 万条，double 会翻倍到 ~28MB/对子
struct SpreadSample {
    int64_t ts{0};
    float sba{0.f}; // spreadBidAsk
    float sbb{0.f}; // spreadBidBid
    float sab{0.f}; // spreadAskBid
    float saa{0.f}; // spreadAskAsk
    float abv{0.f}; // activeBidVolume[0]
    float aav{0.f}; // activeAskVolume[0]
};

class SpreadStatsBuilder {
public:
    // 窗口长度，单位微秒。默认 24h
    void SetWindowUs(int64_t windowUs) {
        if (windowUs > 0) {
            m_windowUs = windowUs;
        }
    }

    int64_t GetWindowUs() const {
        return m_windowUs;
    }

    void Add(const SpreadSample& s) {
        m_samples.push_back(s);
    }

    // 丢弃窗口外的样本（deque 头部有序，逐个 pop 即可）。返回剩余样本数
    size_t Prune(int64_t nowUs);

    size_t Size() const {
        return m_samples.size();
    }

    void Clear() {
        m_samples.clear();
    }

    // 计算分位数快照。
    // 有效样本数 < minSamples 时返回 valid=false（祖先的 spread_need_percent 门槛）
    SpreadStats Build(double quantileUp, double quantileDn, size_t minSamples) const;

private:
    // 已排序序列上的线性插值分位数，对齐 pandas Series.quantile 的 linear 方法
    static double QuantileSorted(const std::vector<double>& sorted, double q);

    // 头部最老、尾部最新
    std::deque<SpreadSample> m_samples;
    int64_t m_windowUs{24LL * 3600 * 1000000};
};

} // namespace pt
