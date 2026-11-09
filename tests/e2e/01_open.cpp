// ===========================================================================
// 01 正常开仓 —— 行情触发信号 -> 建算法单 -> 拆出主动腿子单
//
// 这条链路的四步：
//   ① 信号：PairTradingContext::ProcessPairSignal 看到 ttOLSignal -> 建算法单
//   ② 注册：策略层的回调把它交给 AlgoContext（生产里是同一个进程直接调）
//   ③ 拆单：AlgoContext::OnSpread 用实时盘口算目标价/目标量 -> 报主动腿
//   ④ 占位：PairInfo::hasActiveAlgoOrder = true，这个对子不许再建第二张单
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmOpen(pi, "ttOL", 0.0);

    // ---- ① 信号成立 -> 建单 ----
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "开仓信号 -> 建出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;

    CHECK(o->algoType == stra::AlgoType_PairTrading, "algoType = PairTrading");
    CHECK(o->role == AlgoRole::OPEN_SIGNAL, "role = OPEN_SIGNAL（终点是 maxVolume，不是 0）");
    CHECK(o->tradingTypeOrder == stra::TAKER_TAKER, "tradingTypeOrder = TAKER_TAKER（autoFlag 默认 true 走 TT）");
    CHECK(Near(o->pairTotalVolume, 0.0), "算法单带着当前账本仓位建出来（开仓时是 0）");

    // ---- ② 已入执行侧 ----
    CHECK(fx.AlgoOrder(o->algoOrderId) == o, "算法单已注册进执行侧 AlgoContext");
    CHECK(pi.hasActiveAlgoOrder && pi.currentAlgoOrderId == o->algoOrderId,
          "对子已被这张单占住（同一对子不许并发两张）");

    // ---- ③ 拆单报单 ----
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshData();
    fx.algoCtx.OnSpread(&topic, &pdata);

    CHECK(fx.NNew() == 1, "行情进来 -> 报出 1 条子单（TT 单只报主动腿）");
    if (fx.NNew() != 1) return;

    const pubsub::NewOrder* n = fx.News()[0];
    CHECK(n->exchangeTypeEnum == BINANCE, "子单打主动腿 BINANCE");
    // OPEN_LONG 的腿约定：主动腿 DT_SHORT、被动腿 DT_LONG（配对交易的方向定义，不是笔误）
    CHECK(n->offsetFlag == OF_OPEN && n->direction == DT_SHORT,
          "OPEN_LONG -> 主动腿 DT_SHORT + OF_OPEN");
    CHECK(Near(n->volumeTotal, 1.0), "子单量 = 1.0（maxTTOrderSize 一次一手）");
    CHECK(n->orderType == OT_LIMIT, "TT 主动腿是穿价限价（OT_LIMIT），不是市价");

    // ---- ④ 子单簿 / 配对单簿 ----
    auto kids = fx.ChildOrders(o);
    CHECK(kids.size() == 1 && kids[0].orderStatus == OS_PEND,
          "子单已登记，初始状态 OS_PEND（等交易所确认，不是 OS_NEW）");
    auto pos = fx.PairOrders(o);
    CHECK(pos.size() == 1 && pos[0].algoPairId == o->algoOrderId,
          "pairOrder 已登记，且 algoPairId == algoOrderId");
}

int main() { return RunCase("01 正常开仓", Case); }
