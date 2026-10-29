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
    // limitFlag / errorFlag / manualFlag 已删除：前两个并入 stopFlag / orderParams 的 4 个开关，
    // manualFlag 并入 autoFlag（isManual = !autoFlag）。
    pi.autoFlag     = true;
    pi.closeFlag    = false;
    pi.stopFlag     = false;
    pi.profitSwitch = true;
    pi.profitPct    = 0.0001;

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

    hr("4. 开仓门槛：完整链路（7 道，全过才报单）");
    std::printf("  这一节回答\"配置里的这几个参数到底卡在哪一步\"。①②③⑥ 由配置决定，④⑤⑦ 由运行期状态决定。\n");

    // ---- ⓪ minSpreadSpan：在 RecalcOrderParams 最前面，不满足就直接关掉全部开仓开关并 return ----
    std::printf("\n  ⓪ minSpreadSpan = %s\n", bps(cfg.minSpreadSpan).c_str());
    if (span < cfg.minSpreadSpan) {
        std::printf("     ✗ spreadSpan %s < %s ⇒ 直接 return，下面 ①~⑥ 全部不成立（一单不报）\n",
                    bps(span).c_str(), bps(cfg.minSpreadSpan).c_str());
    } else {
        std::printf("     ✓ spreadSpan %s >= %s ⇒ 继续\n", bps(span).c_str(), bps(cfg.minSpreadSpan).c_str());
    }

    // ---- ① 分位数自然阈值：StartSpread = q × adj ∓ F，触发时裸价差 = q × adj ----
    const double natOL = ls.askAskDQ * cfg.spreadAdjPct + F_mt;   // 开多 StartSpread（未夹）
    const double natOS = ls.bidBidUQ * cfg.spreadAdjPct - F_mt;   // 开空 StartSpread（未夹）
    std::printf("\n  ① 分位数（quantileUp/Dn + spreadAdjPct = %.2f）\n", cfg.spreadAdjPct);
    std::printf("     开多(MT) 裸价差 < %-12s   开空(MT) 裸价差 > %s\n",
                bps(natOL - F_mt).c_str(), bps(natOS + F_mt).c_str());
    std::printf("     注意：这里只用了分位数，与\"此刻价差\"无关；分位数 60s 才重算一次。\n");

    // ---- ② 地板：只有 minSpreadTarget > 0 才夹，且只抬不压 ----
    if (cfg.minSpreadTarget > 0.0) {
        const double floorMT = cfg.minSpreadTarget + 0.000005;
        std::printf("\n  ② 地板 minSpreadTarget = %s ⇒ 夹逼值 ∓%s（TT 再深 %s）\n",
                    bps(cfg.minSpreadTarget).c_str(), bps(floorMT).c_str(), bps(cfg.ttAddPercent).c_str());
        std::printf("     开多 %s  %s → %s ⇒ 毛价差要 < %s\n",
                    (natOL > -floorMT) ? "被夹" : "未夹",
                    bps(natOL).c_str(), bps(op.mtOLStartSpread).c_str(),
                    bps(op.mtOLStartSpread - F_mt).c_str());
        std::printf("     开空 %s  %s → %s ⇒ 毛价差要 > %s\n",
                    (natOS < floorMT) ? "被夹" : "未夹",
                    bps(natOS).c_str(), bps(op.mtOSStartSpread).c_str(),
                    bps(op.mtOSStartSpread + F_mt).c_str());
        std::printf("     只抬不压：分位数比地板更深时不动它。\n");
    } else {
        std::printf("\n  ② 地板 minSpreadTarget = %s ⇒ 不生效，阈值完全由分位数决定\n",
                    bps(cfg.minSpreadTarget).c_str());
        std::printf("     ⚠️ 但 canOpen（③）仍然拿它做门槛 ⇒ 负值 = \"允许在 |值| 的浅价差上开仓\"，不是\"关闭\"。\n");
    }

    // ---- 可达性：门槛回答"如果成交能赚多少"，不回答"会不会成交" ----
    // 地板把入场钉在 -19.05bp 这种深度时，8% 分位可能根本到不了 —— 那就等于永不开仓。
    // needOL/needOS/rL/rS 提到块外：结论那行还要用
    const double needOL = op.mtOLStartSpread - F_mt;   // 开多成交所需裸价差（负）
    const double needOS = op.mtOSStartSpread + F_mt;   // 开空成交所需裸价差（正）
    double rL = 0.0, rS = 0.0;
    {
        auto ratio = [](double need, double q) {
            return std::fabs(q) < 1e-12 ? 0.0 : need / q;   // 两个同号，比值 = 需要几倍分位深度
        };
        rL = ratio(needOL, ls.askAskDQ);
        rS = ratio(needOS, ls.bidBidUQ);
        std::printf("\n     可达性（门槛只管\"如果成交能赚多少\"，不管\"会不会成交\"）：\n");
        std::printf("       开多 门槛 %-12s / 8%%分位 %-12s = %.2f×  %s\n",
                    bps(needOL).c_str(), bps(ls.askAskDQ).c_str(), rL,
                    std::fabs(ls.askAskDQ) < 1e-12 ? "分位为 0 ⇒ 永远碰不到"
                  : (rL <= 1.0 ? "分位内可达" : "分位内碰不到，要更极端尾部"));
        std::printf("       开空 门槛 %-12s / 92%%分位 %-12s = %.2f×  %s\n",
                    bps(needOS).c_str(), bps(ls.bidBidUQ).c_str(), rS,
                    std::fabs(ls.bidBidUQ) < 1e-12 ? "分位为 0 ⇒ 永远碰不到"
                  : (rS <= 1.0 ? "分位内可达" : "分位内碰不到，要更极端尾部"));
        // TT 两个方向：触发判据是 spreadBidAsk + F_tt < ttOLStartSpread / spreadAskBid − F_tt > ttOSStartSpread
        const double needOLtt = op.ttOLStartSpread - F_tt;
        const double needOStt = op.ttOSStartSpread + F_tt;
        const double rLt = ratio(needOLtt, ls.bidAskDQ);
        const double rSt = ratio(needOStt, ls.askBidUQ);
        std::printf("       开多(TT) 门槛 %-12s / 8%%分位 %-12s = %.2f×  %s\n",
                    bps(needOLtt).c_str(), bps(ls.bidAskDQ).c_str(), rLt,
                    std::fabs(ls.bidAskDQ) < 1e-12 ? "分位为 0 ⇒ 永远碰不到"
                  : (rLt <= 1.0 ? "分位内可达" : "分位内碰不到"));
        std::printf("       开空(TT) 门槛 %-12s / 92%%分位 %-12s = %.2f×  %s\n",
                    bps(needOStt).c_str(), bps(ls.askBidUQ).c_str(), rSt,
                    std::fabs(ls.askBidUQ) < 1e-12 ? "分位为 0 ⇒ 永远碰不到"
                  : (rSt <= 1.0 ? "分位内可达" : "分位内碰不到"));
        std::printf("       >1× 时净利再高也没用：开关 on、净利够，但价差永远走不到那个价位。\n");
        std::printf("       TT 比 MT 深 %s = 夹逼多 %s（ttAddPercent）+ 成本多 %s（F_tt−F_mt）。\n",
                    bps(std::fabs(needOLtt - needOL)).c_str(), bps(cfg.ttAddPercent).c_str(),
                    bps(F_tt - F_mt).c_str());
    }

    // ---- ③ canOpen：严格不等号 + closeFlag + autoFlag，四个开仓开关由它派生 ----
    const bool cOL = (op.mtOLStartSpread < -cfg.minSpreadTarget) && !pi.closeFlag;
    const bool cOS = (op.mtOSStartSpread >  cfg.minSpreadTarget) && !pi.closeFlag;
    std::printf("\n  ③ canOpen（严格不等号；closeFlag=%d autoFlag=%d）\n", (int)pi.closeFlag, (int)pi.autoFlag);
    std::printf("     开多 mtOLStartSpread %s %s -minSpreadTarget %s ⇒ %s\n",
                bps(op.mtOLStartSpread).c_str(), cOL ? "<" : ">=",
                bps(-cfg.minSpreadTarget).c_str(), cOL ? "true" : "false");
    std::printf("     开空 mtOSStartSpread %s %s +minSpreadTarget %s ⇒ %s\n",
                bps(op.mtOSStartSpread).c_str(), cOS ? ">" : "<=",
                bps(cfg.minSpreadTarget).c_str(), cOS ? "true" : "false");
    std::printf("     这就是夹逼值要多 5e-6 的原因：夹到正好 -值 时 \"-x < -x\" 为假，4 个开关全关。\n");

    // ---- ④⑤ 运行期状态：本工具按\"正常\"假设，只列出条件 ----
    std::printf("\n  ④ 流动性/保证金（activeLiquidStatus / passiveLiquidStatus / *MarginStatus > 0）\n");
    std::printf("     当前假设全 0 ⇒ ✓ 通过。任一 > 0 ⇒ 4 个开仓开关全压 false。\n");
    std::printf("\n  ⑤ 资金费率 |activeFundingRate| / |passiveFundingRate| <= openMaxFundingRate = %s\n",
                bps(cfg.openMaxFundingRate).c_str());
    std::printf("     当前假设 0 ⇒ ✓ 通过。超了同样把 4 个开仓开关压 false（NaN 当 0）。\n");

    // ---- ⑥ 净利门槛：阈值处的预期往返净利 = 出场裸价差 − 入场裸价差 − 2F ----
    // 用 StartSpread 表达时 F 抵消，但**开多和开空的符号相反**：
    //   开多：入场裸 = OL − F，出场裸 = CL + F ⇒ 净利 = CL − OL
    //   开空：入场裸 = OS + F，出场裸 = CS − F ⇒ 净利 = OS − CS
    // 分位数不对称时两者不相等，所以必须分开算（对称时才会碰巧相同）。
    const double pMTL = op.mtCLStartSpread - op.mtOLStartSpread;
    const double pMTS = op.mtOSStartSpread - op.mtCSStartSpread;
    const double pTTL = op.ttCLStartSpread - op.ttOLStartSpread;
    const double pTTS = op.ttOSStartSpread - op.ttCSStartSpread;
    if (cfg.minProfitTarget > 0.0) {
        const double t = cfg.minProfitTarget;
        std::printf("\n  ⑥ 净利门槛 minProfitTarget = %s ⇒ 阈值处预期净利\n", bps(t).c_str());
        std::printf("     MT 开多 %-12s %-4s   MT 开空 %-12s %-4s\n",
                    bps(pMTL).c_str(), pMTL >= t ? "够" : "不够",
                    bps(pMTS).c_str(), pMTS >= t ? "够" : "不够");
        std::printf("     TT 开多 %-12s %-4s   TT 开空 %-12s %-4s\n",
                    bps(pTTL).c_str(), pTTL >= t ? "够" : "不够",
                    bps(pTTS).c_str(), pTTS >= t ? "够" : "不够");
        if (pMTL < t) {
            std::printf("     不够时两条出路：入场比地板再深 %s，或把 minSpreadTarget 提到 %.6f\n",
                        bps(t - pMTL).c_str(), t + F_mt - 0.000005);
        }
        std::printf("     它只管开仓：平仓 4 个分支一次都不读它；也不保证最终实现（平仓线会漂）。\n");
    } else {
        std::printf("\n  ⑥ 净利门槛 minProfitTarget = %s ⇒ 已关\n", bps(cfg.minProfitTarget).c_str());
    }

    // ---- ⑦ 全局闸门：必须最后压，否则会被上面几段重新打开 ----
    std::printf("\n  ⑦ 只平不开（m_globalPosExceeded / m_closeOnly，ApplyNoOpenGate 最后一步）\n");
    std::printf("     运行期状态，本工具不可见；热改 closeOnly / stopAllAlgo 走的就是这一层。\n");

    // ---- 结论：开关（含地板）+ 净利门槛，两层都过才报单 ----
    const bool okL = (cfg.minProfitTarget <= 0.0) || (pMTL >= cfg.minProfitTarget);
    const bool okS = (cfg.minProfitTarget <= 0.0) || (pMTS >= cfg.minProfitTarget);
    std::printf("\n  ⇒ 按当前分位数：开多 %s   开空 %s   （④⑤⑦ 按\"正常\"假设）\n",
                (op.mtOLSwitch && okL) ? "能报单" : "不能报单",
                (op.mtOSSwitch && okS) ? "能报单" : "不能报单");
    std::printf("     但\"能报单\"只是闸门全开；真要成交还要价差走到：开多 < %s（8%%分位 %s = %.2f×）、"
                "开空 > %s（%.2f×）。\n",
                bps(needOL).c_str(), bps(ls.askAskDQ).c_str(), rL,
                bps(needOS).c_str(), rS);
    std::printf("     × > 1 就是\"配置上永远不开仓\"：所有闸门都是 true，市场却到不了那个价位。\n");

    // ---- 接线核对：声明了但没人读的字段 ----
    hr("5. 参数接线核对（FeeSlippageConfig 里哪些真被读）");
    std::printf("  %-22s %-10s %s\n", "字段", "被读处", "说明");
    std::printf("  %-22s %-10s %s\n", "activeMakerFeeRate", "Recalc", "F_mt 第一项");
    std::printf("  %-22s %-10s %s\n", "activeTakerFeeRate", "Recalc", "F_tt 第一项");
    std::printf("  %-22s %-10s %s\n", "passiveTakerFeeRate", "Recalc", "两者共有");
    std::printf("  %-22s %-10s %s\n", "basicSlippage", "Recalc", "两者共有");
    std::printf("  %-22s %-10s %s\n", "passiveMakerFeeRate", "—", "解析了，但两个 F 都不用它");
    std::printf("  %-22s %-10s %s\n", "ttAddPercent", "Recalc", "TT 阈值额外加深");
    std::printf("  %-22s %-10s %s\n", "spreadAdjPct", "Recalc", "分位数放大系数");
    std::printf("  %-22s %-10s %s\n", "minSpreadSpan", "Recalc", "⓪ 直接 return");
    std::printf("  %-22s %-10s %s\n", "minSpreadTarget", "Recalc", "② 夹逼 + ③ canOpen 门槛");
    std::printf("  %-22s %-10s %s\n", "minProfitTarget", "CheckSignal", "⑥ 只卡开仓");
    std::printf("  %-22s %-10s %s\n", "openMaxFundingRate", "Recalc", "⑤ 资金费率闸门");
    std::printf("  %-22s %-10s %s\n", "slippagePctMinMove", "CalcSlippage", "按最小变价折算滑点");
    std::printf("  %-22s %-10s %s\n", "openProfitPct", "—", "*** 死参数：只有解析和日志，没有逻辑读它 ***");
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
// 用**真实的** RecalcOrderParams 跑一遍变体配置，返回夹逼后的阈值与开关。
// 为什么不自己算：夹逼（只抬不压）、TT 的 ttAddPercent、canOpen 的严格不等号、
// 流动性/资金费率/全局闸门都在函数里，重写一遍必然和源码分叉。
struct Variant {
    double mtOL, mtOS, ttOL, ttOS;
    double mtCL, mtCS, ttCL, ttCS;   // 平仓线：用来算"阈值处预期净利"（= CL − OL，F 抵消）
    bool olOk, osOk, ttOkL, ttOkS;   // 4 个开仓开关（已含 ①②③④⑤⑦ 全部闸门）
};

