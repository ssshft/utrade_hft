// ===========================================================================
// 15 MAKER_TAKER 平仓 —— MT/CL 平多 + MT/CS 平空：建单、拆单方向与挂单价、平到 0 终结
//
// 与 03（TT 平仓）的分界全在拆单这一层，两条都是 MT 独有的：
//   ① 平多的主动腿是**买**（DT_LONG），挂 bid —— 把之前卖出去的那条腿买回来；
//      平空的主动腿是**卖**（DT_SHORT），挂 ask。TT 则是穿价。
//   ② 子单仍然是 OT_POST_ONLY（配置 mtActiveOrderType），所以撤单侧走的是
//      BaseAlgoOrder 的 maker 分支，不是 taker 分支。
//
// 两个方向各用一套独立的 Fx（Boot 会 ResetWorld），互不干扰。
// 这里只测到"建单 + 拆单 + 平到 0 终结"：主动腿**不成交**，是撤掉之后把账本手写到 0。
// "平仓成交 -> 对冲被动腿"没在这条里测，原因见 README 的缺口清单。
// ===========================================================================
#include "harness.h"

// ---- 平多：MT/CL ----
static void CloseLong() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, -5.0);            // 对子多 5 手（多 = 负）
    ArmMtClose(pi, "mtCL", -5.0);
    pi.orderParams.mtOLSwitch = true;  // 开仓开关留开着：平仓单允许在开仓带回补

    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "MT/CL 平仓信号 -> 建出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;

    CHECK(o->tradingTypeOrder == stra::MAKER_TAKER, "tradingTypeOrder = MAKER_TAKER");
    CHECK(o->role == AlgoRole::CLOSE_SIGNAL, "role = CLOSE_SIGNAL（平到 0 算结束）");
    CHECK(Near(o->pairTotalVolume, -5.0), "算法单带着 -5 的账本仓位建出来");
    CHECK(o->mtOLSwitch, "普通平仓单保留开仓开关（允许在开仓带回补）");

    // ---- 拆单：平多 = 买回主动腿，挂 bid ----
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(0.02, -1.0);   // 平多只看 spreadBidBid
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "平仓单按同一套拆单逻辑报出子单");
    if (fx.NNew() != 1) return;

    const pubsub::NewOrder* n = fx.News()[0];
    CHECK(n->direction == DT_LONG, "CLOSE_LONG 的主动腿是 DT_LONG（买回那条空腿）");
    CHECK(n->orderType == OT_POST_ONLY, "子单仍挂 POST_ONLY");
    CHECK(Near(n->limitPrice, 100.00), "买挂 bid = 100.00（不是穿价打 ask）");
    CHECK(Near(n->volumeTotal, 5.0), "量 = 5（把持仓一次平完）");

    auto pos = fx.PairOrders(o);
    CHECK(pos.size() == 1 && pos[0].tradingTypeOffset == stra::CLOSE_LONG,
          "pairOrder.tradingTypeOffset = CLOSE_LONG（平仓意图在这一层表达）");
    if (pos.size() != 1) return;

    // ---- 撤掉没成交的子单，让 pairOrder 收口 ----
    fx.algoCtx.OnOrder(MakeRtn(n->clientOrderId, MakeRef(o, pos[0].pairId).c_str(),
                               OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
    CHECK(fx.PairOrders(o).empty(), "撤单回报 -> pairOrder 收口");

    // ---- 平到 0 -> 立刻终结（不用等 150s 超时）----
    o->pairTotalVolume = 0.0;
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_FILLED, "平到 0 -> ALGO_OS_FILLED");
    CHECK(o->commandType == stra::CommandType_FINISHED, "commandType = FINISHED");
    CHECK(!pi.hasActiveAlgoOrder, "终结回传 -> 对子被释放");
}

// ---- 平空：MT/CS ----
static void CloseShort() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 5.0);             // 对子空 5 手（空 = 正）
    ArmMtClose(pi, "mtCS", 5.0);

    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "MT/CS 平仓信号 -> 建出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;

    CHECK(o->tradingTypeOrder == stra::MAKER_TAKER, "tradingTypeOrder = MAKER_TAKER");
    CHECK(o->role == AlgoRole::CLOSE_SIGNAL, "role = CLOSE_SIGNAL");
    CHECK(o->mtCSSwitch && !o->mtCLSwitch, "平空 -> 只开 CLOSE_SHORT，反方向关着");

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);  // 平空只看 spreadAskAsk
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "报出 1 条子单");
    if (fx.NNew() != 1) return;

    const pubsub::NewOrder* n = fx.News()[0];
    CHECK(n->direction == DT_SHORT, "CLOSE_SHORT 的主动腿是 DT_SHORT（卖回那条多头腿）");
    CHECK(n->orderType == OT_POST_ONLY, "子单仍挂 POST_ONLY");
    CHECK(Near(n->limitPrice, 100.01), "卖挂 ask = 100.01");
    CHECK(Near(n->volumeTotal, 5.0), "量 = 5");

    auto pos = fx.PairOrders(o);
    CHECK(pos.size() == 1 && pos[0].tradingTypeOffset == stra::CLOSE_SHORT,
          "pairOrder.tradingTypeOffset = CLOSE_SHORT");
    if (pos.size() != 1) return;

    fx.algoCtx.OnOrder(MakeRtn(n->clientOrderId, MakeRef(o, pos[0].pairId).c_str(),
                               OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
    CHECK(fx.PairOrders(o).empty(), "撤单回报 -> pairOrder 收口");

    o->pairTotalVolume = 0.0;
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_FILLED, "平到 0 -> ALGO_OS_FILLED");
    CHECK(!pi.hasActiveAlgoOrder, "终结回传 -> 对子被释放");
}

static void Case() {
    CloseLong();
    CloseShort();
}

int main() { return RunCase("15 MAKER_TAKER 平仓", Case); }
