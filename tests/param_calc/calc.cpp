// ===========================================================================
// 开仓参数计算器
//
// 链接**真实的** SignalGenerator.cpp，用真实公式回答三个问题：
//   1) 给定价差分位数，推导出的 orderParams（8 个 StartSpread）是多少？
//   2) 四个开仓开关（ttOL/ttOS/mtOL/mtOS）分别能不能打开？
//   3) 要让某个方向能开仓，分位数至少得达到多少 / 参数得怎么改？
//
// 价差口径（务必先看懂这一段，否则所有数字都会理解错）：
//   dbp 的 spreadcalctype 在 dbp/etc/dbprocess.xml 里是 1 = SPCT_PRICEDIV2
//   （见 dbp/sig/dbp/include.h:9-14），即
//       spread = (d2 - d1) / d1
//   而 dbp/dbprocess/dbsnap.h:303-306 的调用是
//       spreadBidAsk = calcspread(activeBidPrice[0], passiveAskPrice[0])
//       ...
//   所以四个轴是**相对价差**（无量纲），且 active = pairKey 的左边、passive = 右边：
//       spreadBidAsk = (passiveAsk - activeBid) / activeBid
//       spreadBidBid = (passiveBid - activeBid) / activeBid
//       spreadAskBid = (passiveBid - activeAsk) / activeAsk
//       spreadAskAsk = (passiveAsk - activeAsk) / activeAsk
//   config.json 的 pairKeys 是 BINANCE...|GATEIO... -> active=BINANCE, passive=GATEIO，
//   即 spread = (GATEIO - BINANCE) / BINANCE。
//   正 = GATEIO 更贵。
// ===========================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "signal/SignalGenerator.h"

using namespace pt;

// ---------------------------------------------------------------------------
// 输出小工具
// ---------------------------------------------------------------------------
static const double BP = 1e4;   // 1 = 10000bp，所以 x*BP 就是 bp

static void hr(const char* title = nullptr) {
    std::printf("---------------------------------------------------------------------------\n");
    if (title) std::printf("%s\n", title);
}

static const char* yn(bool b) { return b ? "[可开]" : "[关闭]"; }

// 分位数 -> bp 的字符串
static std::string bps(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%+.3f bp", v * BP);
    return buf;
}

// ---------------------------------------------------------------------------
// 构造一个只带价差统计的 PairInfo
// ---------------------------------------------------------------------------
struct Stat {
    double bbaDQ, bbaUQ;
    double bbbDQ, bbbUQ;
    double sabDQ, sabUQ;
    double saaDQ, saaUQ;
};

static PairInfo MakePairInfo(const Stat& s, double maxVolume = 10.0) {
    PairInfo pi;
    std::strncpy(pi.pairInstrumentKey,
                 "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT",
                 sizeof(pi.pairInstrumentKey) - 1);
    std::strncpy(pi.activeInstrumentKey, "BINANCE.USDT_SWAP.DOGE-USDT",
                 sizeof(pi.activeInstrumentKey) - 1);
    std::strncpy(pi.passiveInstrumentKey, "GATEIO.USDT_SWAP.DOGE-USDT",
                 sizeof(pi.passiveInstrumentKey) - 1);
    pi.activeAccountId  = 10000;
    pi.passiveAccountId = 10001;

    // 报单量：RecalcOrderParams 会把 ±maxVolume 写进 EndVolume
    pi.maxVolume = maxVolume;
    pi.minVolume = 1.0;
    pi.ttTargetVolume = maxVolume;
    pi.mtTargetVolume = maxVolume;

    // 开仓允许、平仓允许、自动模式（autoFlag=true -> TT；false -> MT）
    pi.autoFlag     = true;
    pi.closeFlag    = false;
    pi.stopFlag     = false;
    pi.limitFlag    = false;
    pi.errorFlag    = false;
    pi.profitSwitch = true;
    pi.profitPct    = 0.0001;
    pi.manualFlag   = false;

    // 流动性 / 保证金都正常，否则 RecalcOrderParams 末尾会把开仓开关全关掉
    pi.activeLiquidStatus = pi.passiveLiquidStatus = 0;
    pi.activeMarginStatus = pi.passiveMarginStatus = 0;

    // 资金费率正常（|rate| <= openMaxFundingRate = 0.002）
    pi.rtSpread.activeFundingRate  = 0.0;
    pi.rtSpread.passiveFundingRate = 0.0;
    pi.rtSpread.activeFundingInterval  = 8;
    pi.rtSpread.passiveFundingInterval = 8;

    // 腿参数：calcType=0 表示按量计
    pi.activeParam.calcType  = 0;
    pi.passiveParam.calcType = 0;
    pi.activeParam.multiple  = 1.0;
    pi.passiveParam.multiple = 1.0;

    // 价差统计
    pi.largeStats.valid    = true;
    pi.largeStats.count    = 100000;
    pi.largeStats.bidAskDQ = s.bbaDQ;  pi.largeStats.bidAskUQ = s.bbaUQ;
    pi.largeStats.bidBidDQ = s.bbbDQ;  pi.largeStats.bidBidUQ = s.bbbUQ;
    pi.largeStats.askBidDQ = s.sabDQ;  pi.largeStats.askBidUQ = s.sabUQ;
    pi.largeStats.askAskDQ = s.saaDQ;  pi.largeStats.askAskUQ = s.saaUQ;

    pi.rtSpread.valid = true;
    return pi;
}