static Variant RunVariant(const PairInfo& pi, const FeeSlippageConfig& cfg) {
    SignalGenerator::Instance().SetConfig(cfg);
    PairInfo t = pi;
    SignalGenerator::Instance().RecalcOrderParams(t);      // ★ 真实函数
    const auto& op = t.orderParams;
    return Variant{op.mtOLStartSpread, op.mtOSStartSpread,
                   op.ttOLStartSpread, op.ttOSStartSpread,
                   op.mtCLStartSpread, op.mtCSStartSpread,
                   op.ttCLStartSpread, op.ttCSStartSpread,
                   op.mtOLSwitch, op.mtOSSwitch,
                   op.ttOLSwitch, op.ttOSSwitch};
}

static void Sweep(const Stat& s) {
    FeeSlippageConfig base = SignalGenerator::Instance().GetConfig();
    const double F_mt = base.activeMakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;
    const double F_tt = base.activeTakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;

    PairInfo pi = MakePairInfo(s);

    hr("7. 反解：要让开仓开关打开，有哪些旋钮、各需要多少");
    std::printf("  本节每个变体都走**真实 RecalcOrderParams**，所以下面的开关已含 ①②③④⑤⑦ 全部闸门；\n");
    std::printf("  唯一没算的是 ⑥ 净利门槛（它在 CheckSignalForSatisfy 里，不在开关上）。\n");
    std::printf("  成本口径 F_mt = %s，而分位数量级只有 %s。\n",
                bps(F_mt).c_str(), bps(std::max(std::fabs(s.bbbUQ), std::fabs(s.saaDQ))).c_str());
    std::printf("  也就是说：**成本比整个价差带宽还大**，这是开不了仓的根本原因。\n\n");

    std::printf("  ⚠️ 开关 true ≠ 会成交：开关只说明\"允许报单\"，成交还要价差真的走到裸价差门槛。\n");
    std::printf("     所以下面每行都列两件事：开关状态 + 8%%分位端能不能碰到那个门槛。\n\n");

    std::printf("  [旋钮 A] spreadAdjPct（当前 %.2f）—— 放大分位数边界\n", base.spreadAdjPct);
    for (double adj : {0.8, 1.0, 1.2, 1.5, 2.0, 3.0, 5.0}) {
        FeeSlippageConfig c = base; c.spreadAdjPct = adj;
        const Variant v = RunVariant(pi, c);
        const double needOL = v.mtOL - F_mt;      // 开多成交所需裸价差（越小越难）
        const double needOS = v.mtOS + F_mt;      // 开空成交所需裸价差（越大越难）
        const bool reachOL = s.saaDQ <= needOL;   // 8% 分位端是否够深
        const bool reachOS = s.bbbUQ >= needOS;
        std::printf("      adj=%.2f -> 开关[多 %-5s 空 %-5s]  门槛[多 %-11s 空 %-11s]  "
                    "8%%端[多 %-11s 空 %-11s] -> %s\n",
                    adj, v.olOk ? "on" : "off", v.osOk ? "on" : "off",
                    bps(needOL).c_str(), bps(needOS).c_str(),
                    bps(s.saaDQ).c_str(), bps(s.bbbUQ).c_str(),
                    (reachOL && reachOS) ? "两边都能成交"
                  : (reachOL ? "只能开多" : (reachOS ? "只能开空" : "开关可能 on，但两边都碰不到")));
    }
    std::printf("      （旋钮 A 的注：地板生效时分位线再浅也被钉在 12.05bp，所以 adj 改多少门槛都不动；\n");
    std::printf("        adj 只在分位线比地板更深时才有影响，即带宽 B > %.1fbp。）\n",
                2.0 * (base.minSpreadTarget + 0.000005 + F_mt) / base.spreadAdjPct * BP);


    std::printf("\n  [旋钮 B] minSpreadTarget（当前 %.5f）—— 注意它同时是\"夹逼值\"和\"canOpen 门槛\"\n",
                base.minSpreadTarget);
    std::printf("           <= 0 关掉夹逼，但 canOpen 仍用它 ⇒ 负值 = 允许在浅价差上开仓。\n");
    std::printf("           最后一列才是真相：开关 on 但净利 < minProfitTarget，CheckSignal 仍然不报单。\n");
    for (double mst : {0.0, -0.0002, -0.0005, -0.0008, -0.001, -0.002}) {
        FeeSlippageConfig c = base; c.minSpreadTarget = mst;
        const Variant v = RunVariant(pi, c);
        // 阈值处预期净利 = 平仓线 − 开仓线（两边 F 各含一次，相减抵消）—— 就是 ⑥ 的判据
        const double netOL = v.mtCL - v.mtOL;   // 开多：CL − OL
        const double netOS = v.mtOS - v.mtCS;   // 开空：OS − CS（符号相反）
        const bool nOkL = base.minProfitTarget <= 0 || netOL >= base.minProfitTarget;
        const bool nOkS = base.minProfitTarget <= 0 || netOS >= base.minProfitTarget;
        std::printf("      minSpreadTarget=%-9.5f -> 开关[多 %-3s 空 %-3s]  阈值处净利[多 %-11s %s 空 %-11s %s]"
                    "  ⑥门槛 %s\n",
                    mst, v.olOk ? "on" : "off", v.osOk ? "on" : "off",
                    bps(netOL).c_str(), nOkL ? "够" : "不够",
                    bps(netOS).c_str(), nOkS ? "够" : "不够",
                    base.minProfitTarget > 0 ? bps(base.minProfitTarget).c_str() : "off");
    }

    std::printf("\n  [旋钮 C] minSpreadSpan（当前 %.5f）—— 只控制是否直接 return，不解决成本门槛\n",
                base.minSpreadSpan);
    std::printf("           当前 spreadSpan = %s\n", bps(s.bbbUQ - s.saaDQ).c_str());

    std::printf("\n  [旋钮 D] 降成本（VIP 费率 / maker 返佣 / 调小 basicSlippage）\n");
    for (double taker : {0.0006, 0.0005, 0.0003, 0.0002, 0.0}) {
        double f = base.activeMakerFeeRate + taker + base.basicSlippage;
        // 地板生效时，裸价差门槛 = -(地板 + 5e-6 + F)，反解分位数要再除 spreadAdjPct
        double need = -(base.minSpreadTarget + 0.000005 + f) / base.spreadAdjPct;
        std::printf("      被动 taker=%.4f -> F_mt %-12s 开多需要 askAskDQ < %-12s (实际 %s) %s\n",
                    taker, bps(f).c_str(), bps(need).c_str(), bps(s.saaDQ).c_str(),
                    (s.saaDQ < need) ? "OK" : "不够");
    }

    std::printf("\n  [旋钮 E] 放宽分位数（quantileUp/Dn 从 0.92/0.08 调到 0.98/0.02）\n");
    std::printf("           分位数越极端 -> 边界越靠外 -> 阈值越容易满足。\n");
    std::printf("           但同样不产生 edge，只是把开仓点挪到更极端的尾部。\n");

    SignalGenerator::Instance().SetConfig(base);   // 还原，避免影响后续

    std::printf("\n  === 结论 ===\n");
    const double absDQ = std::fabs(s.saaDQ);
    if (absDQ < 1e-12) {
        // 下分位为 0 意味着「历史上从没出现过负价差」，|askAskDQ| = 0。
        // 此时 F_mt/|askAskDQ| 是无穷大：spreadAdjPct 再放大也乘不出一个非零的边界，
        // 开多**在任何参数下都不可能成立**。这里显式说明，避免打印 inf。
        std::printf("  下分位 askAskDQ = 0：历史上价差从未跌破 0（即 GATEIO 从未比 BINANCE 便宜）。\n");
        std::printf("  spreadAdjPct 只能放大一个非零的分位数，放大不了 0 —— 这是「没有 edge」的极端形态。\n");
        std::printf("  ⇒ 开多的开关可能仍是 on（那是**地板**给的，不是分位数给的），\n");
        std::printf("    但成交要求裸价差 < %s，而历史最深只到 %s ⇒ **永远等不到成交**。\n",
                    bps(-(base.minSpreadTarget + 0.000005 + F_mt)).c_str(), bps(s.saaDQ).c_str());
        std::printf("  结论：开关 true 是假象，必须换对子，或先把成本压到 0 以下（maker 返佣）。\n");
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
// 生产口径的 FeeSlippageConfig
//
// ⚠️ 为什么不能用 `FeeSlippageConfig{}`：那个默认构造把 4 个费率全填 0
//    （见 SignalGenerator.h:29-32，真正的值由 etc/config.json 解析进来）。
//    拿 F=0 去算，地板和净利门槛会全部显示"够"，结论完全反了 ——
//    实测成本 14bp 才是所有"开不了仓"问题的根。
//
// 下面这些值逐项抄自 etc/config.json 的 op.feeSlippage 段（2026-10-26）。
// 改了配置记得同步这里；或者用 ./run.sh 的 argv 覆盖（见 usage）。
// ---------------------------------------------------------------------------
static FeeSlippageConfig ProductionConfig() {
    FeeSlippageConfig c;
    c.activeMakerFeeRate  = 0.0002;   // 2bp  —— 主动腿挂单（MT 用）
    c.activeTakerFeeRate  = 0.0005;   // 5bp  —— 主动腿吃单（TT 用）
    c.passiveMakerFeeRate = 0.0002;   // 2bp  —— ⚠️ 未接线，两个 F 都不用它
    c.passiveTakerFeeRate = 0.0005;   // 5bp
    c.basicSlippage       = 0.0;
    c.ttAddPercent        = 0.0003;   // TT 阈值额外加深 3bp
    c.spreadAdjPct        = 0.8;
    c.minSpreadSpan       = 0.0;
    c.minSpreadTarget     = 0.0012;   // 地板 12bp（默认头文件是 0.002，文件覆盖成 0.0012）
    c.minProfitTarget     = 0.0005;   // 5bp
    c.openProfitPct       = 0.0001;   // 死参数，写不写都一样
    c.openMaxFundingRate  = 0.002;
    return c;
}

// ---------------------------------------------------------------------------
// 8. minSpreadTarget 的可行区间
//
// 给定这套分位数，地板填多少才"既不亏、又能成交"？两个约束把它夹住：
//   下限：净利够（minProfitTarget）—— 地板越浅，入场越浅，净利越小
//   上限：8%/92% 分位可达 —— 地板越深，门槛越深，市场越走不到
// 交叉为空 ⇒ 这个对子在当前成本下**做不了**（换对子或降成本），不是参数没调好。
//
// 为什么数值扫描而不解方程：夹逼的"只抬不压"、TT 的 ttAddPercent、canOpen 的严格不等号
// 都在真实函数里，闭式解漏一项就会给出一个"看起来精确、实际做不到"的区间。
// ---------------------------------------------------------------------------
// 中文字符占 2 列显示宽度，而 printf 的 %-Ns 按**字节**补齐 ⇒ 混排时整列会歪。
// 这里按 UTF-8 首字节判断宽度，自己补空格。
static std::string padCol(const std::string& t, int w) {
    int dw = 0;
    for (unsigned char ch : t) {
        if ((ch & 0xC0) == 0x80) continue;   // 续字节不计
        dw += (ch >= 0xE0) ? 2 : 1;
    }
    std::string r = t;
    for (int i = dw; i < w; ++i) r += ' ';
    return r;
}

static const double kScanLo = -0.004;   // 扫描下界 = -40bp
static const double kScanHi =  0.020;   // 扫描上界 = +200bp

static void FeasibleFloor(const Stat& s) {
    FeeSlippageConfig base = SignalGenerator::Instance().GetConfig();
    const double F_mt = base.activeMakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;
    const double F_tt = base.activeTakerFeeRate + base.passiveTakerFeeRate + base.basicSlippage;

    PairInfo pi = MakePairInfo(s);
    const auto& ls = pi.largeStats;

    struct Res {
        bool any{false};
        double lo{0}, hi{0};
        // 无解时给出"最接近可行"的那个点：把两种缺口都折算成 bp 相加，取最小
        double bestMst{0}, bestNetGap{0}, bestReachGap{0};
        double bestDef{1e18};
    };
    Res r[4];   // 0=MT开多 1=MT开空 2=TT开多 3=TT开空
    const char* name[4] = {"MT 开多", "MT 开空", "TT 开多", "TT 开空"};

    const double step = 0.0000025; // 0.025bp（窗口边界常常只有零点几 bp，步长太粗会扫空）
    for (double mst = kScanLo; mst <= kScanHi + 1e-12; mst += step) {
        FeeSlippageConfig c = base;
        c.minSpreadTarget = mst;
        const Variant v = RunVariant(pi, c);

        const bool   ok[4]   = {v.olOk, v.osOk, v.ttOkL, v.ttOkS};
        // 净利 = 出场裸价差 − 入场裸价差 − 2F；用 StartSpread 表达时 F 抵消：
        //   开多：入场裸 = OL − F，出场裸 = CL + F ⇒ 净利 = CL − OL
        //   开空：入场裸 = OS + F，出场裸 = CS − F ⇒ 净利 = OS − CS   ← 符号和开多相反
        const double net[4]  = {v.mtCL - v.mtOL, v.mtOS - v.mtCS,
                                v.ttCL - v.ttOL, v.ttOS - v.ttCS};
        // 成交所需裸价差：开多 = StartSpread − F，开空 = StartSpread + F
        const double need[4] = {v.mtOL - F_mt, v.mtOS + F_mt, v.ttOL - F_tt, v.ttOS + F_tt};
        // 对应的入场轴分位（开多看 8%，开空看 92%）
        const double q[4]    = {ls.askAskDQ, ls.bidBidUQ, ls.bidAskDQ, ls.askBidUQ};
        const bool isLong[4] = {true, false, true, false};

        for (int i = 0; i < 4; ++i) {
            if (!ok[i]) continue;
            // 开多：门槛要比 8% 分位更浅（need >= q，两个都是负数）；开空对称
            const bool reach = isLong[i] ? (need[i] >= q[i]) : (need[i] <= q[i]);
            const double netGap = (base.minProfitTarget > 0.0)
                                ? std::max(0.0, base.minProfitTarget - net[i]) : 0.0;
            const double reachGap = reach ? 0.0
                                 : (isLong[i] ? (q[i] - need[i]) : (need[i] - q[i]));
            const double def = netGap + reachGap;   // 都是 bp，可直接相加
            if (def < r[i].bestDef) {
                r[i].bestDef = def;
                r[i].bestMst = mst;
                r[i].bestNetGap = netGap;
                r[i].bestReachGap = reachGap;
            }
            if (def > 1e-12) continue;   // 1e-12 价格 = 1e-8 bp，纯浮点误差，算"刚好卡住"
            if (!r[i].any) { r[i].any = true; r[i].lo = mst; }
            r[i].hi = mst;
        }
    }

    hr("8. minSpreadTarget 的可行区间（给定这套分位数）");
    std::printf("  逐 0.025bp 扫描，每个点都走真实 RecalcOrderParams。三个条件同时成立才算可行：\n");
    std::printf("     开关 on  ∧  阈值处净利 >= %s  ∧  门槛不比 8%%/92%% 分位更深（能碰到）\n\n",
                base.minProfitTarget > 0 ? bps(base.minProfitTarget).c_str() : "0(关)");
    std::printf("  %s%s%s%s当前 %.5f 的判定\n",
                padCol("方向", 10).c_str(), padCol("下限(净利够)", 14).c_str(),
                padCol("上限(可达)", 12).c_str(), padCol("宽度", 10).c_str(),
                base.minSpreadTarget);

    double pubLo = -1e9, pubHi = 1e9;
    int nAny = 0;
    for (int i = 0; i < 4; ++i) {
        if (!r[i].any) {
            std::printf("  %s%s%s%s✗ 无解（最接近：mst=%s，净利差 %s、可达差 %s）\n",
                        padCol(name[i], 10).c_str(), padCol("-", 14).c_str(),
                        padCol("-", 12).c_str(), padCol("-", 10).c_str(),
                        bps(r[i].bestMst).c_str(),
                        bps(r[i].bestNetGap).c_str(), bps(r[i].bestReachGap).c_str());
            continue;
        }
        ++nAny;
        if (r[i].lo > pubLo) pubLo = r[i].lo;
        if (r[i].hi < pubHi) pubHi = r[i].hi;

        const double cur = base.minSpreadTarget;
        // 扫到下界还可行 ⇒ 下限其实不存在（关掉地板净利也够），别打印成 -40bp
        const bool loOpen = r[i].lo <= kScanLo + 1e-9;
        const bool hiOpen = r[i].hi >= kScanHi - 1e-9;
        std::printf("  %s%s%s%s",
                    padCol(name[i], 10).c_str(),
                    padCol(loOpen ? "无下限" : bps(r[i].lo), 14).c_str(),
                    padCol(hiOpen ? "无上限" : bps(r[i].hi), 12).c_str(),
                    padCol((loOpen || hiOpen) ? "-" : bps(r[i].hi - r[i].lo), 10).c_str());
        if (cur < r[i].lo - 1e-12) {
            std::printf("✗ 低于下限 %s ⇒ 净利不够，报了也不赚\n", bps(r[i].lo - cur).c_str());
        } else if (cur > r[i].hi + 1e-12) {
            std::printf("✗ 高于上限 %s ⇒ 门槛比 8%% 分位还深，永不开仓\n", bps(cur - r[i].hi).c_str());
        } else {
            std::printf("✓ 在区间内\n");
        }
    }

    std::printf("\n  ⇒ ");
    if (nAny == 0) {
        std::printf("四个方向全部无解。这套分位数下**任何** minSpreadTarget 都做不到\n");
        std::printf("     \"净利够 + 能成交\"同时成立 ⇒ 换对子（要更大带宽）或降成本（maker 返佣）。\n");
    } else if (pubLo > pubHi) {
        std::printf("没有公共区间：最紧的那个方向决定上限 %s，最松的下限是 %s。\n",
                    bps(pubHi).c_str(), bps(pubLo).c_str());
        std::printf("     只能按方向取舍 —— 通常 TT 最紧（多 %s 夹逼 + %s 成本）。\n",
                    bps(base.ttAddPercent).c_str(), bps(F_tt - F_mt).c_str());
    } else {
        const bool loOpen = pubLo <= kScanLo + 1e-9;
        std::printf("公共区间：minSpreadTarget ∈ [%s, %s]\n",
                    loOpen ? "-∞" : bps(pubLo).c_str(), bps(pubHi).c_str());
        if (pubHi - pubLo < 1e-9) {
            std::printf("     ⚠️ 宽度为 0：这是**刀刃点**，分位数动一点就掉出去 ⇒ 实际不可用。\n");
        }
        std::printf("     即填 %s 都行%s。当前 %.5f %s。\n",
                    loOpen ? "任何不超过上限的值" : "区间内的值",
                    loOpen ? "（关掉地板也够，地板只是把单笔做大、频率压低）" : "",
                    base.minSpreadTarget,
                    (base.minSpreadTarget >= pubLo - 1e-12 && base.minSpreadTarget <= pubHi + 1e-12)
                        ? "✓ 在区间内" : "✗ 不在区间内");
    }
    std::printf("  区间越窄越说明这套配置在刀刃上：分位数每 60s 重算一次，带宽一缩就可能掉出区间。\n");

    SignalGenerator::Instance().SetConfig(base);   // 还原
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

    SignalGenerator::Instance().SetConfig(ProductionConfig());    // 见上方 ProductionConfig 的说明
    const FeeSlippageConfig& cfg = SignalGenerator::Instance().GetConfig();

    PairInfo pi = MakePairInfo(s);
    SignalGenerator::Instance().RecalcOrderParams(pi);            // ★ 真实函数

    std::printf("===========================================================================\n");
    std::printf(" 开仓参数计算（链接真实 SignalGenerator.cpp）\n");
    std::printf("===========================================================================\n");
    Dump(pi, cfg);

    hr("6. 信号试算（把价差推到带宽的两个极端，看会不会打信号）");
    std::printf("  价差走到下分位端（价差最负 = GATEIO 最便宜）：\n");
    DumpSignal(pi, s.bbaDQ, s.bbbDQ, s.sabDQ, s.saaDQ);
    std::printf("  价差走到上分位端（价差最正 = GATEIO 最贵）：\n");
    DumpSignal(pi, s.bbaUQ, s.bbbUQ, s.sabUQ, s.saaUQ);

    if (mode == "sweep" || mode == "demo") {
        Sweep(s);                           // 第 7 节：旋钮反解
    }
    FeasibleFloor(s);                       // 第 8 节：任何模式都跑，这是"该填多少"的答案
    return 0;
}
