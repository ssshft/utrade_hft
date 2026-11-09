// ===========================================================================
// 13 全局开关 —— closeOnly / stopAllAlgo（不是按对子的，是整机的）
//
// 两个开关语义**不同**，别当成一个：
//   closeOnly   = 只平不开：压掉全部开仓开关，平仓照常，风控强平照常。
//   stopAllAlgo = 停掉所有算法单：ProcessPairSignal / ProcessRisk 一进来就 return，
//                 连平仓单和风控强平单都不发，并且每 tick 对在跑的单发一次 CANCEL。
//
// stopAllAlgo 是"人在旁边看着"的操作，closeOnly 是"可以无人值守过夜"的操作。
// ===========================================================================
#include "harness.h"

// 小持仓 -> CheckRisk 命中碎单 -> needForceClose（同 05）
static void ArmTinyCloseRisk(PairInfo& pi) {
    pi.pairTotalVolume = 0.01;
    pi.activeParam.calcType = 0;
    pi.activeParam.multiple = 1.0;
    pi.rtSpread.activePriceTema = 100.0;
    pi.rtSpread.valid = true;
}

static void CaseCloseOnly() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);

    ApplyRuntimeConfig(fx, GlobalSwitchJson("closeOnly", true));

    auto& sg = SignalGenerator::Instance();
    CHECK(sg.m_closeOnly, "closeOnly 落地到 SignalGenerator");
    CHECK(fx.ptCtx.m_cfg.closeOnly, "同时写进 m_cfg（重启前的那份副本）");
    CHECK(!pi.closeFlag, "closeOnly 是全局的，不写 per-pair 的 closeFlag（两者会互相覆盖）");

    const auto& op = pi.orderParams;
    CHECK(!op.ttOLSwitch && !op.ttOSSwitch && !op.mtOLSwitch && !op.mtOSSwitch,
          "4 个开仓开关被压掉");

    std::string reason;
    CHECK(!sg.CanOpen(pi, reason), "CanOpen = false");
    CHECK(reason == "closeOnly = true (配置)", "原因报的是全局 closeOnly（优先级高于 stopFlag）");
    CHECK(sg.CanClose(pi, reason), "CanClose = true —— 只平不开，平仓那条路留着");

    // OnTimer 会按 cfg 重算报单量（maxVolume 10 -> 5），量摆回来
    MakeHoldable(pi, -5.0);

    // ---- 开仓被拦 ----
    ArmOpen(pi, "ttOL", -0.5);
    CHECK(sg.CheckSignalForSatisfy(pi).ttOLSignal, "前提：开仓信号成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.empty(), "开仓不建单");

    // ---- 平仓照常 ----
    ArmClose(pi, "ttCL", -5.0);
    CHECK(sg.CheckSignalForSatisfy(pi).ttCLSignal, "前提：平仓信号成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "平仓照常建单（closeOnly 不挡平仓）");
}

static void CaseStopAllAlgo() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);

    // 先在场上放一张在跑的算法单 —— 0.7 那一段要撤的就是它
    ArmOpen(pi, "ttOL", -0.5);
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1 && pi.hasActiveAlgoOrder, "前提：场上有 1 张在跑的算法单");

    fx.modifyCalls.clear();
    ApplyRuntimeConfig(fx, R"({"op":{"pairTrading":{"closeOnly":false,"stopAllAlgo":true}}})");

    CHECK(fx.ptCtx.m_cfg.stopAllAlgo, "stopAllAlgo 落地");
    CHECK(!fx.modifyCalls.empty() && fx.modifyCalls[0].second == stra::CommandType_CANCEL,
          "OnTimer 0.7 对在跑的算法单发了一次 CANCEL（每 tick 发，不是只翻转时发一次）");

    // 把"对子被占住"这个干扰项去掉，否则下面的断言会因为 hasActiveAlgoOrder
    // 提前 return 而通过 —— 那是假阴性，测不到 stopAllAlgo 本身。
    pi.hasActiveAlgoOrder = false;

    // ---- 平仓信号成立也不发（这是与 closeOnly 的分界）----
    ArmClose(pi, "ttCL", -5.0);
    CHECK(SignalGenerator::Instance().CheckSignalForSatisfy(pi).ttCLSignal, "前提：平仓信号成立");
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "stopAllAlgo 下平仓信号也不建单（只增了原来那张）");

    // ---- 风控强平也不发 ----
    ArmTinyCloseRisk(pi);
    fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
    CHECK(fx.created.size() == 1, "风控强平也不发（强平单也是算法单）");
}

static void Case() {
    CaseCloseOnly();
    CaseStopAllAlgo();
}

int main() { return RunCase("13 全局开关", Case); }
