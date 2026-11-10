// ===========================================================================
// 17 回报推送 —— 分批成交 / 报单被拒 / 乱序与重复
//
// 14~16 只推了 OS_NEW 和 OS_FILLED 两条，成交是"一步到位"的。真实交易所是**推送流**：
// 一张单会停在 OS_PARTFILLED 上被推好几次，也可能直接 OS_REJECTED。
// 这个文件就是回答"能不能模拟回报推送"：能 —— `AlgoContext::OnOrder` 是回报的**唯一入口**，
// 交易所回调最终也是把 `pubsub::OrderResponse` 喂给它，所以在这里推和真实推送走的是同一段代码。
// 推不了的是**传输层**（dbp::DbpReader / QuantTrade 在夹具里是桩），也就是说：
// 这里能证明"策略/执行逻辑对不对"，证明不了"订阅和 socket 通不通"。
//
// 三个此前 0 命中的分支，全靠这个文件才第一次被执行：
//   AlgoContext.cpp:1194  主动腿**未终结**但有成交 -> 先补被动腿（不等整单成交完）
//   AlgoContext.cpp:1172  报单被拒走 LOG_ERROR 那一支终结
//   AlgoContext.cpp:1205  回报找不到算法单 -> 丢弃
//
// 🔴 写这类用例只有一条硬规矩：`volumeTraded` 是**累计量**，不是增量。
//    QuantOrder::UpdateOrderOnOrder 里 `tradeVolume = volumeTraded - totalVolumeOnOrder`
//    （DataStruct.h:789），所以"先成交 0.4，再成交 0.3"要推 0.4 然后 0.7；
//    推 0.4 再推 0.4 是**重复回报**（增量 0，不再对冲），推 0.4 再推 1.0 是"一步到位"，
//    会直接掉进终结支，:1194 那一行永远测不到。
//
// 另一个必须记住的：`AlgoContext.cpp:1060` 有一条乱序过滤
//    `if (quantOrder.totalVolumeOnOrder - orderResponse.volumeTraded > MIN_FLOAT) return;`
//    累计量比已记录的**小**就整条丢弃 —— 这是推送流能安全重放的前提，也是下面②③的前提。
// ===========================================================================
#include "harness.h"

// 把一条回报推进执行层 —— 和交易所推送同一个入口（AlgoContext::OnOrder）。
// 参数就是回报里真正被读的那几个：clientOrderId（= 子单的 strategyOrderId）、
// strategyRef（"algoOrderId_pairId"）、状态、**累计**成交量、成交价、交易所。
static void Push(Fx& fx, int64_t clientOrderId, const std::string& sref, OrderStatus st,
                 double cumVolume, double price, ExchangeType exch = BINANCE,
                 ApiSource api = AS_ADD_NEW_ORDER) {
    fx.algoCtx.OnOrder(MakeRtn(clientOrderId, sref.c_str(), st, cumVolume, price, exch, USDT_SWAP, api));
}

// 按 clientOrderId 取子单。⚠️ 不能写 `ChildOrders(o)[0]` —— 部分成交一发生，
// 子单簿里就有**两条**（主动腿 + 对冲腿），按下标取会摸到另一条腿上。
static bool ChildOf(BaseAlgoOrder* o, int64_t clientOrderId, stra::QuantOrder* out) {
    for (const auto& kv : o->orderMgr.GetAllOrders()) {
        if (kv.first == clientOrderId) {
            *out = kv.second;
            return true;
        }
    }
    return false;
}

// 开一张 MT/OL 算法单并报出第一条主动腿。三个出参都是后面推回报要用的定位信息。
// 返回 nullptr 表示链路没走通（建单或拆单失败），调用方直接放弃。
static BaseAlgoOrder* OpenMtOL(Fx& fx, int64_t* soid, double* px, std::string* sref) {
    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmMtOpen(pi, "mtOL", 0.0);
    fx.ptCtx.ProcessPairSignal(pi);
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return nullptr;

    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);   // 开多只看 spreadAskAsk
    fx.algoCtx.OnSpread(&topic, &pdata);
    if (fx.NNew() != 1) return nullptr;

    const pubsub::NewOrder* n = fx.News()[0];
    *soid = n->clientOrderId;
    *px   = n->limitPrice;
    auto pos = fx.PairOrders(o);
    if (pos.empty()) return nullptr;
    *sref = MakeRef(o, pos[0].pairId);
    return o;
}

