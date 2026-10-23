// ---------------------------------------------------------------------------
// bench.cpp —— 量「算法执行线程」上那几个可疑操作的**实际代价**
//
// 用法（不需要第三方库，macOS / Ubuntu 都能跑）：
//     cd utrade_hft/tests/thread_cost && ./run.sh
//
// 被测对象（全部来自真实源码 quant_library/basic/PairInfoManager.cpp，零改写）：
//   1. SaveSnapshot          —— OnTimer 第 6 步，每 csvSaveIntervalSec(默认10s) 一次
//   2. WriteSpreadStatsFile  —— T_signal 每轮统计更新(默认60s) 一次（已搬离执行线程）
//   3. GetAllPairInfos       —— OnTimer 里被调用 5 次/秒（返回 vector<PairInfo*> 按值）
//   4. 拼 pair 键            —— BaseAlgoOrder::PairOrderTrade 每报一单拼一次
//   5. 取数到 POD 行         —— 落盘搬走后，执行线程**仍然要付**的那部分（快照 / 统计各一份）
//
// 为什么用「真实源码 + 桩」而不是抄一份同构代码：
//   同构代码只能证明「同构代码很慢」，证明不了生产代码很慢。这里链的是
//   quant_library/basic/PairInfoManager.cpp 本体，只把外部依赖（dbp / smc /
//   LOG_* / fmt）换成桩，见 tests/market_to_algo/stubs/。
//
// 本机（macOS / APFS）与部署机（Ubuntu / ext4）的**绝对**数字不同，
// 尤其 rename 与 fsync 语义；所以下面每项都同时给出「格式化的成本」
// 和「格式化的成本 + 落盘」，比值比绝对值更有参考价值。
// ---------------------------------------------------------------------------

// 只为读到 m_pairInfoMap / m_pairKeys 这两个私有容器 —— 测试专用写法，
// 与 tests/market_to_algo/suite.cpp 的做法一致。生产代码不这么干。
#define private public

#include "basic/PairInfoManager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// 跑 N 次取中位数，避免单次抖动（文件系统、页缓存状态）
template <typename Fn>
double medianUs(int iters, Fn&& fn) {
    std::vector<double> v;
    v.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        v.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// 造一个长度接近生产值的 pair 键：两家交易所 + 两个合约名
// 生产实测形如 "BINANCE_SWAP_BTC-USDT|OKX_SWAP_BTC-USDT"（33 字符）
std::string MakePairKey(int i) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "BINANCE_SWAP_%03dUSDT|OKX_SWAP_%03dUSDT", i, i);
    return std::string(buf);
}

// 用 N 个对子填满单例。直接写内部容器，绕开 Init 里的合约注册链
// —— 这里只想量「写盘/格式化/遍历」的代价，不关心合约信息从哪来。
void FillWorld(int n) {
    auto& pim = pt::PairInfoManager::Instance();
    pim.m_pairInfoMap.clear();
    pim.m_pairKeys.clear();
    pim.m_pairInfoMap.reserve(static_cast<size_t>(n));
    pim.m_pairKeys.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        const std::string key = MakePairKey(i);
        pt::PairInfo pi;
        std::snprintf(pi.pairInstrumentKey, sizeof(pi.pairInstrumentKey), "%s", key.c_str());
        // 让数值有真实位数（17 位有效数字是 SaveSnapshot 的 setprecision）
        pi.pairTotalVolume      = -12.345678901234567 + i * 1e-6;
        pi.pairActiveTotalPrice = 60123.456789012345 + i * 0.001;
        pi.pairPassiveTotalPrice= 60125.987654321098 + i * 0.001;
        pi.profitPct            = 0.000123456789012345;
        pi.currentAlgoOrderId   = 1000000 + i;
        pi.hasActiveAlgoOrder   = (i % 3 == 0);
        pi.autoFlag             = true;
        pi.stopFlag             = (i % 7 == 0);

        // 分位数统计：SaveSpreadStats 只写 IsValid() 为真的行
        pi.largeStats.valid = true;
        pi.largeStats.bidAskUQ = 0.000123456789012345;
        pi.largeStats.bidAskDQ = -0.000987654321098765;
        pi.largeStats.bidBidUQ = 0.000223456789012345;
        pi.largeStats.bidBidDQ = -0.000887654321098765;
        pi.largeStats.askBidUQ = 0.000323456789012345;
        pi.largeStats.askBidDQ = -0.000787654321098765;
        pi.largeStats.askAskUQ = 0.000423456789012345;
        pi.largeStats.askAskDQ = -0.000687654321098765;
        pi.largeStats.count    = 8640;
        pi.largeStats.avgDepthVolume = 12345.678901234567;
        pi.largeStats.calcTs   = 1761000000000000LL + i;

        pim.m_pairKeys.push_back(key);
        pim.m_pairInfoMap.emplace(key, pi);
    }
}

