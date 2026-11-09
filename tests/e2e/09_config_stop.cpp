// ===========================================================================
// 09 配置停止交易 —— pairCommands cmdType=1 (STOP) -> stopFlag
//
// "停止交易"的定义：**开仓不发，平仓也不发**。这两条分别落在
//   · SignalGenerator::CanOpen  -> reason "stopFlag = true"
//   · SignalGenerator::CanClose -> reason "stopFlag = true"
// 少判任何一个，症状都是"停不干净"：只停开仓等于把仓位关在里面没人管。
//
// 断言分两层，缺一不可：
//   ① 闸门：CanOpen / CanClose 都 false（读字段）
//   ② 行为：信号成立也确实不建单（走 ProcessPairSignal）
// 只做 ② 会假阴性 —— 信号本身没成立的话，不建单和"被拦住"看起来一样。
// 所以 ② 之前先 CHECK 信号确实成立。
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);          // 有持仓，CanClose 才不会先被 "no position" 挡掉
    const bool autoBefore = pi.autoFlag;

    // ---- 下发 cmdType=1 ----
    ApplyRuntimeConfig(fx, PairCmdJson(1));

    CHECK(pi.stopFlag, "STOP -> stopFlag = true");
    CHECK(!pi.closeFlag, "STOP 不碰 closeFlag（两个开关语义独立，别顺手一起置）");
    CHECK(pi.autoFlag == autoBefore,
          "不碰 autoFlag（写 false 会让该对子的单免掉超时撤单，那不是急停该有的效果）");

    // ---- ① 闸门 ----
    auto& sg = SignalGenerator::Instance();
    std::string reason;
    CHECK(!sg.CanOpen(pi, reason), "CanOpen = false");
    CHECK(reason == "stopFlag = true", "开仓被拦的原因是 stopFlag（不是 closeFlag / closeOnly）");
    CHECK(!sg.CanClose(pi, reason), "CanClose = false —— 停止交易 = 平仓也停");
    CHECK(reason == "stopFlag = true", "平仓被拦的原因同样是 stopFlag");

    // OnTimer 会按 cfg 重算报单量（maxVolume 10 -> 5），量摆回来再测行为
    MakeHoldable(pi, -5.0);

    // ---- ② 行为：开仓信号成立，不建单 ----
    // 量要同时满足两件事：
    //   · 有持仓（|vol| > 1e-9）—— 否则 CheckSignalForSatisfy 在 stopFlag 下直接返回空信号，
    //     断言会变成"信号本来就没成立"的假阴性；
    //   · vol > ttOLEndVolume（ArmOpen 把它写成 -1.0）—— 否则开仓判据不成立。
    // 取 -0.5 两条都过。
    ArmOpen(pi, "ttOL", -0.5);
    CHECK(sg.CheckSignalForSatisfy(pi).ttOLSignal, "前提：开仓信号确实成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.empty(), "开仓信号成立，但 stopFlag 下不建单");

    // ---- ② 行为：平仓信号成立，也不建单 ----
    ArmClose(pi, "ttCL", -5.0);
    CHECK(sg.CheckSignalForSatisfy(pi).ttCLSignal, "前提：平仓信号确实成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.empty(), "平仓信号成立，stopFlag 下同样不建单");
}

int main() { return RunCase("09 配置停止交易", Case); }