// ---- 分批成交：0.4 -> 0.7 -> 1.0，每条对冲腿都只补差额 ----
static void PartialFill() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    int64_t soid = 0;
    double px = 0.0;
    std::string sref;
    BaseAlgoOrder* o = OpenMtOL(fx, &soid, &px, &sref);
    if (!o) {
        CHECK(false, "MT/OL 建单 + 拆单没走通，后面的断言无从谈起");
        return;
    }

    // ---- ① 受理 -> 确认：没有成交就不该有被动腿 ----
    Push(fx, soid, sref, OS_PENDING_NEW, 0.0, px);
    Push(fx, soid, sref, OS_NEW, 0.0, px);
    CHECK(fx.NNew() == 1, "只确认不成交 -> 不报被动腿（场上没有敞口）");
    CHECK(fx.NActiveChild(o) == 1, "主动腿在子单簿里等成交");
    {
        auto kids = fx.ChildOrders(o);
        CHECK(kids.size() == 1 && kids[0].orderStatus == OS_NEW,
              "子单状态跟着回报走到 OS_NEW（PENDING_NEW 只是过渡态）");
    }

    // ---- ② 部分成交 0.4：主动腿还没终结，但敞口已经产生，必须马上对冲 ----
    Push(fx, soid, sref, OS_PARTFILLED, 0.4, px);
    CHECK(fx.NNew() == 2, "部分成交 -> 立刻补被动腿（AlgoContext.cpp:1194，不等整单终结）");
    CHECK(fx.NActiveChild(o) == 1, "主动腿未终结，仍在子单簿里（剩 0.6 还挂着）");
    CHECK(Near(o->pairTotalVolume, 0.0),
          "账本还没动：pairTotalVolume 只在 pairOrder 收口时结算");
    {
        auto p = fx.PairOrders(o);
        CHECK(p.size() == 1 && Near(p[0].activeTotalVolumeOnOrder, 0.4),
              "已成交量记在 pairOrder.activeTotalVolumeOnOrder = 0.4");
    }
    if (fx.NNew() != 2) return;
    const int64_t p1   = fx.News()[1]->clientOrderId;
    const double  p1px = fx.News()[1]->limitPrice;
    CHECK(Near(fx.News()[1]->volumeTotal, 0.4),
          "对冲量 = 已成交的 0.4（按实仓算，不是整单 1.0）");

    // ---- ③ 再成交 0.3（累计 0.7）：只补差额，不重复对冲 ----
    Push(fx, soid, sref, OS_PARTFILLED, 0.7, px);
    CHECK(fx.NNew() == 3, "第二次部分成交 -> 再补一条被动腿");
    if (fx.NNew() != 3) return;
    const int64_t p2   = fx.News()[2]->clientOrderId;
    const double  p2px = fx.News()[2]->limitPrice;
    CHECK(Near(fx.News()[2]->volumeTotal, 0.3),
          "只补差额 0.3（累计 0.7 - 已对冲 0.4），不会把 0.7 整笔再对冲一遍");

    // ---- ④ 主动腿成交完（累计 1.0）：走终结支，把最后 0.3 补齐 ----
    Push(fx, soid, sref, OS_FILLED, 1.0, px);
    CHECK(fx.NActiveChild(o) == 0, "主动腿成交完 -> 从子单簿删除");
    CHECK(fx.NNew() == 4, "终结支同样走 PairOrderTrade：再补最后一条 0.3");
    if (fx.NNew() != 4) return;
    const int64_t p3   = fx.News()[3]->clientOrderId;
    const double  p3px = fx.News()[3]->limitPrice;
    CHECK(Near(fx.News()[3]->volumeTotal, 0.3), "累计对冲量凑到 0.4 + 0.3 + 0.3 = 1.0");
    CHECK(Near(o->pairTotalVolume, 0.0), "被动腿还挂着 -> pairOrder 未收口，账本仍不动");

    // ---- ⑤ 三条对冲腿陆续成交 -> 收口 ----
    Push(fx, p1, sref, OS_NEW, 0.0, p1px, GATEIO);
    Push(fx, p1, sref, OS_FILLED, 0.4, p1px, GATEIO);
    CHECK(fx.NNew() == 4, "对冲腿成交不会反过来再报单（敞口已对齐，目标量算出来是 0）");
    Push(fx, p2, sref, OS_FILLED, 0.3, p2px, GATEIO);
    CHECK(fx.NNew() == 4, "第二腿成交也不再报单");
    CHECK(fx.PairOrders(o).size() == 1, "还有一条对冲腿挂着 -> pairOrder 不能收口");
    Push(fx, p3, sref, OS_FILLED, 0.3, p3px, GATEIO);

    CHECK(fx.ChildOrders(o).empty(), "四条子单全部终结 -> 子单簿清零");
    CHECK(fx.PairOrders(o).empty(), "最后一条对冲腿成交 -> pairOrder 收口删除");
    CHECK(Near(o->pairTotalVolume, -1.0),
          "分批成交同样只在收口时结算：主动腿卖 1 -> pairTotalVolume = -1（多负空正）");
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "算法单还活着（终点是 10，只走了 1）");
}

