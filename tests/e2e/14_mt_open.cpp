// ===========================================================================
// 14 MAKER_TAKER 开仓 —— MT/OL 全流程 + MT/OS 建单拆单
//
// 这是 MT 半边第一次被端到端跑到。与 01/02（TT）的分界点全在"拆单"这一层：
//   ① 主动腿报单类型是 OT_POST_ONLY，不是 TT 的 OT_LIMIT —— 一个是挂单赚 maker，
//      一个是穿价吃 taker，后面的撤单分支也按这个字段分叉（BaseAlgoOrder.cpp:465）。
//   ② 目标价取**对手价**：开多卖主动腿 -> 卖挂 ask；开空买主动腿 -> 买挂 bid。
//      所以子单价贴着盘口内一档，而 TT 的 OPEN_LONG 是穿价打 bid。
//   ③ 默认 mtRebalanceFlag = true ⇒ 走"同一类型只允许一个 pairOrder"那支
//      （AlgoContext.cpp:485）。第二帧不会再报第二条子单，这条断言是 MT 独有的。
//
// 两个方向各用一套独立的 Fx（Boot 会 ResetWorld），互不干扰。
// OL 走完整一圈（含成交与被动腿），OS 只到"建单 + 拆单"—— 成交链路已经由 OL 证明，
// 再跑一遍只是重复，不增加信息。
//
// 账本口径沿用 02：主动腿成交后子单会被 OnOrder 从子单簿删掉，
// 成交量要看 pairOrder.activeTotalVolumeOnOrder。
// ===========================================================================
#include "harness.h"

// ---- 开多：MT/OL，完整一圈 ----
static void OpenLong() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmMtOpen(pi, "mtOL", 0.0);

    // ---- ① 信号 -> 建单：模式与报单类型必须是 MT 那一套 ----
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "MT 开仓信号 -> 建出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;

    CHECK(o->tradingTypeOrder == stra::MAKER_TAKER, "tradingTypeOrder = MAKER_TAKER（MT/OL 那一路）");
    CHECK(o->activeOrderType == OT_POST_ONLY, "主动腿报单类型 = OT_POST_ONLY（挂单，不是穿价）");
    CHECK(o->role == AlgoRole::OPEN_SIGNAL, "role = OPEN_SIGNAL（终点是 maxVolume）");
    CHECK(o->mtRebalanceSwitch && o->mtRebalanceFlag,
          "rebalance 开关与模式都从配置继承（默认 true/true）");
    CHECK(Near(o->pairTotalVolume, 0.0), "算法单带着当前账本仓位建出来（开仓时是 0）");

    // ---- ② 拆单：MT 的价、方向、报单类型 ----
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);   // 开多只看 spreadAskAsk；盘口 100.00/100.01
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "行情进来 -> 报出 1 条主动腿子单");
    if (fx.NNew() != 1) return;

    const pubsub::NewOrder* n = fx.News()[0];
    CHECK(n->exchangeTypeEnum == BINANCE, "子单打主动腿 BINANCE");
    CHECK(n->direction == DT_SHORT, "OPEN_LONG 的主动腿是 DT_SHORT（配对方向定义，不是笔误）");
    CHECK(n->orderType == OT_POST_ONLY, "子单挂 POST_ONLY（maker 语义）");
    CHECK(Near(n->limitPrice, 100.01),
          "卖挂 ask = 100.01（MT 取对手价；TT 的 OPEN_LONG 是穿价打 bid）");
    CHECK(Near(n->volumeTotal, 1.0), "量 = min(|0 - mtOLEndVolume|, mtTargetVolume) = 1.0");

    // ---- ③ rebalance 模式：同一类型只允许一个 pairOrder ----
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "再喂一帧 -> 不重复报单（rebalance 模式一个类型只挂一个 pairOrder）");

    auto pos = fx.PairOrders(o);
    CHECK(pos.size() == 1, "pairOrder 已登记");
    if (pos.size() != 1) return;
    CHECK(pos[0].tradingTypeOrder == stra::MAKER_TAKER && pos[0].tradingTypeOffset == stra::OPEN_LONG,
          "pairOrder 记着 MAKER_TAKER + OPEN_LONG");
    CHECK(pos[0].rebalanceFlag, "pairOrder.rebalanceFlag 从 mtRebalanceFlag 继承");

    // ---- ④ 回报：确认 -> 成交 ----
    const int64_t soid = n->clientOrderId;
    const std::string sref = MakeRef(o, pos[0].pairId);

    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, n->limitPrice, BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 1, "交易所确认后主动腿仍在子单簿里");

    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_FILLED, 1.0, n->limitPrice, BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 0, "主动腿成交后从子单簿出清");
    CHECK(fx.NNew() == 2, "主动腿成交 -> 立刻补报被动腿");
    CHECK(Near(o->pairTotalVolume, 0.0),
          "此刻账本还没动：pairTotalVolume 只在 pairOrder 收口时更新（不是一成交就改）");
    {
        auto p2 = fx.PairOrders(o);
        CHECK(p2.size() == 1 && Near(p2[0].activeTotalVolumeOnOrder, 1.0),
              "成交量记在 pairOrder.activeTotalVolumeOnOrder = 1.0");
    }
    if (fx.NNew() != 2) return;

    // ---- ⑤ 被动腿：MT 走 posMgr 那一版 CreatePassiveOrder（按实际持仓算对冲量）----
    const pubsub::NewOrder* pn = fx.News()[1];
    CHECK(pn->exchangeTypeEnum == GATEIO, "被动腿打 GATEIO");
    CHECK(pn->direction == DT_LONG, "被动腿方向与主动腿对冲（DT_LONG）");
    CHECK(pn->orderType == OT_LIMIT, "被动腿是 OT_LIMIT（配置 passiveOrderType）");
    CHECK(Near(pn->volumeTotal, 1.0), "对冲量 = 主动腿已成交的 1 手");

    // ---- ⑥ 两腿收口 ----
    fx.algoCtx.OnOrder(MakeRtn(pn->clientOrderId, sref.c_str(), OS_NEW, 0.0, pn->limitPrice, GATEIO, USDT_SWAP));
    fx.algoCtx.OnOrder(MakeRtn(pn->clientOrderId, sref.c_str(), OS_FILLED, 1.0, pn->limitPrice, GATEIO, USDT_SWAP));

    CHECK(fx.ChildOrders(o).empty(), "两腿都终结 -> 子单簿清零");
    CHECK(fx.PairOrders(o).empty(), "两腿成交 -> pairOrder 收口并删除");
    CHECK(Near(o->pairTotalVolume, -1.0),
          "收口时账本才更新：主动腿卖 1 -> pairTotalVolume = -1（多负空正）");
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "开仓单两腿成交后仍是 ALGO_OS_NEW（终点是 10，才走了 1）");
}

