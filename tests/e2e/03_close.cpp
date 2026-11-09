// ===========================================================================
// 03 正常平仓 —— 有持仓 + 平仓信号 -> CLOSE_SIGNAL 单 -> 平到 0 -> 终结并释放对子
//
// 这一条的重点是"什么时候算结束"：
//   平仓单的 role 是 CLOSE_SIGNAL，AlgoContext::OnTimer 的判据是"平到 0"，
//   所以它在还有持仓时**不会**提前结束，平到 0 时**立刻**结束（不用等 150s 超时）。
//
// ⚠️ pairTotalVolume 符号反着：负 = 对子多。所以"平多"喂 -5.0。
// ⚠️ 普通平仓单建单时**不清**四个开仓开关（它允许在开仓带回补），
//    只有风控强平才清 —— 这是 05 与本用例的分界。
// ⚠️ 子单的 offsetFlag 恒为 OF_OPEN（PairManager.cpp:127 写死的），
//    平仓意图只体现在 pairOrder.tradingTypeOffset 和方向（reduceOnly）上。
//    这里断言 pairOrder 的 CLOSE_LONG，不断言子单的 offsetFlag。
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);            // 对子多 5 手
    ArmClose(pi, "ttCL", -5.0);        // 平多：只开 ttCL，价差越过平仓线
    pi.orderParams.ttOLSwitch = true;  // 开仓开关留开着：平仓单允许回补

    fx.ptCtx.ProcessPairSignal(pi);
    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "平仓信号 -> 建出 1 张算法单");
    if (!o) return;

    CHECK(o->role == AlgoRole::CLOSE_SIGNAL, "role = CLOSE_SIGNAL（平到 0 算结束）");
    CHECK(Near(o->pairTotalVolume, -5.0), "算法单带着 -5 的账本仓位建出来");
    CHECK(o->ttOLSwitch, "普通平仓单保留开仓开关（允许在开仓带回补）");
    CHECK(pi.hasActiveAlgoOrder, "对子被这张平仓单占住");

    // ---- 还有 5 手没平完：不能提前终结 ----
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW,
          "还有持仓 -> OnTimer 不终结（role 不等于'建出来就结束'）");

    // ---- 拆单：方向必须与"平多"一致 ----
    // 行情要与平仓方向一致：ttCL 的判据是 `spreadAskBid - F > ttCLStartSpread`，
    // 所以 spreadAskBid 给正值。喂 FreshData()（四个口径全 -1）一条子单都报不出来。
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MakeData(100.00, 100.01, 100.00, 100.01,
                                  -1.0, -1.0, 1.0, -0.001, crypto::getCurrentTime());
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "平仓单按同一套拆单逻辑报出子单");
    auto pos = fx.PairOrders(o);
    CHECK(!pos.empty() && pos[0].tradingTypeOffset == stra::CLOSE_LONG,
          "pairOrder.tradingTypeOffset = CLOSE_LONG（平仓意图在这一层表达）");
    if (fx.NNew() == 1) {
        const pubsub::NewOrder* n = fx.News()[0];
        CHECK(Near(n->volumeTotal, 5.0), "平仓子单量 = 5（把持仓一次平完）");
        // OPEN_LONG 的主动腿是 DT_SHORT，所以 CLOSE_LONG 的主动腿是 DT_LONG（买回）
        CHECK(n->direction == DT_LONG, "CLOSE_LONG -> 主动腿 DT_LONG（买回那条空腿）");
    }

    // ---- 撤掉这张没成交的子单，让 pairOrder 收口 ----
    // 有在途 pairOrder 时 OnTimer 不会判结束（不能一边挂着单一边说"平完了"）
    if (fx.NNew() == 1 && !pos.empty()) {
        fx.algoCtx.OnOrder(MakeRtn(fx.News()[0]->clientOrderId,
                                   MakeRef(o, pos[0].pairId).c_str(),
                                   OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
        CHECK(fx.PairOrders(o).empty(), "撤单回报 -> pairOrder 收口");
    }

    // ---- 平到 0 ----
    // 生产里这个值由成交回报驱动；这里直接写算法单上的账本，效果等价。
    o->pairTotalVolume = 0.0;
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_FILLED, "平到 0 -> 立刻 FILLED（不用等 150s 超时）");
    CHECK(o->commandType == stra::CommandType_FINISHED, "commandType = FINISHED");
    CHECK(!pi.hasActiveAlgoOrder, "终结回传 -> 对子被释放，下一轮可以重新建单");
}

int main() { return RunCase("03 正常平仓", Case); }