// ---------------------------------------------------------------------------
// 打印一次完整的推导结果
// ---------------------------------------------------------------------------
static void Dump(const PairInfo& pi, const FeeSlippageConfig& cfg) {
    const auto& op = pi.orderParams;
    const auto& ls = pi.largeStats;

    const double F_tt = cfg.activeTakerFeeRate + cfg.passiveTakerFeeRate + cfg.basicSlippage;
    const double F_mt = cfg.activeMakerFeeRate + cfg.passiveTakerFeeRate + cfg.basicSlippage;

    hr("1. 执行成本口径（SignalGenerator::CalcExecCost）");
    std::printf("  主动腿挂单/吃单 费率 : %.5f / %.5f\n", cfg.activeMakerFeeRate, cfg.activeTakerFeeRate);
    std::printf("  被动腿挂单/吃单 费率 : %.5f / %.5f\n", cfg.passiveMakerFeeRate, cfg.passiveTakerFeeRate);
    std::printf("  基础滑点 basicSlippage: %.5f\n", cfg.basicSlippage);
    std::printf("  F_tt = 主动吃 + 被动吃 + 滑点 = %.6f  (%s)\n", F_tt, bps(F_tt).c_str());
    std::printf("  F_mt = 主动挂 + 被动吃 + 滑点 = %.6f  (%s)\n", F_mt, bps(F_mt).c_str());
    std::printf("  注：成本是单次往返口径；开一次仓 + 平一次仓要再乘 2 = %s / %s\n",
                bps(2 * F_tt).c_str(), bps(2 * F_mt).c_str());

    hr("2. 输入的分位数（相对价差，正 = GATEIO 比 BINANCE 贵）");
    std::printf("  spreadBidAsk  DQ %-12s UQ %s\n", bps(ls.bidAskDQ).c_str(), bps(ls.bidAskUQ).c_str());
    std::printf("  spreadBidBid  DQ %-12s UQ %s\n", bps(ls.bidBidDQ).c_str(), bps(ls.bidBidUQ).c_str());
    std::printf("  spreadAskBid  DQ %-12s UQ %s\n", bps(ls.askBidDQ).c_str(), bps(ls.askBidUQ).c_str());
    std::printf("  spreadAskAsk  DQ %-12s UQ %s\n", bps(ls.askAskDQ).c_str(), bps(ls.askAskUQ).c_str());

    const double span = ls.bidBidUQ - ls.askAskDQ;
    std::printf("\n  spreadSpan = bidBidUQ - askAskDQ = %s   (门槛 minSpreadSpan = %s) -> %s\n",
                bps(span).c_str(), bps(cfg.minSpreadSpan).c_str(),
                span < cfg.minSpreadSpan
                    ? "*** 低于门槛：RecalcOrderParams 直接关掉全部开仓开关并 return ***"
                    : "通过");

    hr("3. 推导出的 orderParams（由真实 SignalGenerator::RecalcOrderParams 写出）");
    std::printf("  开多 OL             StartSpread     EndSpread     EndVolume\n");
    std::printf("    ttOL  %-14s %-14s %-8.4f  %s\n",
                bps(op.ttOLStartSpread).c_str(), bps(op.ttOLEndSpread).c_str(), op.ttOLEndVolume, yn(op.ttOLSwitch));
    std::printf("    mtOL  %-14s %-14s %-8.4f  %s\n",
                bps(op.mtOLStartSpread).c_str(), bps(op.mtOLEndSpread).c_str(), op.mtOLEndVolume, yn(op.mtOLSwitch));
    std::printf("  开空 OS\n");
    std::printf("    ttOS  %-14s %-14s %-8.4f  %s\n",
                bps(op.ttOSStartSpread).c_str(), bps(op.ttOSEndSpread).c_str(), op.ttOSEndVolume, yn(op.ttOSSwitch));
    std::printf("    mtOS  %-14s %-14s %-8.4f  %s\n",
                bps(op.mtOSStartSpread).c_str(), bps(op.mtOSEndSpread).c_str(), op.mtOSEndVolume, yn(op.mtOSSwitch));
    std::printf("  平多 CL / 平空 CS（恒为 true，由价差回归触发）\n");
    std::printf("    ttCL  %-14s ttCS %-14s\n", bps(op.ttCLStartSpread).c_str(), bps(op.ttCSStartSpread).c_str());
    std::printf("    mtCL  %-14s mtCS %-14s\n", bps(op.mtCLStartSpread).c_str(), bps(op.mtCSStartSpread).c_str());

    hr("4. 开仓门槛：分位数至少要达到多少（由 minSpreadTarget 决定）");
    const double needOL = -(F_mt + cfg.minSpreadTarget) / cfg.spreadAdjPct;
    const double needOS = +(F_mt + cfg.minSpreadTarget) / cfg.spreadAdjPct;
    std::printf("  开多(MT) 需要 askAskDQ < %s\n", bps(needOL).c_str());
    std::printf("  开空(MT) 需要 bidBidUQ > %s\n", bps(needOS).c_str());
    std::printf("  开多(TT) 需要 bidAskDQ < %s\n",
                bps(-(F_tt - cfg.ttAddPercent + cfg.minSpreadTarget) / cfg.spreadAdjPct).c_str());
    std::printf("  开空(TT) 需要 askBidUQ > %s\n",
                bps(+(F_tt - cfg.ttAddPercent + cfg.minSpreadTarget) / cfg.spreadAdjPct).c_str());
    std::printf("\n  实测缺口：\n");
    std::printf("    askAskDQ 实际 %-12s 需要 %-12s 缺口 %s\n",
                bps(ls.askAskDQ).c_str(), bps(needOL).c_str(), bps(needOL - ls.askAskDQ).c_str());
    std::printf("    bidBidUQ 实际 %-12s 需要 %-12s 缺口 %s\n",
                bps(ls.bidBidUQ).c_str(), bps(needOS).c_str(), bps(needOS - ls.bidBidUQ).c_str());
}