// ---- 开空：MT/OS，建单 + 拆单 ----
static void OpenShort() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmMtOpen(pi, "mtOS", 0.0);

    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "MT/OS 开仓信号 -> 建出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;
    CHECK(o->tradingTypeOrder == stra::MAKER_TAKER, "tradingTypeOrder = MAKER_TAKER（MT/OS 那一路）");
    CHECK(o->mtOSSwitch && !o->mtOLSwitch, "开空 -> 只开 OPEN_SHORT，反方向关着");

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(0.02, -1.0);   // 开空只看 spreadBidBid
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "报出 1 条主动腿子单");
    if (fx.NNew() != 1) return;

    const pubsub::NewOrder* n = fx.News()[0];
    CHECK(n->direction == DT_LONG, "OPEN_SHORT 的主动腿是 DT_LONG（买，与 OL 相反）");
    CHECK(n->orderType == OT_POST_ONLY, "子单挂 POST_ONLY");
    CHECK(Near(n->limitPrice, 100.00), "买挂 bid = 100.00（开多时是卖挂 ask，方向镜像）");
    CHECK(Near(n->volumeTotal, 1.0), "量 = min(mtOSEndVolume - 0, mtTargetVolume) = 1.0");

    auto pos = fx.PairOrders(o);
    CHECK(pos.size() == 1 && pos[0].tradingTypeOffset == stra::OPEN_SHORT,
          "pairOrder.tradingTypeOffset = OPEN_SHORT");
}

static void Case() {
    OpenLong();
    OpenShort();
}

int main() { return RunCase("14 MAKER_TAKER 开仓", Case); }
