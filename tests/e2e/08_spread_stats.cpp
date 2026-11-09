// ===========================================================================
// 08 spread 统计落库 —— 分位数存盘 / 重启读回（"重启免预热"）
//
// 为什么要有这个文件：spread_stats.csv 丢了的话，重启后分位数要重新攒 24h，
// 而这 60s 内用的是 min/max，开仓门槛会比正常宽很多（= 更容易开仓）。
// 所以"能不能读回来"和"多旧就不读"都是要钉的行为。
//
// 两条判据：
//   · 新鲜度按 **样本时间 calcTs** 算，不是按文件 mtime —— 停了一周再启动，
//     文件的 mtime 可能是刚写的（如果被重新落盘过），但样本本身已经过期。
//   · 超过 spreadStatsMaxStaleSec 的行会被跳过，对子退回"正常预热"。
// ===========================================================================
#include "harness.h"

// 用真实的 builder 攒一组样本，算出分位数 —— 不手搓 SpreadStats 的字段
static SpreadStats BuildStats(double base, int64_t ts) {
    SpreadStatsBuilder b;
    for (int i = 0; i < 10; ++i) {
        SpreadSample s;
        s.ts  = ts + i;
        s.sba = base + i * 0.001;
        s.sbb = base + i * 0.001;
        s.sab = base - i * 0.001;
        s.saa = base - i * 0.001;
        s.abv = 1000.0;
        s.aav = 1000.0;
        b.Add(s);
    }
    return b.Build(0.92, 0.08, 3);
}

static void Case() {
    const std::string path = TempPath("spread_stats.csv");

    // ---- ① 写一份"新鲜"的统计 ----
    SpreadStats st = BuildStats(-0.01, crypto::getCurrentTime());
    CHECK(st.IsValid(), "前置：样本够 -> 统计有效");

    std::vector<SpreadStatsMsg> rows(1);
    std::strncpy(rows[0].pairKey, kPairKey, sizeof(rows[0].pairKey) - 1);
    rows[0].st = st;
    CHECK(WriteSpreadStatsFile(path, rows), "WriteSpreadStatsFile 成功");

    const std::string text = ReadTextFile(path);
    CHECK(!text.empty(), "spread_stats.csv 已生成");
    {
        const std::string header = text.substr(0, text.find('\n'));
        size_t n = 1;
        for (char c : header) if (c == ',') ++n;
        CHECK(n == 12, "表头 12 列（列数不对 LoadSpreadStats 会整份丢弃）");
    }

    // ---- ② 重启：Init 会调 LoadSpreadStats ----
    {
        Fx b;
        Boot(b, /*keepState=*/true);
        PairInfo* p = b.pi();
        CHECK(p != nullptr, "重启后对子仍在");
        if (p) {
            CHECK(p->largeStats.IsValid(), "新鲜统计被读回 -> largeStats 有效（免预热）");
            CHECK(Near(p->largeStats.bidBidUQ, st.bidBidUQ, 1e-12),
                  "分位数与落盘时一致（不是重新攒出来的近似值）");
            CHECK(p->largeStats.count == st.count, "样本数一致");
        }
    }

    // ---- ③ 把样本时间拨老，超过允许的陈旧度 -> 不读 ----
    // 直接调 LoadSpreadStats 比走 Init 更能说明"是新鲜度判据在起作用"：
    // 文件内容完全一样，只是 calcTs 变旧了。
    {
        SpreadStats old = BuildStats(-0.01, crypto::getCurrentTime() - 7200 * SEC);   // 2 小时前
        std::vector<SpreadStatsMsg> oldRows(1);
        std::strncpy(oldRows[0].pairKey, kPairKey, sizeof(oldRows[0].pairKey) - 1);
        oldRows[0].st = old;
        CHECK(WriteSpreadStatsFile(path, oldRows), "写一份 2 小时前的统计");

        Fx c;
        Boot(c, /*keepState=*/true);        // spreadStatsMaxStaleSec 默认 3600
        PairInfo* p = c.pi();
        CHECK(p != nullptr && !p->largeStats.IsValid(),
              "陈旧度 2h > 3600s -> 不恢复，退回正常预热");
    }
}

int main() { return RunCase("08 spread 统计落库/重启", Case); }