// 给定实时价差，看信号会不会打出来
static void DumpSignal(const PairInfo& pi,
                       double sba, double sbb, double sab, double saa) {
    PairInfo t = pi;
    t.rtSpread.spreadBidAsk = sba;
    t.rtSpread.spreadBidBid = sbb;
    t.rtSpread.spreadAskBid = sab;
    t.rtSpread.spreadAskAsk = saa;
    t.rtSpread.valid = true;

    SignalResult r = SignalGenerator::Instance().CheckSignalForSatisfy(t);
    std::printf("  实时 sba %-12s sbb %-12s sab %-12s saa %-12s -> %s\n",
                bps(sba).c_str(), bps(sbb).c_str(), bps(sab).c_str(), bps(saa).c_str(),
                r.hasSignal ? "有信号" : "无信号");
    if (r.hasSignal) {
        std::printf("      ttOL=%d ttOS=%d ttCL=%d ttCS=%d mtOL=%d mtOS=%d mtCL=%d mtCS=%d\n",
                    r.ttOLSignal, r.ttOSSignal, r.ttCLSignal, r.ttCSSignal,
                    r.mtOLSignal, r.mtOSSignal, r.mtCLSignal, r.mtCSSignal);
    }
}

// ---------------------------------------------------------------------------
// sweep：反解要让开仓成立，参数得改成什么
// ---------------------------------------------------------------------------
static void Sweep(const Stat& s) {
    FeeSlippageConfig base = SignalGenerator::Instance().GetConfig();
    const double F_mt = base.activeMakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;
    const double F_tt = base.activeTakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;

    hr("5. 反解：要让开仓开关打开，有哪些旋钮、各需要多少");
    std::printf("  成本口径 F_mt = %s，而分位数量级只有 %s。\n",
                bps(F_mt).c_str(), bps(std::max(std::fabs(s.bbbUQ), std::fabs(s.saaDQ))).c_str());
    std::printf("  也就是说：**成本比整个价差带宽还大**，这是开不了仓的根本原因。\n\n");

    std::printf("  [旋钮 A] spreadAdjPct（当前 %.2f）—— 放大分位数边界\n", base.spreadAdjPct);
    for (double adj : {0.8, 1.0, 1.2, 1.5, 2.0, 3.0, 5.0}) {
        double ol = s.saaDQ * adj + F_mt;
        double os = s.bbbUQ * adj - F_mt;
        const char* verdict = (ol < 0 && os > 0) ? "两边都能开"
                            : (ol < 0 ? "只能开多" : (os > 0 ? "只能开空" : "两边都不行"));
        std::printf("      adj=%.2f -> mtOLStartSpread %-12s mtOSStartSpread %-12s  %s\n",
                    adj, bps(ol).c_str(), bps(os).c_str(), verdict);
    }

    std::printf("\n  [旋钮 B] minSpreadTarget（当前 %.5f）—— 门槛是 startSpread < -minSpreadTarget，\n",
                base.minSpreadTarget);
    std::printf("           所以设成负数就是直接放松。\n");
    for (double mst : {0.0, -0.0002, -0.0005, -0.0008, -0.001, -0.002}) {
        double ol = s.saaDQ * base.spreadAdjPct + F_mt;
        double os = s.bbbUQ * base.spreadAdjPct - F_mt;
        std::printf("      minSpreadTarget=%-9.5f -> canOpenLong %-5s canOpenShort %-5s\n",
                    mst, (ol < -mst) ? "true" : "false", (os > mst) ? "true" : "false");
    }

    std::printf("\n  [旋钮 C] minSpreadSpan（当前 %.5f）—— 只控制是否直接 return，不解决成本门槛\n",
                base.minSpreadSpan);
    std::printf("           当前 spreadSpan = %s\n", bps(s.bbbUQ - s.saaDQ).c_str());

    std::printf("\n  [旋钮 D] 降成本（VIP 费率 / maker 返佣 / 调小 basicSlippage）\n");
    for (double taker : {0.0006, 0.0005, 0.0003, 0.0002, 0.0}) {
        double f = base.activeMakerFeeRate + taker + base.basicSlippage;
        double need = -(f + base.minSpreadTarget) / base.spreadAdjPct;
        std::printf("      被动 taker=%.4f -> F_mt %-12s 开多需要 askAskDQ < %-12s (实际 %s) %s\n",
                    taker, bps(f).c_str(), bps(need).c_str(), bps(s.saaDQ).c_str(),
                    (s.saaDQ < need) ? "OK" : "不够");
    }

    std::printf("\n  [旋钮 E] 放宽分位数（quantileUp/Dn 从 0.92/0.08 调到 0.98/0.02）\n");
    std::printf("           分位数越极端 -> 边界越靠外 -> 阈值越容易满足。\n");
    std::printf("           但同样不产生 edge，只是把开仓点挪到更极端的尾部。\n");

    std::printf("\n  === 结论 ===\n");
    const double absDQ = std::fabs(s.saaDQ);
    if (absDQ < 1e-12) {
        // 下分位为 0 意味着「历史上从没出现过负价差」，|askAskDQ| = 0。
        // 此时 F_mt/|askAskDQ| 是无穷大：spreadAdjPct 再放大也乘不出一个非零的边界，
        // 开多**在任何参数下都不可能成立**。这里显式说明，避免打印 inf。
        std::printf("  下分位 askAskDQ = 0：历史上价差从未跌破 0（即 GATEIO 从未比 BINANCE 便宜）。\n");
        std::printf("  此时 spreadAdjPct 无论调多大，边界都恒为 0，开多**在任何参数下都不可能成立**。\n");
        std::printf("  spreadAdjPct 只能放大一个非零的分位数，放大不了 0 —— 这是「没有 edge」的极端形态。\n");
        std::printf("  结论：必须换对子，或先把成本压到 0 以下（maker 返佣）。\n");
        (void)F_tt;
        return;
    }
    const double needAdj = F_mt / absDQ;
    std::printf("  不动成本口径时，要让开多成立需要 spreadAdjPct >= F_mt/|askAskDQ| = %.6f/%.6f = %.2f\n",
                F_mt, absDQ, needAdj);
    std::printf("  但 spreadAdjPct 只是把「要求的分位数」放大，不产生任何 edge ——\n");
    std::printf("  它等价于承认「我愿意在价差只有 %.3f bp 时就开仓」，而往返成本是 %.3f bp，\n",
                std::fabs(s.saaDQ) * BP, F_mt * BP);
    std::printf("  每做一轮就是净亏 %.3f bp（还没算平仓那一次的成本）。\n", (F_mt - std::fabs(s.saaDQ)) * BP);
    std::printf("  所以正确的做法是：**换价差带宽大于 %.1f bp 的对子**，或者把成本真正降下来。\n", F_mt * BP);
    (void)F_tt;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    Stat s;
    std::string mode = (argc >= 2) ? argv[1] : "demo";

    if (mode == "band" && argc >= 4) {
        double c = std::atof(argv[2]), h = std::atof(argv[3]);
        s = {c - h, c + h, c - h, c + h, c - h, c + h, c - h, c + h};
    } else if (mode == "axes" && argc >= 10) {
        s.bbaDQ = std::atof(argv[2]); s.bbaUQ = std::atof(argv[3]);
        s.bbbDQ = std::atof(argv[4]); s.bbbUQ = std::atof(argv[5]);
        s.sabDQ = std::atof(argv[6]); s.sabUQ = std::atof(argv[7]);
        s.saaDQ = std::atof(argv[8]); s.saaUQ = std::atof(argv[9]);
    } else if (mode == "sweep" && argc >= 4) {
        double c = std::atof(argv[2]), h = std::atof(argv[3]);
        s = {c - h, c + h, c - h, c + h, c - h, c + h, c - h, c + h};
    } else {
        std::printf("用法：\n");
        std::printf("  ./run.sh band <center> <halfwidth>   四个轴围绕 center 波动 ±halfwidth\n");
        std::printf("  ./run.sh axes <8 个分位数>           逐轴指定 DQ/UQ\n");
        std::printf("  ./run.sh sweep <center> <halfwidth>  反解参数\n");
        std::printf("\n下面用内置场景演示：四个轴都围绕 0.0002 波动 ±0.0002\n\n");
        s = {0.0, 0.0004, 0.0, 0.0004, 0.0, 0.0004, 0.0, 0.0004};
    }

    SignalGenerator::Instance().SetConfig(FeeSlippageConfig{});   // 生产默认值
    const FeeSlippageConfig& cfg = SignalGenerator::Instance().GetConfig();

    PairInfo pi = MakePairInfo(s);
    SignalGenerator::Instance().RecalcOrderParams(pi);            // ★ 真实函数

    std::printf("===========================================================================\n");
    std::printf(" 开仓参数计算（链接真实 SignalGenerator.cpp）\n");
    std::printf("===========================================================================\n");
    Dump(pi, cfg);

    hr("5. 信号试算（把价差推到带宽的两个极端，看会不会打信号）");
    std::printf("  价差走到下分位端（价差最负 = GATEIO 最便宜）：\n");
    DumpSignal(pi, s.bbaDQ, s.bbbDQ, s.sabDQ, s.saaDQ);
    std::printf("  价差走到上分位端（价差最正 = GATEIO 最贵）：\n");
    DumpSignal(pi, s.bbaUQ, s.bbbUQ, s.sabUQ, s.saaUQ);

    if (mode == "sweep" || mode == "demo") {
        Sweep(s);
    }
    return 0;
}
