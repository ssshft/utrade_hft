// ===========================================================================
// 02 成交回报 —— 主动腿成交 -> 补报被动腿 -> 两腿收口
//
// 要记住的一件事：主动腿成交后**不会**以 OS_FILLED 留在子单簿里，
// OnOrder 对 FILLED/REJECTED/CANCELED 三种终态会把它删掉（"订单完结解冻"）。
// 所以成交量要去 pairOrder.activeTotalVolumeOnOrder 上看。
//
// 最后一条断言是本用例的重点：开仓单两腿都成交之后**不终结** ——
// 它的终点是 maxVolume(10)，才成交 1 手，后面还要接着往上爬。
// ===========================================================================
#include "harness.h"

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    BaseAlgoOrder* o = OpenTtOL(fx);
    CHECK(o != nullptr, "前置：开仓单已建");
    if (!o) return;

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshData();
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "前置：主动腿子单已报出");
    auto pos = fx.PairOrders(o);
    if (fx.NNew() != 1 || pos.empty()) return;

    const int64_t soid = fx.News()[0]->clientOrderId;
    const int64_t pairId = pos[0].pairId;
    const double  px = fx.News()[0]->limitPrice;
    const std::string sref = MakeRef(o, pairId);

    // ---- 交易所确认（OS_PEND -> OS_NEW）----
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, px, BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 1, "确认后主动腿仍在途（还在子单簿里）");

    // ---- 主动腿全成 -> 子单出清 + 立刻补报被动腿 ----
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_FILLED, 1.0, px, BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 0, "主动腿成交后从子单簿出清");
    CHECK(fx.NNew() == 2, "主动腿成交 -> 立刻补报被动腿");
    {
        auto p2 = fx.PairOrders(o);
        CHECK(p2.size() == 1 && Near(p2[0].activeTotalVolumeOnOrder, 1.0),
              "成交量记在 pairOrder.activeTotalVolumeOnOrder = 1.0");
    }
    if (fx.NNew() != 2) return;

    // ---- 被动腿成交 -> 整张 pairOrder 收口 ----
    const pubsub::NewOrder* pn = fx.News()[1];
    CHECK(pn->exchangeTypeEnum == GATEIO && pn->direction == DT_LONG,
          "被动腿打 GATEIO、方向 DT_LONG（与主动腿对冲）");
    const int64_t psoid = pn->clientOrderId;
    const double  ppx   = pn->limitPrice;

    fx.algoCtx.OnOrder(MakeRtn(psoid, sref.c_str(), OS_NEW,    0.0, ppx, GATEIO, USDT_SWAP));
    fx.algoCtx.OnOrder(MakeRtn(psoid, sref.c_str(), OS_FILLED, 1.0, ppx, GATEIO, USDT_SWAP));

    CHECK(fx.ChildOrders(o).empty(), "两腿都终结 -> 子单簿清零");
    CHECK(fx.PairOrders(o).empty(), "两腿成交 -> pairOrder 收口并删除");

    // ---- 收口后的状态 ----
    // 非终态回传只同步量/价，不释放对子；开仓单还要接着往上爬
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "开仓单两腿成交后仍是 ALGO_OS_NEW（终点是 10，才走了 1）");
    CHECK(fx.pi()->hasActiveAlgoOrder, "对子仍被占着（单子没终结，不许再建第二张）");
}

int main() { return RunCase("02 成交回报", Case); }
