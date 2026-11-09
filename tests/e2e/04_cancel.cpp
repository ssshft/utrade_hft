// ===========================================================================
// 04 定时撤单 —— 机会连续不成立 -> 撤掉在跑的算法单 -> 收尾 CANCELED
//
// 判据是 `nowUs - pi.satisfyTime > algoOrderTimeoutMs`（默认 150s）。
// 注意它量的**不是订单年龄**，是"机会连续不成立的时长"：机会一成立
// UpdateSatisfyTime 就把 satisfyTime 刷新，单子就能一直挂着。所以本用例有两条：
//   ① satisfyTime 是新的 -> 不撤（单子还在追机会）
//   ② satisfyTime 是旧的 -> 撤（机会没了，别占着对子）
//
// 撤单是两段式的：先请求（算法单进 CANCELLING），下一帧才撤在途子单。
// 子单刚报出去是 OS_PEND，撤单分支有时间门槛，所以要先 AgeChildOrders。
// ===========================================================================
#include "harness.h"

// 建一张 ttOL 开仓单并报出主动腿，确认到 OS_NEW（可撤状态）
static BaseAlgoOrder* ArmInFlight(Fx& fx) {
    BaseAlgoOrder* o = OpenTtOL(fx);
    if (!o) return nullptr;
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshData();
    fx.algoCtx.OnSpread(&topic, &pdata);
    if (fx.NNew() != 1) return nullptr;
    auto pos = fx.PairOrders(o);
    if (pos.empty()) return nullptr;
    fx.algoCtx.OnOrder(MakeRtn(fx.News()[0]->clientOrderId,
                               MakeRef(o, pos[0].pairId).c_str(),
                               OS_NEW, 0.0, fx.News()[0]->limitPrice, BINANCE, USDT_SWAP));
    return o;
}

static void Case() {
    // ---- ① 机会仍在（satisfyTime 是新的）-> 不撤 ----
    {
        Fx fx;
        Boot(fx);
        GoTrading(fx);
        SeedFunds(fx);
        BaseAlgoOrder* o = ArmInFlight(fx);
        CHECK(o != nullptr, "前置：开仓单 + 在途主动腿");
        if (!o) return;

        PairInfo& pi = *fx.pi();
        pi.satisfyTime = crypto::getCurrentTime();      // 刚刚刷新过
        const size_t before = fx.modifyCalls.size();
        fx.ptCtx.OnTimer(crypto::getCurrentTime());

        CHECK(fx.modifyCalls.size() == before, "机会仍在（satisfyTime 是新的）-> 不发撤单");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "算法单状态不变");
    }

    // ---- ② 机会消失 151s -> 撤 ----
    {
        Fx fx;
        Boot(fx);
        GoTrading(fx);
        SeedFunds(fx);
        BaseAlgoOrder* o = ArmInFlight(fx);
        CHECK(o != nullptr, "前置：开仓单 + 在途主动腿");
        if (!o) return;

        PairInfo& pi = *fx.pi();
        const int64_t now = crypto::getCurrentTime();
        pi.satisfyTime = now - 151 * SEC;               // 超过 algoOrderTimeoutMs(150s)
        fx.ptCtx.OnTimer(now);

        CHECK(fx.modifyCalls.size() == 1, "机会 151s 不成立 -> 发出 1 次撤单请求");
        CHECK(!fx.modifyCalls.empty() && fx.modifyCalls[0].second == stra::CommandType_CANCEL,
              "请求类型 = CANCEL");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING,
              "执行侧收到 -> 算法单进入 CANCELLING（还没终结）");

        // ---- 撤在途子单 ----
        AgeChildOrders(o, 20 * 1000);                   // 跨过撤单的时间门槛（10ms）
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        pdata.spreadEffective = false;                  // 行情无效 -> 只走撤单分支，不报新单
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NCancel() >= 1, "CANCELLING -> 撤掉在途主动腿");

        // ---- 撤单回报 -> 收尾 ----
        const int64_t soid = fx.News()[0]->clientOrderId;
        auto pos = fx.PairOrders(o);
        if (!pos.empty()) {
            fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, pos[0].pairId).c_str(),
                                       OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
        }
        CHECK(fx.ChildOrders(o).empty(), "撤单回报 -> 子单簿清零");

        fx.algoCtx.OnTimer(crypto::getCurrentTime());
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELED, "OnTimer 收尾 -> ALGO_OS_CANCELED");
        CHECK(!fx.pi()->hasActiveAlgoOrder, "终结 -> 对子被释放");
    }
}

int main() { return RunCase("04 定时撤单", Case); }