// 把 FillWorld 造出来的世界收成 WriteSpreadStatsFile 要的形状（只收有效统计，
// 与生产里 RunSignalOnce 的过滤规则一致）。
std::vector<pt::SpreadStatsMsg> BuildStatsMsgs() {
    auto& pim = pt::PairInfoManager::Instance();
    std::vector<pt::SpreadStatsMsg> v;
    v.reserve(pim.m_pairKeys.size());
    for (const auto& pk : pim.m_pairKeys) {
        auto it = pim.m_pairInfoMap.find(pk);
        if (it == pim.m_pairInfoMap.end()) continue;
        if (!it->second.largeStats.IsValid()) continue;
        pt::SpreadStatsMsg m{};
        std::snprintf(m.pairKey, sizeof(m.pairKey), "%s", it->second.pairInstrumentKey);
        m.st = it->second.largeStats;
        v.push_back(m);
    }
    return v;
}

} // namespace

int main() {
    const char* dir = "/tmp/thread_cost";
    std::string mk = std::string("mkdir -p ") + dir;
    if (std::system(mk.c_str()) != 0) {
        std::fprintf(stderr, "mkdir failed\n");
        return 1;
    }

    const std::string csvPath   = std::string(dir) + "/pair_info.csv";
    const std::string statsPath = std::string(dir) + "/spread_stats.csv";

    std::printf("对子数   SaveSnapshot(us)   WriteSpreadStats(us)   GetAllPairInfos(us)\n");
    std::printf("---------------------------------------------------------------------\n");

    double snap500 = 0.0;   // 供下面「500 行拆解」对比用，不写死常数
    for (int n : {50, 200, 500, 1000}) {
        FillWorld(n);
        auto& pim = pt::PairInfoManager::Instance();
        const std::vector<pt::SpreadStatsMsg> msgs = BuildStatsMsgs();

        // 预热一次：让 ofstream 的内部缓冲、页缓存、目录项都进入稳态
        pim.SaveSnapshot(csvPath);
        pt::WriteSpreadStatsFile(statsPath, msgs);

        const double tSnap  = medianUs(15, [&] { pim.SaveSnapshot(csvPath); });
        const double tStats = medianUs(15, [&] { pt::WriteSpreadStatsFile(statsPath, msgs); });
        const double tAll   = medianUs(200, [&] {
            std::vector<pt::PairInfo*> v = pim.GetAllPairInfos();
            // 防止编译器把整个调用优化掉
            if (v.size() == 0xdeadbeef) std::printf("x");
        });

        std::printf("%6d   %16.1f   %20.1f   %18.2f\n", n, tSnap, tStats, tAll);
        if (n == 500) snap500 = tSnap;
    }

    // 单独量「拼 pair 键」：每报一单一次
    {
        const std::string a = "BINANCE_SWAP_BTC-USDT";
        const std::string b = "OKX_SWAP_BTC-USDT";
        const double tKey = medianUs(20000, [&] {
            std::string k = a + "|" + b;
            if (k.size() == 0xdeadbeef) std::printf("x");
        });
        std::printf("\n拼 pair 键 (string+string): %.3f us/次\n", tKey);
    }

    // 对照 1：同样 500 行、同样 17 位有效数字、同样字段数，但只格式化不落盘。
    // 目的：把「格式化的成本」与「ofstream + rename 的成本」分开。
    // 字段顺序照抄 PairInfoManager.cpp:233-252，否则行宽不同、比较没意义。
    {
        FillWorld(500);
        auto& pim = pt::PairInfoManager::Instance();
        const double tFmt = medianUs(15, [&] {
            std::string s;
            s.reserve(96 * 1024);
            char buf[512];
            for (const auto& pk : pim.m_pairKeys) {
                auto it = pim.m_pairInfoMap.find(pk);
                if (it == pim.m_pairInfoMap.end()) continue;
                const pt::PairInfo& p = it->second;
                const int len = std::snprintf(
                    buf, sizeof(buf),
                    "%s,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%ld\n",
                    p.pairInstrumentKey,
                    p.pairTotalVolume, p.pairActiveTotalPrice, p.pairPassiveTotalPrice,
                    p.openSmallSpreadBidBidUQ, p.openSmallSpreadAskAskDQ,
                    p.adlClose.triggered ? 1 : 0, p.adlClose.currentTier,
                    p.spreadNoRegression.triggered ? 1 : 0, p.spreadNoRegression.currentTier,
                    p.fundingAbnormal.triggered ? 1 : 0, p.fundingAbnormal.currentTier,
                    p.positionExceedThresholdStartTime, p.spreadNoRegressionStartTime,
                    p.autoFlag ? 1 : 0, p.stopFlag ? 1 : 0, p.currentAlgoOrderId);
                s.append(buf, static_cast<size_t>(len));
            }
            if (s.size() == 0xdeadbeef) std::printf("x");
        });

        // 对照 2：同样内容，但用 ofstream << setprecision(17) 格式化到内存（不落盘）。
        // 差值 = iostream 数字格式化比 snprintf 贵多少。
        const double tStream = medianUs(15, [&] {
            std::ostringstream oss;
            oss << std::setprecision(17);
            for (const auto& pk : pim.m_pairKeys) {
                auto it = pim.m_pairInfoMap.find(pk);
                if (it == pim.m_pairInfoMap.end()) continue;
                const pt::PairInfo& p = it->second;
                oss << p.pairInstrumentKey << ","
                    << p.pairTotalVolume << ","
                    << p.pairActiveTotalPrice << ","
                    << p.pairPassiveTotalPrice << ","
                    << p.openSmallSpreadBidBidUQ << ","
                    << p.openSmallSpreadAskAskDQ << ","
                    << p.adlClose.triggered << ","
                    << p.adlClose.currentTier << ","
                    << p.spreadNoRegression.triggered << ","
                    << p.spreadNoRegression.currentTier << ","
                    << p.fundingAbnormal.triggered << ","
                    << p.fundingAbnormal.currentTier << ","
                    << p.positionExceedThresholdStartTime << ","
                    << p.spreadNoRegressionStartTime << ","
                    << p.autoFlag << ","
                    << p.stopFlag << ","
                    << p.currentAlgoOrderId << "\n";
            }
            if (oss.str().size() == 0xdeadbeef) std::printf("x");
        });

        std::printf("\n500 行拆解（同一份数据，只改「怎么格式化 / 落不落盘」）：\n");
        std::printf("  snprintf 格式化到内存      : %7.1f us\n", tFmt);
        std::printf("  ofstream<< 格式化到内存    : %7.1f us   (%.1fx snprintf)\n",
                    tStream, tStream / tFmt);
        std::printf("  ofstream<< + flush + rename: %7.1f us   (见上表 500 行)\n", snap500);
        std::printf("  => 格式化约占 %.0f%%，IO(open/flush/rename) 约占 %.0f%%。\n",
                    100.0 * tStream / snap500, 100.0 * (snap500 - tStream) / snap500);
        std::printf("     两个都不是「可以忽略」的量级，所以正确做法是整段搬走，而不是只换格式化函数。\n");
    }

    // 对照 3：把"取数"单独量出来 —— 也就是把落盘搬到写线程后，主线程还要付的那部分。
    // 形状严格照 PairInfoManager::CollectSpreadStats 的写法：
    //   遍历 m_pairKeys -> m_pairInfoMap.find -> 跳过无效 -> 拷 216 B POD 行。
    // 注意：这是**代理实现**（真正的那份还没写），只用来量"取数 vs 格式化"的比例。
    {
        FillWorld(500);
        auto& pim = pt::PairInfoManager::Instance();

        struct Row {                    // 与拟议的 SpreadStatsRow 同尺寸
            char    key[stra::INST_KEY_LEN];
            double  q[8];
            int     count;
            double  avgDepthVolume;
            int64_t calcTs;
        };
        std::printf("\n取数（主线程要付的那部分，500 行，sizeof(Row)=%zu B）:\n", sizeof(Row));

        std::vector<Row> rows(500);
        const double tCollect = medianUs(500, [&] {
            int n = 0;
            for (const auto& pk : pim.m_pairKeys) {
                if (n >= 500) break;
                auto it = pim.m_pairInfoMap.find(pk);
                if (it == pim.m_pairInfoMap.end()) continue;
                const pt::SpreadStats& st = it->second.largeStats;
                if (!st.IsValid()) continue;
                Row& r = rows[static_cast<size_t>(n++)];
                std::memcpy(r.key, it->second.pairInstrumentKey, sizeof(r.key));
                r.q[0] = st.bidAskUQ; r.q[1] = st.bidAskDQ;
                r.q[2] = st.bidBidUQ; r.q[3] = st.bidBidDQ;
                r.q[4] = st.askBidUQ; r.q[5] = st.askBidDQ;
                r.q[6] = st.askAskUQ; r.q[7] = st.askAskDQ;
                r.count = st.count;
                r.avgDepthVolume = st.avgDepthVolume;
                r.calcTs = st.calcTs;
            }
            if (n == 0xdeadbeef) std::printf("x");
        });
        std::printf("  取数到 POD 数组            : %7.1f us   (map.find x500 + 拷贝 %.0f KB)\n",
                    tCollect, 500.0 * sizeof(Row) / 1024.0);
        std::printf("  原来整段（格式化+落盘）    : %7.1f us\n", snap500);
        std::printf("  => 搬到写线程后主线程只剩 %.1f%% 的代价。\n", 100.0 * tCollect / snap500);
    }

    // 对照 4：SaveSnapshot 搬走之后，执行线程仍然要付的那部分 —— 33 列取数到 POD 行。
    // 这里调的是**生产函数本体** `PairInfoManager::CollectSnapshotRows`（不是代理实现），
    // 所以这个数字就是 OnTimer 里 PublishSnapshot 的真实开销。
    {
        FillWorld(500);
        auto& pim = pt::PairInfoManager::Instance();
        std::vector<pt::PairInfoRecord> rows;
        std::printf("\n取数（主线程要付的那部分，500 行，sizeof(PairInfoRecord)=%zu B）:\n", sizeof(pt::PairInfoRecord));

        const double tCollect = medianUs(500, [&] {
            const int n = pim.CollectSnapshotRows(rows);
            if (n == 0xdeadbeef) std::printf("x");
        });
        std::printf("  33 列取数到 POD 数组       : %7.1f us   (map.find x500 + 拷贝 %.0f KB)\n",
                    tCollect, 500.0 * sizeof(pt::PairInfoRecord) / 1024.0);
        std::printf("  原来整段（格式化+落盘）    : %7.1f us\n", snap500);
        std::printf("  => 搬到写线程后主线程只剩 %.1f%% 的代价。\n", 100.0 * tCollect / snap500);
    }

    return 0;
}
