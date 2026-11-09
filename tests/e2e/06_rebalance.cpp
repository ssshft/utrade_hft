// ===========================================================================
// 06 敞口再平衡 —— 净敞口超阈值 -> stopFlag + reBalance -> 只平超出阈值的部分
//
// 与 05 的分界：强平是把两条腿都平干净；再平衡只平"多出来的那一截"，
// 平完净敞口 == 阈值（仍然有敞口，只是回到允许的上限）。
//
// 两个坐标系必须分清（这是这条链路唯一容易写错的地方）：
//   · 平多少 = 用**实盘持仓**算（交易所上的事实）
//   · 阶梯终点 = 写在**账本**坐标系里（算法单读的是 pairTotalVolume）
// 本用例里账本和实盘故意一样，所以两个口径重合；07/08 无关，这里是 06。
// ===========================================================================
#include "harness.h"

// 摆一个"净敞口异常"的局面。符号沿用 PairInfo 口径：多负空正。
static void ArmExposureAbnormal(PairInfo& pi, double activePos, double passivePos, double ttTargetVolume) {
    pi.pairTotalVolume = activePos;
    pi.pairPassiveTotalVolume = passivePos;
    pi.activeRealPosition = activePos;
    pi.passiveRealPosition = passivePos;
    pi.activeAvgPrice = 100.0;
    pi.passiveAvgPrice = 100.0;
    pi.activeParam.multiple = 1.0;
    pi.passiveParam.multiple = 1.0;
    pi.activeParam.minVolume = 0.1;
    pi.passiveParam.minVolume = 0.1;
    pi.ttTargetVolume = ttTargetVolume;
    pi.mtTargetVolume = ttTargetVolume;
    pi.maxVolume = 20.0;
    pi.minVolume = 0.1;
    pi.profitSwitch = true;
    pi.profitPct = 0.0001;
    pi.rtSpread.valid = true;
    pi.rtSpread.activePriceTema = 100.0;
    pi.rtSpread.passivePriceTema = 100.0;

    // 生产里八个开关不是"全开"。这里刻意全关，用来证明再平衡单的开关
    // 是它自己算出来的，不是从 orderParams 抄的。
    auto& op = pi.orderParams;
    op.ttOLSwitch = op.ttOSSwitch = op.ttCLSwitch = op.ttCSSwitch = false;
    op.mtOLSwitch = op.mtOSSwitch = op.mtCLSwitch = op.mtCSSwitch = false;
    op.ttCLEndVolume = op.ttCSEndVolume = op.mtCLEndVolume = op.mtCSEndVolume = 0.0;
}

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    // ttTargetVolume 2.5 -> 阈值 4 * 2.5 = 10；净敞口 |20 + (-5)| = 15 > 10
    ArmExposureAbnormal(pi, 20.0, -5.0, 2.5);

    // ---- ① 判据：只置标记 + 撤单，不建单 ----
    fx.ptCtx.CheckExposureAbnormal(pi);
    CHECK(pi.stopFlag, "净敞口 15 > 阈值 10 -> stopFlag（对子停手）");
    CHECK(pi.reBalance, "同时置 reBalance（把'平掉超出部分'交给下一环）");
    CHECK(fx.created.empty(), "判据本身不建单");

    // ---- ② 建单：ProcessRisk 在没有强平需求时走再平衡 ----
    fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "ProcessRisk（needForceClose == false）-> 建出再平衡单");
    if (!o) return;

    AlgoRebalanceOrder* r = (AlgoRebalanceOrder*)o;
    CHECK(o->algoType == stra::AlgoType_Rebalance, "algoType = AlgoType_Rebalance");
    CHECK(o->role == AlgoRole::CLOSE_SIGNAL, "role = CLOSE_SIGNAL（残量口径保证不会一建出来就 FILLED）");
    CHECK(r->activeTrade == 1, "主动腿敞口更大（20 > 5）-> 只动主动腿");
    CHECK(Near(r->targetResidualVolume, 15.0), "目标残量 = 20 - (15 - 10) = 15（只平超出阈值的 5）");
    CHECK(Near(o->ttCSEndVolume, 15.0), "阶梯终点 = 残量 15，不是 orderParams 里的 0（否则会平干净）");
    CHECK(o->ttCSSwitch && !o->ttCLSwitch, "持仓是空 -> 只开 CLOSE_SHORT，反方向关着");
    CHECK(!o->ttOLSwitch && !o->ttOSSwitch && !o->mtOLSwitch && !o->mtOSSwitch,
          "四个开仓开关全关（单腿单不许开仓）");
    CHECK(o->ttRebalanceSwitch && o->ttRebalanceFlag, "rebalance 开关打开（关着的话一条子单都不报）");
    CHECK(pi.hasActiveAlgoOrder && pi.currentAlgoOrderId == o->algoOrderId, "对子被这张单占住");

    // ---- ③ 报单：单腿、量 = min(持仓 - 残量, ttTargetVolume) ----
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshData();
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "报出 1 条子单");
    if (fx.NNew() == 1) {
        CHECK(Near(fx.News()[0]->volumeTotal, 2.5), "量 = min(5, ttTargetVolume 2.5) = 2.5");
        CHECK(fx.News()[0]->exchangeTypeEnum == BINANCE, "单腿 -> 只打主动腿");
    }

    // ---- ④ 收工：敞口回到阈值内 -> 清标记（但 stopFlag 不解，等人工 RESUME） ----
    pi.pairTotalVolume = 15.0;
    pi.activeRealPosition = 15.0;
    pi.hasActiveAlgoOrder = false;      // 单子撤掉了（生产里由超时撤单 -> OnAlgoFinished 完成）
    fx.ptCtx.ProcessExposureRebalance(pi);
    CHECK(!pi.reBalance, "敞口回到阈值内 -> 清 reBalance");
    CHECK(pi.stopFlag, "stopFlag 不解（压完仍然是'判死、等人工 RESUME'）");
}

int main() { return RunCase("06 敞口再平衡", Case); }
