// ===========================================================================
// 07 pairinfo 落库 —— 快照存盘 / 重启读回
//
// 快照只存"无法自己恢复"的字段（运维意图 + 账本 + 风控档位），
// 账户真值（持仓 / 均价 / 浮盈 / 强平价 / ADL）一律不存 —— 由持仓推送自愈。
// 所以本用例有两组断言，方向是相反的：
//   ✅ 存过的字段必须逐字回来
//   ❌ 没存的字段必须还是 0（如果它回来了，说明有人偷偷加进了快照）
//
// 落盘格式是 33 列 CSV，写 .tmp 再 rename 原子替换。表头文本对不上时 LoadSnapshot
// 会**整份拒绝**并只打一条 LOG_ERROR —— 所以列数、表头都要断言。
// ⚠️ 校验比的是**表头文本**而不是列数：33 列这个数字 2026-10-29 之前也用过
//    （当时 profitPct 后面是 errorFlag），只比个数会让旧文件被当成新格式读进来、
//    整行错位还不报错。用例末尾专门守这条。
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
    // 被动腿账本：**符号取被动腿自己的方向**，所以正常对冲的对子里它与
    // pairTotalVolume 反号（这里是 +3.2 对 -3.2）。
    pi.pairPassiveTotalVolume = 3.2;
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
        CHECK(CountFields(header) == 33, "表头 33 列（列数不对 LoadSnapshot 会整份拒绝）");
        CHECK(header.find("pairInstrumentKey") != std::string::npos, "表头第一列是 pairInstrumentKey");
        const std::string lastCol = "pairPassiveTotalVolume";
        CHECK(header.size() >= lastCol.size() &&
                  header.compare(header.size() - lastCol.size(), lastCol.size(), lastCol) == 0,
              "被动腿账本是最后一列（追加在末尾，不动 profitPct/closeFlag/stopFlag 的列号）");
    }

    // ---- 模拟进程重启：清内存，不删文件，重新 Init ----
    Fx b;
    Boot(b, /*keepState=*/true);

    PairInfo* p = b.pi();
    CHECK(p != nullptr, "重启后对子仍在（按配置重建）");
    if (!p) return;

    CHECK(Near(p->pairTotalVolume, -3.2), "账本量恢复 = -3.2");
    CHECK(Near(p->pairPassiveTotalVolume, 3.2), "被动腿账本恢复 = +3.2（与主动腿反号）");
    CHECK(Near(p->pairActiveTotalPrice, 101.5), "建仓均价恢复 = 101.5");
    CHECK(!p->autoFlag, "autoFlag 恢复 = false（手动模式）");
    CHECK(p->closeFlag, "closeFlag 恢复 = true（只平不开）");
    // ⚠️ 快照里存的 profitPct（0.002）**读回来立刻被全局配置覆盖** ——
    //    PairTradingContext::Init 的"全局 profitPct"循环把 cfg.profitPct 写到所有对子，
    //    harness 用的是 PairTradingConfig 的默认值 0.0001。
    //    ⇒ 这一列在生产上是**死数据**（改了也不生效），本断言钉的是"覆盖还在"：
    //      哪天决定让快照赢，它会红，逼人重新决定，而不是悄悄改语义。
    CHECK(Near(p->profitPct, 0.0001),
          "profitPct 被全局配置覆盖（快照里的 0.002 读回来就被冲成 0.0001）");
    CHECK(!p->stopFlag, "stopFlag 恢复 = false（快照里就是 false，不是被谁改的）");

    // ⚠️ profitSwitch **不在** 33 列里（列清单见 PairInfoManager.cpp 的 kSnapshotHeader，
    //    只有 profitPct）。所以写盘时的 false 丢了，重启后回到 PairInfo.h 的
    //    默认值 true —— 也就是说"盈利保护"默认是开的。
    //    如果哪天决定把它也存进快照，这条断言必须跟着改。
    CHECK(p->profitSwitch, "profitSwitch 不持久化 -> 回到默认 true（盈利保护默认开）");

    // ---- 账户真值不恢复（保持结构体默认值，等持仓推送来填）----
    CHECK(Near(p->activeRealPosition, 0.0), "activeRealPosition 不恢复（由持仓推送自愈）");
    CHECK(Near(p->activeAvgPrice, -1.0), "activeAvgPrice 不恢复（默认 -1.0，见 PairInfo.h:242）");

    // ---- 重启后仍是"待对账"，不会拿快照账本直接交易 ----
    CHECK(b.ptCtx.m_phase != PairTradingContext::StartupPhase::Trading,
          "重启后处于对账阶段，不是直接 Trading");

    // ---- 旧表头必须被整份拒绝 ----
    // 2026-10-29 之前的格式**也是 33 列**，只是 profitPct(29) 后面是 errorFlag。
    // 只比列数的话这份文件会被当成新格式读进来，而且不报错：
    //   f[30] 旧 errorFlag(0/1)      -> hasActiveAlgoOrder
    //   f[31] 旧 hasActiveAlgoOrder  -> currentAlgoOrderId
    //   f[32] 旧 currentAlgoOrderId  -> pairPassiveTotalVolume（1.7e18 量级）
    // 所以校验比的是表头文本。这条断言就是守"别再退回比列数"。
    {
        const std::string cur = text.substr(0, text.find('\n'));
        const std::string tailNew = ",hasActiveAlgoOrder,currentAlgoOrderId,pairPassiveTotalVolume";
        const std::string tailOld = ",errorFlag,hasActiveAlgoOrder,currentAlgoOrderId";
        std::string oldHeader = cur;
        const size_t at = oldHeader.rfind(tailNew);
        CHECK(at != std::string::npos, "当前表头尾部形状符合预期（否则下面的构造会失真）");
        if (at != std::string::npos) {
            oldHeader.replace(at, tailNew.size(), tailOld);
            CHECK(CountFields(oldHeader) == 33, "构造出来的旧表头也是 33 列 —— 正是危险之处");

            const std::string oldPath = TempPath("pair_info_old33.csv");
            CHECK(WriteTextFile(oldPath, oldHeader + "\n"), "写入旧格式快照");
            CHECK(PairInfoManager::Instance().LoadSnapshot(oldPath) < 0,
                  "33 列旧表头（errorFlag 版）被整份拒绝，不会整行错位读进来");
        }
    }
}

int main() { return RunCase("07 pairinfo 落库/重启", Case); }
