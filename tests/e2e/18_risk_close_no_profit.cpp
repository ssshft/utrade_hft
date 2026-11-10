// ===========================================================================
// 18 强平不要求盈利 —— 同一个亏损局面，普通平仓单报不出子单，强平单报得出
//
// 这条用例钉的是 `AlgoPairOrder.cpp:353/472` 那道守卫的**后果**，不是标志位：
//     if (profitSwitch) { profitRate = tempSpread - lockedSpread;
//                         if (profitRate < profitPct) targetActiveVolume = startVolume; }
//
// 为什么单独一条：05 只证明了"强平单建出来时 profitSwitch=false"，
// 但那是**因**，不是**果**。真正要证明的是"亏损状态下强平单还能把单报出去" ——
// 否则改了个标志位、拆单那一层仍然夹回满仓，等于白改。
//
// 编排：同一个 PairInfo、同一帧行情，只换 role，两次拆单对照。
//   · 建仓基准 lockedSpread = pairPassive/pairActive − 1 = 102/100 − 1 = +200bp
//   · 现在能拿到的平仓价差只有 +10bp（spreadAskBid）
//   ⇒ 净往返 = 10 − 200 = −190bp，远低于 profitPct(1bp)
//
// ⚠️ 对照组必须**自证**：先在 profitSwitch=true 下拿到空单，再把同一个对象的
//    profitSwitch 掰成 false、同一帧行情再取一次，必须拿得到。否则"空单"可能只是
//    阶梯没触发（比如 ttCLStartVolume 写成默认 0 让守卫夹不到满仓），整条用例就是假绿。
// ⚠️ ttCLStartVolume 必须是 -maxVolume（生产由 RecalcOrderParams 写成 -mv）。
// ⚠️ 用 GetTargetPairOrder 而不是 CreatePairOrder：前者成功时把 pairId 原样写回
//    传入的 id、失败时保持 -1，一次调用就能分清"报得出"和"报不出"（同 algo_exec D9）。
// ⚠️ 行情帧要 spreadEffective=false：只灌 SpreadManager 的槽位，不让 OnSpread 顺手报单。
// ⚠️ 必须走 SubmitAlgoOrder 而不是直接 BuildAlgoOrderJson：后者 new 出来的对象没注册，
//    smc 为空 ⇒ passInfoParameterCheck 失败 ⇒ 一条子单都取不到（会伪装成"保护生效"）。
// ===========================================================================
#include "harness.h"

// 摆一个"持仓是亏的"局面：对子多 5 手 ⇒ 平仓方向 CLOSE_LONG（主动腿买回）。
static void ArmLosingClose(PairInfo& pi) {
    MakeHoldable(pi, -5.0);
    pi.pairActiveTotalPrice  = 100.0;
    pi.pairPassiveTotalPrice = 102.0;      // lockedSpread = +200bp

    auto& op = pi.orderParams;
    op.ttCLSwitch      = true;
    op.ttCLStartSpread = 0.0;              // 平仓线：价差 ≥ 0 就该平
    op.ttCLEndSpread   = 0.000005;
    op.ttCLStartVolume = -10.0;            // = -maxVolume，见文件头 ⚠️
    op.ttCLEndVolume   = 0.0;
}

// 只灌行情、不报单
static void FeedLosingQuote(Fx& fx) {
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MakeData(100.00, 100.01, 100.00, 100.01,
                                  -1.0, -1.0, 0.0010, -0.001, crypto::getCurrentTime());
    pdata.spreadEffective = false;
    fx.algoCtx.OnSpread(&topic, &pdata);
}

static void Case() {
    // ---- 对照组：普通平仓单（profitSwitch 跟随 pair_info = true）----
    {
        Fx fx;
        Boot(fx);
        GoTrading(fx);
        SeedFunds(fx);

        PairInfo& pi = *fx.pi();
        ArmLosingClose(pi);
        fx.ptCtx.SubmitAlgoOrder(pi, "TT", "CL", 0.0, AlgoRole::CLOSE_SIGNAL);

        BaseAlgoOrder* o = fx.Order0();
        CHECK(o != nullptr, "对照组：普通平仓单建得出来");
        if (!o) return;
        CHECK(o->profitSwitch, "对照组：profitSwitch = true（跟随 pair_info，保护开着）");

        FeedLosingQuote(fx);

        PairOrder blocked = o->GetTargetPairOrder(stra::TAKER_TAKER, stra::CLOSE_LONG, 12345);
        CHECK(blocked.pairId == -1,
              "对照组：亏着 ⇒ 守卫把目标量夹回满仓，一条子单都不报（pairId 保持 -1）");

        // 自证：同一个对象、同一帧行情，只把保护关掉就必须报得出来
        o->profitSwitch = false;
        PairOrder forced = o->GetTargetPairOrder(stra::TAKER_TAKER, stra::CLOSE_LONG, 12345);
        CHECK(forced.pairId == 12345,
              "对照组自证：关掉保护后同一局面报得出子单 ⇒ 阶梯本来就该触发，不是假绿");
        CHECK(forced.tradingTypeOffset == stra::CLOSE_LONG,
              "对照组自证：方向仍是 CLOSE_LONG");
    }

    // ---- 实验组：风控强平单（role = RISK_CLOSE，建单即关掉保护）----
    {
        Fx fx;
        Boot(fx);
        GoTrading(fx);
        SeedFunds(fx);

        PairInfo& pi = *fx.pi();
        ArmLosingClose(pi);
        fx.ptCtx.SubmitAlgoOrder(pi, "TT", "CL", 0.0, AlgoRole::RISK_CLOSE);

        BaseAlgoOrder* o = fx.Order0();
        CHECK(o != nullptr, "实验组：强平单建得出来");
        if (!o) return;
        CHECK(o->role == AlgoRole::RISK_CLOSE, "实验组：role = RISK_CLOSE");
        CHECK(!o->profitSwitch && Near(o->profitPct, 0.0),
              "实验组：建单即关掉盈利保护（不用手掰）");

        FeedLosingQuote(fx);

        PairOrder po = o->GetTargetPairOrder(stra::TAKER_TAKER, stra::CLOSE_LONG, 12345);
        CHECK(po.pairId == 12345,
              "实验组：同一个亏损局面，强平单照样报得出子单（亏着也能平）");
        CHECK(po.tradingTypeOffset == stra::CLOSE_LONG,
              "实验组：拆出来的还是 CLOSE_LONG，没因为关保护而走错方向");
    }
}

int main() { return RunCase("18 强平不要求盈利", Case); }
