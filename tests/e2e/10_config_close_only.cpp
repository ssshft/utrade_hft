// ===========================================================================
// 10 配置只平不开 —— pairCommands cmdType=2 (CLOSE) -> closeFlag
//
// 与 09 的区别：09 是"连平仓都停"，本用例是"只停开仓，持仓照常平"。
// 这是运维最常用的一个动作（先把新仓关掉，让手上的仓位自己走到终点）。
//
// 一条命令同时写三个字段（整条覆盖，不是补丁）：
//   closeFlag = true / profitPct / closeRemainVolume
// 后两个只在 CLOSE 命令里生效，其中 closeRemainVolume 决定平仓阶梯的末端
// （0 = 平干净，3 = 留 3 手不平）—— 它改的是 orderParams 的 4 个 *EndVolume。
//
// ---------------------------------------------------------------------------
// ✅ 本用例曾钉出一个真缺陷，已于 2026-11-09 修复（一个字符）：
//
//    PairTradingContext.cpp:1589   `else if (cmd.cmdType != PairCmd_CLOSE)`
//    方向写反了（应为 `==`）。后果有两个方向，都很严重：
//      ① cmdType=2（CLOSE）进不了这个分支，落到末尾的
//         `else { LOG_WARN("pairCommands not support cmdtype: 2") }`
//         ⇒ **"只平不开"这个功能整体不可用**，运维发完命令以为生效了，其实什么都没变。
//      ② cmdType=4（MODIFY，文档写明"未实现，落地端报错跳过"）反而会进这个分支
//         ⇒ 误置 closeFlag=true，顺手把 profitPct / closeRemainVolume 也改成 0。
//
//    修复前 13 条失败，修复后 19/19 全绿（断言一条没改）。
//    留着这段是因为它解释了下面这些断言为什么这么写：全部按**文档语义**
//    （RuntimeConfig.h:52-61）写，不迁就实现。
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);

    // ---- 下发 cmdType=2，顺带让利平仓 -20bp、留 3 手 ----
    ApplyRuntimeConfig(fx, PairCmdJson(2, ",\"profitPct\":-0.002,\"closeRemainVolume\":3"));

    CHECK(pi.closeFlag, "CLOSE -> closeFlag = true（失败见文件头：:1589 条件写反）");
    CHECK(!pi.stopFlag, "CLOSE 不置 stopFlag（那是 STOP 的事）");
    CHECK(Near(pi.profitPct, -0.002), "profitPct 整条覆盖成 -0.002（负数 = 让利平仓）");
    CHECK(Near(pi.closeRemainVolume, 3.0), "closeRemainVolume = 3（0 = 平干净）");

    // ---- 开关：4 个开仓压掉，4 个平仓不动 ----
    const auto& op = pi.orderParams;
    CHECK(!op.ttOLSwitch && !op.ttOSSwitch && !op.mtOLSwitch && !op.mtOSSwitch,
          "4 个开仓开关被压成 false（ApplyNoOpenGate）");
    CHECK(op.ttCLSwitch && op.ttCSSwitch && op.mtCLSwitch && op.mtCSSwitch,
          "4 个平仓开关不动（只平不开 = 平仓那条路要留着）");

    auto& sg = SignalGenerator::Instance();
    std::string reason;
    CHECK(!sg.CanOpen(pi, reason), "CanOpen = false");
    CHECK(reason == "closeFlag = true", "原因说得出是 closeFlag，不是 closeOnly / stopFlag");
    CHECK(sg.CanClose(pi, reason), "CanClose = true（有持仓就能平）");

    // OnTimer 会按 cfg 重算报单量（maxVolume 10 -> 5），量摆回来；顺便让阶梯末端按 closeRemainVolume 重算
    MakeHoldable(pi, -5.0);
    sg.RecalcOrderParams(pi);
    CHECK(Near(pi.orderParams.ttCLEndVolume, -3.0), "平多阶梯末端 = -3（留 3 手不平）");
    CHECK(Near(pi.orderParams.ttCSEndVolume,  3.0), "平空阶梯末端 = +3（同一个 r，符号对称）");

    // ---- 行为①：开仓信号成立，被拦住 ----
    ArmOpen(pi, "ttOL", -0.5);
    CHECK(sg.CheckSignalForSatisfy(pi).ttOLSignal, "前提：开仓信号确实成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.empty(), "开仓信号成立，closeFlag 下不建单");

    // ---- 行为②：平仓照常放行 ----
    OnlySwitch(pi, "ttCL");
    pi.pairTotalVolume = -5.0;
    pi.rtSpread.valid = true;
    pi.rtSpread.spreadAskBid = 1.0;
    pi.orderParams.ttCLStartSpread = 0.0;
    CHECK(sg.CheckSignalForSatisfy(pi).ttCLSignal, "前提：平仓信号成立（vol -5 < 末端 -3）");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "平仓照常建单");
    if (fx.created.size() == 1) {
        CHECK(fx.created[0]->role == AlgoRole::CLOSE_SIGNAL, "role = CLOSE_SIGNAL");
        CHECK(Near(fx.created[0]->ttCLEndVolume, -3.0), "末端 -3 一路传到算法单（不是 0）");
    }

    // ---- 缺陷的另一个方向：cmdType=4 (MODIFY) 必须什么都不做 ----
    // 文档（RuntimeConfig.h:44）写明"4=改参数（未实现，落地端报错跳过）"。
    // 同一个 `!=` 让它进了"只平不开"分支 —— 顺手把 closeFlag 置了。
    PairInfo& pi2 = *fx.pi();
    pi2.closeFlag = false;
    pi2.profitPct = 0.0001;
    ApplyRuntimeConfig(fx, PairCmdJson(4));
    CHECK(!pi2.closeFlag, "cmdType=4 不该置 closeFlag（同一处 :1589 的另一个症状）");
    CHECK(Near(pi2.profitPct, 0.0001), "cmdType=4 不该改 profitPct");
}

int main() { return RunCase("10 配置只平不开", Case); }
