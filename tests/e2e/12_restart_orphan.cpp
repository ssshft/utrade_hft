// ===========================================================================
// 12 进程重启（场上有未终结的算法单）—— 孤儿单冻结
//
// 07/08 测的是"状态能不能原样读回来"；本用例测的是**最危险的那一种重启**：
// 快照里 hasActiveAlgoOrder == true，说明上一轮还有算法单没终结，
// 交易所侧可能残留子单在继续成交 —— 此时账本 pairTotalVolume 不可信。
//
// 处理方式：不清标记、不猜账本，直接置 stopFlag 冻结该对子，等人看日志确认
// 交易所已干净之后发 PairCmd_RESUME（11 号用例）复活。
//
// ⚠️ 必须在 Init 里做，不能等到 ScanFinishedAlgoOrders：
//    后者第一次跑就会因为"算法单在 AlgoContext 里找不到"把 hasActiveAlgoOrder 清掉，
//    之后再也看不出这轮重启是有问题的。
// ===========================================================================
#include "harness.h"

static void Case() {
    // ---- 第一轮：留下一个"有在途算法单"的快照 ----
    {
        Fx a;
        Boot(a);
        GoTrading(a);
        PairInfo& pi = *a.pi();
        pi.pairTotalVolume    = -3.2;
        pi.stopFlag           = false;    // 上一轮是正常跑的
        pi.closeFlag          = false;
        pi.reBalance          = true;     // 故意置上：它不进快照，用来验证"不持久化"
        pi.hasActiveAlgoOrder = true;     // 关键：上一轮有单没终结
        pi.currentAlgoOrderId = 777;
        CHECK(PairInfoManager::Instance().SaveSnapshot(TempPath("pair_info.csv")), "快照写盘");
    }

    // ---- 第二轮：同一组文件上重新 Init ----
    Fx b;
    Boot(b, /*keepState=*/true);

    PairInfo* p = b.pi();
    CHECK(p != nullptr, "重启后对子仍在");
    if (!p) return;

    CHECK(p->stopFlag, "孤儿单 -> stopFlag = true（冻结，开仓和平仓都不发）");
    CHECK(p->hasActiveAlgoOrder, "hasActiveAlgoOrder 保留为 true（清掉就再也看不出痕迹）");
    CHECK(p->currentAlgoOrderId == 777, "currentAlgoOrderId 读回来 = 777（日志要能报出是哪张单）");
    CHECK(Near(p->pairTotalVolume, -3.2), "账本仍然读回来（不可信但不清零 —— 那是人工的判断）");
    CHECK(!p->reBalance, "reBalance 不进快照 -> 重启后是 false");
    CHECK(b.ptCtx.m_phase == PairTradingContext::StartupPhase::Reconciling,
          "仍然先走对账，不会拿这本可疑的账直接下单");

    // ---- 行为：冻结期间连平仓都不发 ----
    GoTrading(b);
    MakeHoldable(*p, -3.2);              // 量摆回来，避免"本来就报不出单"的假阴性

    auto& sg = SignalGenerator::Instance();
    std::string reason;
    CHECK(!sg.CanOpen(*p, reason), "CanOpen = false");
    CHECK(reason == "stopFlag = true", "原因是孤儿冻结写的 stopFlag");
    CHECK(!sg.CanClose(*p, reason), "CanClose = false（冻结 = 平仓也停，不是只停开仓）");

    ArmClose(*p, "ttCL", -3.2);
    CHECK(sg.CheckSignalForSatisfy(*p).ttCLSignal, "前提：平仓信号确实成立");
    b.ptCtx.ProcessPairSignal(*p);
    CHECK(b.created.empty(), "平仓信号成立，冻结下也不建单");
}

int main() { return RunCase("12 重启孤儿单冻结", Case); }
