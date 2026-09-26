#include "SpreadStatsBuilder.h"

#include <algorithm>
#include <cmath>

namespace pt {

size_t SpreadStatsBuilder::Prune(int64_t nowUs) {
    const int64_t cutoff = nowUs - m_windowUs;

    // 样本按 ts 递增入队，所以过期的一定在头部，pop 到第一个未过期为止
    while (!m_samples.empty() && m_samples.front().ts < cutoff) {
        m_samples.pop_front();
    }

    return m_samples.size();
}

double SpreadStatsBuilder::QuantileSorted(const std::vector<double>& sorted, double q) {
    const size_t n = sorted.size();
    if (n == 0) {
        return std::nan("");
    }

    if (q <= 0.0) {
        return sorted.front();
    }
    if (q >= 1.0) {
        return sorted.back();
    }

    // pandas: pos = (n - 1) * q; base = floor(pos); rest = pos - base
    //         result = s[base] + rest * (s[base + 1] - s[base])
    const double pos = q * static_cast<double>(n - 1);
    const size_t base = static_cast<size_t>(std::floor(pos));
    const double rest = pos - static_cast<double>(base);

    if (base + 1 >= n) {
        return sorted[n - 1];
    }

    return sorted[base] + rest * (sorted[base + 1] - sorted[base]);
}

SpreadStats SpreadStatsBuilder::Build(double quantileUp, double quantileDn, size_t minSamples) const {
    SpreadStats st;

    // 第一遍：只数有效样本（4 个价差全部有限）。
    // dbp 断档或某条腿没行情时可能出现 NaN，NaN 参与排序会污染整个分位数，
    // 所以这里按"样本级"整条剔除，而不是逐个字段过滤。
    size_t n = 0;
    for (const auto& s : m_samples) {
        if (std::isfinite(s.sba) && std::isfinite(s.sbb) &&
            std::isfinite(s.sab) && std::isfinite(s.saa)) {
            ++n;
        }
    }

    st.count = static_cast<int>(n);

    // 样本数门槛（祖先 spread_count > 24*3600/5*0.5 = 8640）
    if (n < minSamples) {
        st.valid = false;
        return st;
    }

    // 复用同一块 scratch，避免 4 份 43 万样本的重复分配
    std::vector<double> buf;
    buf.reserve(n);

    // 填序列 -> 排序 -> 取上下分位数
    auto quantilePair = [&](auto getter, double& uq, double& dq) {
        buf.clear();
        for (const auto& s : m_samples) {
            if (std::isfinite(s.sba) && std::isfinite(s.sbb) &&
                std::isfinite(s.sab) && std::isfinite(s.saa)) {
                buf.push_back(getter(s));
            }
        }
        std::sort(buf.begin(), buf.end());
        uq = QuantileSorted(buf, quantileUp);
        dq = QuantileSorted(buf, quantileDn);
    };

    quantilePair([](const SpreadSample& s) { return static_cast<double>(s.sba); }, st.bidAskUQ, st.bidAskDQ);
    quantilePair([](const SpreadSample& s) { return static_cast<double>(s.sbb); }, st.bidBidUQ, st.bidBidDQ);
    quantilePair([](const SpreadSample& s) { return static_cast<double>(s.sab); }, st.askBidUQ, st.askBidDQ);
    quantilePair([](const SpreadSample& s) { return static_cast<double>(s.saa); }, st.askAskUQ, st.askAskDQ);

    // 主动腿盘口深度均值，对齐祖先 active_depth_volume = mean((activeBidVolume + activeAskVolume) / 2)
    double depthSum = 0.0;
    for (const auto& s : m_samples) {
        if (std::isfinite(s.sba) && std::isfinite(s.sbb) &&
            std::isfinite(s.sab) && std::isfinite(s.saa)) {
            depthSum += (static_cast<double>(s.abv) + static_cast<double>(s.aav)) / 2.0;
        }
    }
    st.avgDepthVolume = depthSum / static_cast<double>(n);

    st.valid = true;
    return st;
}

} // namespace pt
