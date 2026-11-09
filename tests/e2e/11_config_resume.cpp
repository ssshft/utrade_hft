// ===========================================================================
// 11 配置恢复 —— pairCommands cmdType=3 (RESUME)
//
// 这是 STOP / CLOSE / 敞口异常 / 孤儿冻结 的**唯一出路**（不改快照就得重启）。
// 所以它必须一次清干净三样东西：
//   stopFlag（挡死开仓+平仓） / closeFlag（只平不开） / reBalance（自动再平衡的补救路径）
// 以及把两个数值字段整条覆盖成命令里的值（缺省 0）。
//
// ⚠️ 为什么必须清 reBalance：不清的话下一次 ProcessRisk 会立刻按旧敞口
//    再建一张再平衡单，把运维的恢复操作顶回去。
//
// ⚠️ 为什么 RESUME 的分支要排在 CLOSE 的 stopFlag 检查**之前**：
//    否则"发完 STOP 就再也解不开"，只能重启。本用例从 stopFlag=true 的状态开始，
//    正好钉住这个顺序。
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);
    const bool autoBefore = pi.autoFlag;

    // ---- 先停下来 ----
    ApplyRuntimeConfig(fx, PairCmdJson(1));
    CHECK(pi.stopFlag, "前提：STOP 之后 stopFlag = true");

    // ---- 摆一个"需要恢复"的现场 ----
    // 这几个字段在真实系统里会由别的路径留下：closeFlag 来自快照或 CLOSE 命令，
    // reBalance 来自 CheckExposureAbnormal，两个数值来自上一次命令。
    pi.closeFlag = true;
    pi.reBalance = true;
    pi.profitPct = -0.002;
    pi.closeRemainVolume = 3.0;

    // ---- 下发 cmdType=3（两个数值字段都不写 = 缺省 0）----
    ApplyRuntimeConfig(fx, PairCmdJson(3));

    CHECK(!pi.stopFlag, "RESUME -> stopFlag 清掉（这条是解 STOP 的唯一出路）");
    CHECK(!pi.closeFlag, "RESUME -> closeFlag 也清掉（不是只解 stopFlag）");
    CHECK(!pi.reBalance, "RESUME -> reBalance 清掉（否则下一轮 ProcessRisk 会再建再平衡单顶回来）");
    CHECK(Near(pi.profitPct, 0.0), "profitPct 整条覆盖成缺省 0（上一次的 -0.002 必须被冲掉）");
    CHECK(Near(pi.closeRemainVolume, 0.0), "closeRemainVolume 覆盖成 0 = 平干净");
    CHECK(pi.autoFlag == autoBefore, "不碰 autoFlag（它是'参数由谁算'的标签，不是急停状态）");

    // OnTimer 会按 cfg 重算报单量（maxVolume 10 -> 5），量摆回来
    MakeHoldable(pi, -5.0);

    // ---- 行为：恢复之后开仓要能重新发出去 ----
    ArmOpen(pi, "ttOL", -0.5);
    CHECK(SignalGenerator::Instance().CheckSignalForSatisfy(pi).ttOLSignal, "前提：开仓信号成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "RESUME 之后开仓单重新发得出去");
    if (fx.created.size() == 1) {
        CHECK(fx.created[0]->role == AlgoRole::OPEN_SIGNAL, "role = OPEN_SIGNAL");
    }
}

int main() { return RunCase("11 配置恢复", Case); }
