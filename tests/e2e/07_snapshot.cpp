// ===========================================================================
// 07 pairinfo 落库 —— 快照存盘 / 重启读回
//
// 快照只存"无法自己恢复"的字段（运维意图 + 账本 + 风控档位），
// 账户真值（持仓 / 均价 / 浮盈 / 强平价 / ADL）一律不存 —— 由持仓推送自愈。
// 所以本用例有两组断言，方向是相反的：
//   ✅ 存过的字段必须逐字回来
//   ❌ 没存的字段必须还是 0（如果它回来了，说明有人偷偷加进了快照）
//
// 落盘格式是 32 列 CSV，写 .tmp 再 rename 原子替换。列数对不上时 LoadSnapshot
// 会**整份拒绝**并只打一条 LOG_WARN —— 所以列数也要断言。
//
// ⚠️ 已确认的两处"看起来该存但没存"：
//    · profitSwitch —— 列里只有 profitPct，重启后回到默认 true（盈利保护默认开）
//    · 账户真值（持仓/均价）—— 故意不存，靠持仓推送自愈
// ===========================================================================
#include "harness.h"

static int CountLines(const std::string& s) {
    int n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

static size_t CountFields(const std::string& line) {
    size_t n = 1;
    for (char c : line) if (c == ',') ++n;
    return n;
}

static void Case() {
    const std::string snap = TempPath("pair_info.csv");

    Fx a;
    Boot(a);
    GoTrading(a);

    PairInfo& pi = *a.pi();
    // ---- 存这些 ----
    pi.pairTotalVolume      = -3.2;
    pi.pairActiveTotalPrice = 101.5;
    pi.autoFlag   = false;
    pi.stopFlag   = false;
    pi.closeFlag  = true;          // 只平不开：必须原样回来
    pi.profitPct  = 0.002;
    pi.profitSwitch = false;
    pi.hasActiveAlgoOrder = false; // false -> 重启不触发孤儿冻结（12 号用例测 true）
    // ---- 不存这些（账户真值，由推送自愈）----
    pi.activeRealPosition = 9.9;
    pi.activeAvgPrice     = 88.8;

    CHECK(PairInfoManager::Instance().SaveSnapshot(snap), "SaveSnapshot 返回 true");

    const std::string text = ReadTextFile(snap);
    CHECK(!text.empty(), "快照文件已生成");
    CHECK(CountLines(text) == 2, "文件 = 表头 + 1 行数据");
    {
        const std::string header = text.substr(0, text.find('\n'));
        CHECK(CountFields(header) == 32, "表头 32 列（列数不对 LoadSnapshot 会整份拒绝）");
        CHECK(header.find("pairInstrumentKey") != std::string::npos, "表头第一列是 pairInstrumentKey");
    }

    // ---- 模拟进程重启：清内存，不删文件，重新 Init ----
    Fx b;
    Boot(b, /*keepState=*/true);

    PairInfo* p = b.pi();
    CHECK(p != nullptr, "重启后对子仍在（按配置重建）");
    if (!p) return;

    CHECK(Near(p->pairTotalVolume, -3.2), "账本量恢复 = -3.2");
    CHECK(Near(p->pairActiveTotalPrice, 101.5), "建仓均价恢复 = 101.5");
    CHECK(!p->autoFlag, "autoFlag 恢复 = false（手动模式）");
    CHECK(p->closeFlag, "closeFlag 恢复 = true（只平不开）");
    CHECK(Near(p->profitPct, 0.002), "profitPct 恢复 = 0.002");
    CHECK(!p->stopFlag, "stopFlag 恢复 = false（快照里就是 false，不是被谁改的）");

    // ⚠️ profitSwitch **不在** 32 列里（列清单见 PairInfoManager.cpp:96-109，
    //    只有 profitPct）。所以写盘时的 false 丢了，重启后回到 PairInfo.h:308
    //    的默认值 true —— 也就是说"盈利保护"默认是开的。
    //    如果哪天决定把它也存进快照，这条断言必须跟着改。
    CHECK(p->profitSwitch, "profitSwitch 不持久化 -> 回到默认 true（盈利保护默认开）");

    // ---- 账户真值不恢复（保持结构体默认值，等持仓推送来填）----
    CHECK(Near(p->activeRealPosition, 0.0), "activeRealPosition 不恢复（由持仓推送自愈）");
    CHECK(Near(p->activeAvgPrice, -1.0), "activeAvgPrice 不恢复（默认 -1.0，见 PairInfo.h:242）");

    // ---- 重启后仍是"待对账"，不会拿快照账本直接交易 ----
    CHECK(b.ptCtx.m_phase != PairTradingContext::StartupPhase::Trading,
          "重启后处于对账阶段，不是直接 Trading");
}

int main() { return RunCase("07 pairinfo 落库/重启", Case); }
