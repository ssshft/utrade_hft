// ===========================================================================
// 05 风控强平 —— 真实风控条件命中 -> RISK_CLOSE 单 -> 平干净
//
// 这里不用"造一个假的风控开关"：RiskManager 的第一条就是碎单检查
//     |pairTotalVolume| > 0  且  持仓市值 < 25 USDT
// 取 0.01 手、腿价 100、multiple 1 -> 市值 1 USDT < 25，真实命中。
//
// 强平单与普通平仓单的三处区别（本用例就是在钉这三条）：
//   ① 四个开仓开关**建单即清**：逆着强平方向加仓是最坏的失败模式
//   ② 四个平仓阶梯末端归零：强平的定义是平干净，不是平到半路
//   ③ profitSwitch 关掉、profitPct 归零：强平不许要求盈利（否则亏损时平不出去）
// ===========================================================================
#include "harness.h"

// 小持仓 -> CheckRisk 命中碎单 -> needForceClose
static void ArmTinyCloseRisk(PairInfo& pi) {
    pi.pairTotalVolume = 0.01;
    pi.activeParam.calcType = 0;
    pi.activeParam.multiple = 1.0;
    pi.rtSpread.activePriceTema = 100.0;
    pi.rtSpread.valid = true;
}

static void Case() {
    Fx fx;
    Boot(fx);
    GoTrading(fx);
    SeedFunds(fx);

    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);       // 只为拿到 ttTargetVolume（否则建单会因为 targetVolume<=0 失败）
    ArmTinyCloseRisk(pi);
    // 先给一个"平到半路"的残余量，验证强平会把它归零
    pi.closeRemainVolume = 3.0;
    pi.orderParams.ttCLEndVolume = -3.0;
    pi.orderParams.mtCSEndVolume = 3.0;

    CHECK(std::abs(pi.CalcPositionValue()) < 25.0, "前置：持仓市值 < 碎单门槛 25 USDT");

    fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "风控命中 -> 建出 1 张算法单");
    if (!o) return;

    CHECK(o->role == AlgoRole::RISK_CLOSE, "role = RISK_CLOSE（不是 CLOSE_SIGNAL）");
    CHECK(!o->ttOLSwitch && !o->ttOSSwitch && !o->mtOLSwitch && !o->mtOSSwitch,
          "建单即清掉四个开仓开关（强平单终生不许开仓）");
    CHECK(Near(o->ttCLEndVolume, 0.0) && Near(o->ttCSEndVolume, 0.0) &&
          Near(o->mtCLEndVolume, 0.0) && Near(o->mtCSEndVolume, 0.0),
          "四个平仓阶梯末端归零（强平 = 平干净，不受 closeRemainVolume 影响）");
    CHECK(pi.activeAlgoIsRiskClose, "PairInfo.activeAlgoIsRiskClose = true（MODIFY 侧靠它压开关）");
    CHECK(pi.riskCloseOrderInFlight, "riskCloseOrderInFlight = true（下一轮不撤自己刚发的单）");
    // ③ 盈利保护必须关掉：否则亏损状态下 AlgoPairOrder 的 profitPct 守卫会把目标量
    //    夹回满仓、报空单，让利 tier1/2/3 全部空转（强平根本平不出去）
    CHECK(!o->profitSwitch && Near(o->profitPct, 0.0),
          "建单即关掉盈利保护（强平不许要求盈利）");

    // 平到 0 -> 终结
    o->pairTotalVolume = 0.0;
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_FILLED, "平到 0 -> FILLED");
}

int main() { return RunCase("05 风控强平", Case); }
