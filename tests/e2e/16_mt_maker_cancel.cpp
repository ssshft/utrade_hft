// ===========================================================================
// 16 MAKER_TAKER 撤单与多单上限 —— 两处 MT 独有的分支
//
// A. maker 主动腿"价格跑掉就撤"
//    TT 的主动腿是 OT_LIMIT，撤单走 BaseAlgoOrder::CancelOrderOnSpread 的 taker 分支；
//    MT 是 OT_POST_ONLY，走 maker 分支（BaseAlgoOrder.cpp:465），门槛完全不同：
//      · 时间撤单：超过 activeMakerCancelOrderTime **且** 价格已偏离
//      · 被动腿价格变化撤单
//      · 主动腿价格变化撤单：`activeAsk / pairOrder.activeAskPrice1 - 1 < -阈值`
//      · 大单成交延迟撤单
//    这里钉第三条：盘口没动时**不许撤**（否则 maker 挂单一挂就被撤，永远赚不到手续费返还），
//    卖价被市场甩开 21bp（阈值 10bp）时才撤。
//
// B. mtRebalanceFlag = false 时的防重复报单
//    rebalance 模式靠"同一类型只允许一个 pairOrder"限流（14 已测）；
//    关掉之后改成"同类型可以挂多张，但有在途子单时不重复报"，判据是
//    AlgoContext.cpp:520-535 的 createFlag 循环 —— 之前 0 次覆盖。
//    ⚠️ 这两支的差别只在 `mtRebalanceFlag`，所以 B 里必须把它显式关掉，
//       并且把 maxMTOrderSize 放大到 3，否则 `1 < 1` 也会拦住第二单，
//       测到的就不是 createFlag 而是上限，属于假阳性。
// ===========================================================================
#include "harness.h"

// ---- A. 主动腿价格跑掉 -> 撤单 ----
static void MakerCancelOnPriceMove() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmMtOpen(pi, "mtOL", 0.0);
    fx.ptCtx.ProcessPairSignal(pi);

    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "前置：MT 开仓单已建");
    if (!o) return;

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);      // 盘口 100.00 / 100.01
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "前置：主动腿已挂出（POST_ONLY 卖在 ask 100.01）");
    if (fx.NNew() != 1) return;

    auto pos = fx.PairOrders(o);
    if (pos.empty()) { CHECK(false, "前置：pairOrder 已登记"); return; }
    const std::string sref = MakeRef(o, pos[0].pairId);
    const int64_t soid = fx.News()[0]->clientOrderId;

    // 撤单只对 OS_NEW 生效（OS_PEND 是本地初态，交易所还没认）
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, 100.01, BINANCE, USDT_SWAP));

    // ---- 盘口不动 -> 不许撤 ----
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NCancel() == 0, "盘口没动 -> 不撤单（maker 挂单要留在簿子里赚返佣）");

    // ---- 主动腿卖价被甩开 21bp（阈值 activeMakerCancelOrderPct = 10bp）----
    dbp::DbpData moved = MakeData(99.79, 99.80, 100.00, 100.01,
                                  -1.0, -0.02, -1.0, -0.02, crypto::getCurrentTime());
    fx.algoCtx.OnSpread(&topic, &moved);
    CHECK(fx.NCancel() == 1, "卖价被市场甩开 21bp（> 10bp 阈值）-> 撤掉这条 maker 单");
    CHECK(fx.Cancels().size() == 1 && fx.Cancels()[0]->clientOrderId == soid,
          "撤的就是那条主动腿子单");

    auto kids = fx.ChildOrders(o);
    CHECK(kids.size() == 1 && kids[0].orderStatus == OS_CANCEL,
          "本地立刻置 OS_CANCEL（撤单请求已发出），不是直接删单 —— 终结要等交易所回报");
}

// ---- B. 关掉 rebalance -> 有在途子单时不重复报 ----
static void NoDuplicateWhileInFlight() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    // 关掉 rebalance 模式，并把单类型上限放大到 3：这样"第二帧没报单"只可能是
    // createFlag 拦的，不是 maxMTOrderSize 拦的。
    fx.ptCtx.m_cfg.mtRebalanceFlag = false;
    fx.ptCtx.m_cfg.maxMTOrderSize = 3.0;

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmMtOpen(pi, "mtOL", 0.0);
    fx.ptCtx.ProcessPairSignal(pi);

    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "前置：MT 开仓单已建");
    if (!o) return;
    CHECK(!o->mtRebalanceFlag, "算法单继承了 mtRebalanceFlag = false");
    CHECK(o->mtRebalanceSwitch, "mtRebalanceSwitch 仍是 true（关的是模式，不是能力）");
    CHECK(Near(o->maxMTOrderSize, 3.0), "maxMTOrderSize 从配置继承 = 3");

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);

    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "第一帧 -> 报出 1 条主动腿");
    if (fx.NNew() != 1) return;

    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "第二帧 -> 不重复报（子单还是 OS_PEND，在途）");

    auto pos = fx.PairOrders(o);
    if (!pos.empty()) {
        fx.algoCtx.OnOrder(MakeRtn(fx.News()[0]->clientOrderId, MakeRef(o, pos[0].pairId).c_str(),
                                   OS_NEW, 0.0, 100.01, BINANCE, USDT_SWAP));
    }
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "子单确认成 OS_NEW 后仍不重复报（OS_NEW 也算在途）");
    CHECK(fx.PairOrders(o).size() == 1, "始终只有一个 pairOrder");
}

static void Case() {
    MakerCancelOnPriceMove();
    NoDuplicateWhileInFlight();
}

int main() { return RunCase("16 MAKER_TAKER 撤单与多单上限", Case); }