// ---- 报单被拒：一笔没成，pairOrder 直接收口，且不能把这一轮卡死 ----
static void Reject() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    int64_t soid = 0;
    double px = 0.0;
    std::string sref;
    BaseAlgoOrder* o = OpenMtOL(fx, &soid, &px, &sref);
    if (!o) {
        CHECK(false, "MT/OL 建单 + 拆单没走通，后面的断言无从谈起");
        return;
    }

    Push(fx, soid, sref, OS_REJECTED, 0.0, px);
    CHECK(fx.ChildOrders(o).empty(), "被拒的主动腿从子单簿删除（不重试、也不留挂单）");
    CHECK(fx.PairOrders(o).empty(), "pairOrder 跟着收口删除（一笔没成，没有敞口要对冲）");
    CHECK(fx.NNew() == 1, "被拒 -> 不报被动腿");
    CHECK(Near(o->pairTotalVolume, 0.0), "账本不动：一笔都没成交");
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW,
          "一次拒单不等于停机：算法单还活着，下个信号照常报");

    // 拒单之后这条对子没有在途单了，下一帧必须能重新报 ——
    // 否则一次拒单（限频/保证金/价格越界都可能触发）就把这一轮锁死到超时。
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = MtData(-1.0, -0.02);
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 2, "拒单后下一帧重新报出主动腿（没有卡在'已有一单在途'）");
}

// ---- 推送流的健壮性：乱序、重复、无主回报都不能改状态 ----
static void StreamHygiene() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    int64_t soid = 0;
    double px = 0.0;
    std::string sref;
    BaseAlgoOrder* o = OpenMtOL(fx, &soid, &px, &sref);
    if (!o) {
        CHECK(false, "MT/OL 建单 + 拆单没走通，后面的断言无从谈起");
        return;
    }

    Push(fx, soid, sref, OS_NEW, 0.0, px);
    Push(fx, soid, sref, OS_PARTFILLED, 0.4, px);
    if (fx.NNew() != 2) {
        CHECK(false, "前置条件不成立：0.4 部分成交没补出被动腿");
        return;
    }
    const size_t base = fx.NNew();

    // ---- ① 迟到的新单确认（累计量比已记录的少）-> 整条丢弃 ----
    Push(fx, soid, sref, OS_NEW, 0.0, px);
    {
        stra::QuantOrder c;
        const bool got = ChildOf(o, soid, &c);
        CHECK(fx.NNew() == base, "迟到的新单确认不补单");
        CHECK(got && c.orderStatus == OS_PARTFILLED,
              "状态不被倒退（乱序回报整条丢弃，AlgoContext.cpp:1060）");
        CHECK(got && Near(c.totalVolumeOnOrder, 0.4), "累计成交量不被倒退");
    }

    // ---- ② 重复的同一笔部分成交（累计量没变）-> 增量 0 -> 不再对冲 ----
    Push(fx, soid, sref, OS_PARTFILLED, 0.4, px);
    {
        stra::QuantOrder c;
        const bool got = ChildOf(o, soid, &c);
        CHECK(fx.NNew() == base, "重复回报不重复对冲（同一条推送重放是幂等的）");
        CHECK(got && Near(c.tradeVolume, 0.0), "tradeVolume 是增量：累计量没变 -> 0");
    }

    // ---- ③ 成交量倒退 -> 丢弃 ----
    Push(fx, soid, sref, OS_PARTFILLED, 0.2, px);
    {
        stra::QuantOrder c;
        const bool got = ChildOf(o, soid, &c);
        CHECK(fx.NNew() == base, "倒退的成交量不补单");
        CHECK(got && Near(c.totalVolumeOnOrder, 0.4), "倒退的成交量被丢弃");
    }

    // ---- ④ 查询回报走另一支：queryCount 记账，推送回报再清回去 ----
    Push(fx, soid, sref, OS_PARTFILLED, 0.4, px, BINANCE, AS_QUERY_ORDER);
    {
        stra::QuantOrder c;
        CHECK(ChildOf(o, soid, &c) && c.queryCount == 1,
              "AS_QUERY_ORDER 走 UpdateOrderOnQueryOrder（queryCount += 1）");
    }
    Push(fx, soid, sref, OS_PARTFILLED, 0.4, px);
    {
        stra::QuantOrder c;
        CHECK(ChildOf(o, soid, &c) && c.queryCount == 0,
              "推送回报把 queryCount 清回 0（推送与查询共用同一份子单）");
    }

    // ---- ⑤ 无主回报：algoOrderId 不存在 / strategyRef 解析不出 -> 丢弃，不崩 ----
    Push(fx, soid, "999999_1", OS_FILLED, 1.0, px);
    Push(fx, soid, "", OS_FILLED, 1.0, px);
    CHECK(fx.NNew() == base, "无主回报被丢弃，不补单（AlgoContext.cpp:1205 / :1041）");
    CHECK(fx.NActiveChild(o) == 1, "主动腿仍在子单簿里 —— 没有被无主回报误删");
}

static void Case() {
    PartialFill();
    Reject();
    StreamHygiene();
}

int main() { return RunCase("17 回报推送", Case); }
