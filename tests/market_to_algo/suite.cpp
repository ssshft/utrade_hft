// =============================================================================
// 行情 -> 算法单 半程测试套件
//
// 覆盖：创建 / 开仓 / 平仓 / 定时更新 / 撤单 / 风控触发 / 重启
//       + 持仓推送 / 资金推送 / 账户总览推送（H / I / J 组，2026-09-30 补）
//
// 全部在进程内模拟：手工构造 dbp::DbpTopic / DbpData / pubsub::Position / Balance /
// TotalAccount 直接调 context 的方法，报单与撤单出口换成回调记账。
// 不读配置文件、不起 dbprocess / tb、不碰共享内存、不联网 —— 一条 ./run.sh 跑完。
//
// 这一套**编译真实的源码**：
//     quant_library/algo/PairTradingContext.cpp
//     quant_library/basic/PairInfoManager.cpp
//     quant_library/signal/SignalGenerator.cpp
//     quant_library/signal/SpreadStatsBuilder.cpp
//     quant_library/risk/RiskManager.cpp
// 只有**执行端**（AlgoContext / BaseAlgoOrder / AlgoPairOrder 的报单与撤单）是桩：
// 那部分已经在 OnCommand 里单独测过，不属于本套件的范围。
//
// PairTradingContext 的目标方法全是 private。这里用 `#define private public`
// 在包含头文件之前打开访问权限 —— 这样测的就是**真实的类、真实的成员状态**，
// 而不是把方法体抄进 .inc 里（那种做法会随源码漂移）。
// 代价：所有 std 头必须在宏生效之前先包含完，否则会破坏标准库内部的访问限定。
// =============================================================================

#include <algorithm>
#include <array>
#include <bitset>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

// ⚠️ `#define private public` 必须在**所有 std 头包含完之后**才生效。
//    原因：项目头（PairTradingContext.h / SignalGenerator.h / SpreadStatsBuilder.h …）
//    会在宏生效期间被解析，它们内部还会 include 若干 std 头；如果那些 std 头此刻才第一次
//    被包含，它们的内部实现就会在 `private` 被改写成 `public` 的情况下解析 ——
//    libc++ 上通常还能过，libstdc++（Ubuntu 服务器用的 g++）更容易炸。
//    所以这里先把 std 头**一次包含足**，让项目头里的 include 全部命中 include guard。
//    上面这份清单是「按需 + 冗余」的：不要求精确，多包含几个没有代价。
#define private public
#include "algo/PairTradingContext.h"
#undef private

using namespace pt;

// ---------------------------------------------------------------------------
// 断言
// ---------------------------------------------------------------------------
static int g_fail = 0;
static int g_pass = 0;
static const char* g_section = "";

static void Section(const char* name) {
    g_section = name;
    std::printf("\n=== %s ===\n", name);
}

static void CHECK(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s   <<< %s\n", what, g_section);
    }
}

static bool Near(double a, double b, double tol = 1e-12) {
    return std::abs(a - b) <= tol;
}

// ---------------------------------------------------------------------------
// 时间常量
// ---------------------------------------------------------------------------
static const int64_t SEC  = 1000000LL;
static const int64_t MIN_ = 60LL * SEC;
static const int64_t HOUR = 60LL * MIN_;
static const int64_t DAY  = 24LL * HOUR;

// SignalGenerator 默认费率下的执行成本（与算法单 takerTakerFs/makerTakerFs 同源）
// F_tt = 0.0006 + 0.0006 + 0.0001 = 0.0013
// F_mt = 0.0002 + 0.0006 + 0.0001 = 0.0009
static const double F_TT = 0.0013;
static const double F_MT = 0.0009;

static const char* kPairKey = "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT";
static const char* kActive = "BINANCE.USDT_SWAP.DOGE-USDT";
static const char* kPassive = "GATEIO.USDT_SWAP.DOGE-USDT";

// ---------------------------------------------------------------------------
// 世界重置：PairInfoManager 是单例，测试之间必须清干净
// （private 已被宏打开，可以直接清内部容器）
// ---------------------------------------------------------------------------
static void ResetWorld() {
    auto& pim = PairInfoManager::Instance();
    pim.m_pairInfoMap.clear();
    pim.m_instrToPairs.clear();
    pim.m_pairKeys.clear();
    pim.smc = nullptr;
}

// ---------------------------------------------------------------------------
// 夹具
// ---------------------------------------------------------------------------
struct Fx {
    PairTradingContext ctx;
    sm::SecurityManager smc;

    std::vector<BaseAlgoOrder*> submits;                                  // 报单回调日志（拥有所有权）
    std::vector<std::pair<int64_t, stra::CommandType>> modifyCalls;       // 撤单/改参回调日志
    std::vector<stra::AlgoOrderModify> modifyPayloads;

    std::string csvPath;

    Fx() = default;
    Fx(const Fx&) = delete;
    Fx& operator=(const Fx&) = delete;
    ~Fx() {
        for (BaseAlgoOrder* o : submits) delete o;
    }

    void Wire() {
        ctx.SetAlgoCommandCallback([this](BaseAlgoOrder* o) {
            submits.push_back(o);
        });
        ctx.SetAlgoOrderModifyCallback([this](int64_t id, stra::CommandType c, const stra::AlgoOrderModify* m) {
            modifyCalls.emplace_back(id, c);
            modifyPayloads.push_back(m ? *m : stra::AlgoOrderModify{});
        });
    }

    PairInfo* pi() {
        return PairInfoManager::Instance().GetPairInfo(kPairKey);
    }

    // 报单次数
    size_t N() const { return submits.size(); }

    // 最后一次回调的 CommandType（无回调返回 CommandType_MIN）
    stra::CommandType LastCmd() const {
        return modifyCalls.empty() ? stra::CommandType_MIN : modifyCalls.back().second;
    }
};

// 注册合约 + Init。csv 路径留空则跳过快照。
static void Boot(Fx& fx, const std::string& csv = "") {
    ResetWorld();
    fx.smc.Clear();
    fx.smc.Set(kActive, md::InstrumentInfo{1.0, 0.0001, 0.1, 0});
    fx.smc.Set(kPassive, md::InstrumentInfo{1.0, 0.0001, 0.1, 0});

    PairTradingConfig cfg;
    cfg.pairKeys = {kPairKey};
    cfg.activeAccountId = 10000;
    cfg.passiveAccountId = 10001;
    cfg.csvStatePath = csv;
    cfg.spreadStatsMinSamples = 3;      // 让少量样本就能建立统计
    cfg.spreadSampleIntervalMs = 0;     // 不降频
    fx.csvPath = csv;

    fx.Wire();
    fx.ctx.Init(cfg, &fx.smc);
}

// 把启动闸门打开（跳过对账），用于只关心信号/风控的用例
static void GoTrading(Fx& fx) {
    fx.ctx.m_phase = PairTradingContext::StartupPhase::Trading;
    fx.ctx.m_startupTimeUs = 1;
}

// ---------------------------------------------------------------------------
// 行情构造
// ---------------------------------------------------------------------------
static dbp::DbpTopic MakeTopic(const std::string& key) {
    dbp::DbpTopic t;
    std::strncpy(t.__name, key.c_str(), sizeof(t.__name) - 1);
    return t;
}

static dbp::DbpData MakeData(double bidAsk, double bidBid, double askBid, double askAsk, int64_t ts) {
    dbp::DbpData d;
    d.spreadBidAsk = bidAsk;
    d.spreadBidBid = bidBid;
    d.spreadAskBid = askBid;
    d.spreadAskAsk = askAsk;
    d.generateTs = ts;
    d.activePriceTema = 100.0;
    d.passivePriceTema = 100.0;
    d.activeBidVolume[0] = 1000.0;
    d.activeAskVolume[0] = 1000.0;
    return d;
}

// ---------------------------------------------------------------------------
// 持仓 / 资金 / 账户总览 推送构造（H/I/J 组用）
//
// 这三类是 pubsub 队列推上来的入站数据，进程内入口分别是
//     PairTradingStrategy::on_position     -> PairTradingContext::OnPosition
//     PairTradingStrategy::on_balance      -> PairTradingContext::OnBalance
//     PairTradingStrategy::on_total_account-> PairTradingContext::OnTotalAccount
// 本套件直接调 context 的方法（策略层那三个转发函数不参与 —— 它们只做转发，
// 且 PairTradingStrategy.cpp 不在本套件的编译单元里）。
// ---------------------------------------------------------------------------
static pubsub::Position MakePosition(ExchangeType ex, InstType it, const char* instId,
                                     Direction dir, double volume,
                                     double avgPrice, double pnl,
                                     double liquidPrice, double markPrice,
                                     double adl, int accountId, bool isLast) {
    pubsub::Position p;
    p.exchangeTypeEnum = ex;
    p.instTypeEnum = it;
    std::strncpy(p.instId, instId, sizeof(p.instId) - 1);
    p.direction = dir;
    p.volume = volume;
    p.avgPrice = avgPrice;
    p.unrealizedPnl = pnl;
    p.liquidPrice = liquidPrice;
    p.markPrice = markPrice;
    p.adlQuantile = adl;
    p.accountId = accountId;
    p.isLast = isLast;
    return p;
}

static pubsub::Balance MakeBalance(ExchangeType ex, InstType it, const char* currency,
                                   double total, double pnl) {
    pubsub::Balance b;
    b.exchangeTypeEnum = ex;
    b.instTypeEnum = it;
    std::strncpy(b.currency, currency, sizeof(b.currency) - 1);
    b.total = total;
    b.unrealizedPnl = pnl;
    return b;
}

// 主动腿（BINANCE）的持仓推送
static pubsub::Position ActivePos(Direction dir, double volume, bool isLast = true) {
    return MakePosition(BINANCE, USDT_SWAP, "DOGE-USDT", dir, volume,
                        0.5, 1.2, 0.3, 0.55, 2.0, 10000, isLast);
}

// 被动腿（GATEIO）的持仓推送
static pubsub::Position PassivePos(Direction dir, double volume, bool isLast = true) {
    return MakePosition(GATEIO, USDT_SWAP, "DOGE-USDT", dir, volume,
                        0.5, 1.2, 0.3, 0.55, 2.0, 10001, isLast);
}

// 只保留某一个开/平仓开关，其余全部关掉，方便把信号轴孤立出来
static void OnlySwitch(PairInfo& pi, const char* which) {
    auto& op = pi.orderParams;
    op.ttOLSwitch = op.ttOSSwitch = op.ttCLSwitch = op.ttCSSwitch = false;
    op.mtOLSwitch = op.mtOSSwitch = op.mtCLSwitch = op.mtCSSwitch = false;
    std::string w(which);
    if (w == "ttOL") op.ttOLSwitch = true;
    if (w == "ttOS") op.ttOSSwitch = true;
    if (w == "ttCL") op.ttCLSwitch = true;
    if (w == "ttCS") op.ttCSSwitch = true;
    if (w == "mtOL") op.mtOLSwitch = true;
    if (w == "mtOS") op.mtOSSwitch = true;
    if (w == "mtCL") op.mtCLSwitch = true;
    if (w == "mtCS") op.mtCSSwitch = true;
}

// 让某一个开仓信号成立：把该轴的阈值放到"必然穿越"的位置
static void ArmOpen(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    std::string w(which);
    if (w == "ttOL") { pi.rtSpread.spreadBidAsk = -1.0; pi.orderParams.ttOLStartSpread = 0.0; pi.orderParams.ttOLEndVolume = -1.0; }
    if (w == "ttOS") { pi.rtSpread.spreadAskBid =  1.0; pi.orderParams.ttOSStartSpread = 0.0; pi.orderParams.ttOSEndVolume =  1.0; }
    if (w == "mtOL") { pi.rtSpread.spreadAskAsk =  0.0; pi.orderParams.mtOLStartSpread = 0.05; pi.orderParams.mtOLEndVolume = -1.0; }
    if (w == "mtOS") { pi.rtSpread.spreadBidBid =  1.0; pi.orderParams.mtOSStartSpread = 0.0;  pi.orderParams.mtOSEndVolume =  1.0; }
}

// 让某一个平仓信号成立（需要已有持仓）
static void ArmClose(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    std::string w(which);
    if (w == "ttCL") { pi.rtSpread.spreadAskBid = 1.0; pi.orderParams.ttCLStartSpread = 0.0; pi.orderParams.ttCLEndVolume = 0.0; }
    if (w == "ttCS") { pi.rtSpread.spreadBidAsk = -1.0; pi.orderParams.ttCSStartSpread = 0.0; pi.orderParams.ttCSEndVolume = 0.0; }
    if (w == "mtCL") { pi.rtSpread.spreadBidBid = 1.0; pi.orderParams.mtCLStartSpread = 0.0; pi.orderParams.mtCLEndVolume = 0.0; }
    if (w == "mtCS") { pi.rtSpread.spreadAskAsk = -1.0; pi.orderParams.mtCSStartSpread = 0.0; pi.orderParams.mtCSEndVolume = 0.0; }
}

// 持仓基准：有量、有目标量、8 个开关都打开（= RecalcOrderParams 之后的正常态），
// 这样 BuildAlgoOrderJson 不会因为"开关关闭且无让利"而返回 nullptr。
// 需要孤立某一条轴的用例会先调用 OnlySwitch / ArmOpen / ArmClose 覆盖它。
static void MakeHoldable(PairInfo& pi, double vol) {
    pi.pairTotalVolume = vol;
    pi.ttTargetVolume = 10.0;
    pi.mtTargetVolume = 10.0;
    pi.maxVolume = 10.0;
    pi.minVolume = 1.0;
    pi.profitSwitch = true;
    pi.profitPct = 0.0001;
    auto& op = pi.orderParams;
    op.ttOLSwitch = op.ttOSSwitch = op.ttCLSwitch = op.ttCSSwitch = true;
    op.mtOLSwitch = op.mtOSSwitch = op.mtCLSwitch = op.mtCSSwitch = true;
    pi.rtSpread.valid = true;
    pi.rtSpread.activePriceTema = 100.0;
    pi.rtSpread.passivePriceTema = 100.0;
    pi.activeParam.multiple = 1.0;
    pi.passiveParam.multiple = 1.0;
}

// ===========================================================================
// A. 创建
// ===========================================================================
static void TestA_Create() {
    Section("A. 创建 (BuildAlgoOrderJson / SubmitAlgoOrder)");

    // A1 身份字段
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        CHECK(o != nullptr, "A1 TT/OL 能建单");
        if (o) {
            CHECK(o->algoType == stra::AlgoType_PairTrading, "A1 algoType = AlgoType_PairTrading");
            CHECK(o->commandType == stra::CommandType_NEW, "A1 commandType = CommandType_NEW");
            CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "A1 algoOrderStatus = ALGO_OS_NEW");
            CHECK(std::string(o->algoStrategyName) == "pair_trading", "A1 algoStrategyName = pair_trading");
            CHECK(std::string(o->pairInstrumentKey) == kPairKey, "A1 pairInstrumentKey 透传");
            CHECK(std::string(o->activeInstrumentKey) == kActive, "A1 activeInstrumentKey 透传");
            CHECK(std::string(o->passiveInstrumentKey) == kPassive, "A1 passiveInstrumentKey 透传");
            CHECK(std::string(o->baseAsset) == "USDT", "A1 baseAsset = USDT");
            CHECK(o->activeAccountId == 10000 && o->passiveAccountId == 10001, "A1 两腿账户透传");
            CHECK(o->insertTime > 0 && o->updateTime == o->insertTime, "A1 insertTime/updateTime 一致且非 0");
            CHECK(o->algoOrderId > 0, "A1 algoOrderId 已生成");
            delete o;
        }
    }

    // A2 TT/MT 主动腿报单类型
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* tt = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        BaseAlgoOrder* mt = fx.ctx.BuildAlgoOrderJson(pi, "MT", "OL", 0.0);
        CHECK(tt && tt->activeOrderType == OT_MARKET, "A2 TT 主动腿 = OT_MARKET（吃单）");
        CHECK(mt && mt->activeOrderType == OT_LIMIT,  "A2 MT 主动腿 = OT_LIMIT（挂单）");
        CHECK(tt && tt->passiveOrderType == OT_MARKET, "A2 被动腿恒 = OT_MARKET");
        CHECK(mt && mt->passiveOrderType == OT_MARKET, "A2 被动腿恒 = OT_MARKET (MT)");
        delete tt; delete mt;
    }

    // A3 驱动类型
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        CHECK(o && o->activeDriveType == stra::DriveType_ACTIVE,  "A3 activeDriveType = ACTIVE");
        CHECK(o && o->passiveDriveType == stra::DriveType_PASSIVE, "A3 passiveDriveType = PASSIVE");
        delete o;
    }

    // A4 费率/滑点与信号侧同源
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        const auto& sg = SignalGenerator::Instance();
        const auto& fs = sg.GetConfig();
        CHECK(o && Near(o->takerTakerFs, sg.CalcExecCost(true)),  "A4 takerTakerFs == CalcExecCost(true)（同源）");
        CHECK(o && Near(o->makerTakerFs, sg.CalcExecCost(false)), "A4 makerTakerFs == CalcExecCost(false)（同源）");
        CHECK(o && Near(o->takerTakerFs, F_TT) && Near(o->makerTakerFs, F_MT), "A4 成本口径 = 0.0013 / 0.0009");
        CHECK(o && Near(o->activeMakerFeeRate, fs.activeMakerFeeRate), "A4 主动 maker 费率同源");
        CHECK(o && Near(o->activeTakerFeeRate, fs.activeTakerFeeRate), "A4 主动 taker 费率同源");
        CHECK(o && Near(o->passiveMakerFeeRate, fs.passiveMakerFeeRate), "A4 被动 maker 费率同源");
        CHECK(o && Near(o->passiveTakerFeeRate, fs.passiveTakerFeeRate), "A4 被动 taker 费率同源");
        CHECK(o && Near(o->activeMakerSlippage, fs.basicSlippage), "A4 主动滑点 = basicSlippage");
        CHECK(o && Near(o->activeTakerSlippage, fs.basicSlippage), "A4 主动滑点只计一次");
        CHECK(o && Near(o->passiveMakerSlippage, 0.0) && Near(o->passiveTakerSlippage, 0.0),
              "A4 被动腿滑点 = 0（不重复计）");
        delete o;
    }

    // A5 量与价透传
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -3.5);
        pi.pairActiveTotalPrice = 0.1234;
        pi.pairPassiveTotalPrice = 0.1235;
        pi.pairPassiveTotalVolume = 3.5;
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0);
        CHECK(o && Near(o->pairTotalVolume, -3.5), "A5 pairTotalVolume 透传");
        CHECK(o && Near(o->pairActiveTotalPrice, 0.1234), "A5 pairActiveTotalPrice 透传");
        CHECK(o && Near(o->pairPassiveTotalPrice, 0.1235), "A5 pairPassiveTotalPrice 透传");
        CHECK(o && Near(o->pairPassiveTotalVolume, 3.5), "A5 pairPassiveTotalVolume 透传");
        CHECK(o && Near(o->ttTargetVolume, 10.0) && Near(o->mtTargetVolume, 10.0), "A5 目标量透传");
        CHECK(o && Near(o->minVolume, 1.0), "A5 minVolume 透传");
        delete o;
    }

    // A6 开关门槛
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.orderParams.ttOLSwitch = false;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0) == nullptr,
              "A6 开仓开关关闭 + 无让利 -> 建单失败(nullptr)");
        BaseAlgoOrder* risk = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.00002);
        CHECK(risk != nullptr, "A6 开仓开关关闭但带让利（风控强平）-> 仍能建单");
        delete risk;

        pi.orderParams.ttCLSwitch = false;
        BaseAlgoOrder* cl = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0);
        CHECK(cl != nullptr, "A6 平仓开关关闭 -> 平仓不受开关限制，仍能建单");
        delete cl;
    }

    // A7 报单量校验
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.ttTargetVolume = 0.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0) == nullptr, "A7 targetVolume = 0 -> nullptr");
        pi.ttTargetVolume = std::nan("");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0) == nullptr, "A7 targetVolume = NaN -> nullptr");
        pi.ttTargetVolume = -1.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0) == nullptr, "A7 targetVolume < 0 -> nullptr");
        pi.ttTargetVolume = 10.0;
        pi.mtTargetVolume = 0.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "MT", "OL", 0.0) == nullptr, "A7 MT 走 mtTargetVolume 校验");
    }

    // A8 非法模式/方向
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "XX", "OL", 0.0) == nullptr, "A8 非法 algoMode -> nullptr");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "ZZ", 0.0) == nullptr, "A8 非法 direction -> nullptr");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "", "", 0.0) == nullptr, "A8 空模式 -> nullptr（不崩）");
    }

    // A9 平仓单清掉四个开仓开关
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& op = pi.orderParams;
        op.ttOLSwitch = op.ttOSSwitch = op.mtOLSwitch = op.mtOSSwitch = true;
        op.ttCLSwitch = op.ttCSSwitch = op.mtCLSwitch = op.mtCSSwitch = true;
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0);
        CHECK(o && !o->ttOLSwitch && !o->ttOSSwitch && !o->mtOLSwitch && !o->mtOSSwitch,
              "A9 平仓单：四个开仓开关全部清零");
        CHECK(o && o->ttCLSwitch, "A9 平仓单：本次触发的平仓开关被强制打开");
        CHECK(o && o->mtCLSwitch && o->mtCSSwitch, "A9 平仓单：不动其它平仓开关");
        delete o;
    }

    // A10 风控让利把触发价差往更容易成交的方向挪
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.orderParams.ttCLStartSpread = 0.0010;
        pi.orderParams.ttCLEndSpread   = 0.0011;
        const double shift = 0.00002;
        BaseAlgoOrder* cl = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", shift);
        CHECK(cl && Near(cl->ttCLStartSpread, 0.0010 - shift), "A10 CL 让利：StartSpread 下移 shift");
        CHECK(cl && Near(cl->ttCLEndSpread, 0.0011 - shift),   "A10 CL 让利：EndSpread 下移 shift");
        delete cl;

        pi.orderParams.ttCSStartSpread = -0.0010;
        pi.orderParams.ttCSEndSpread   = -0.0011;
        BaseAlgoOrder* cs = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CS", shift);
        CHECK(cs && Near(cs->ttCSStartSpread, -0.0010 + shift), "A10 CS 让利：StartSpread 上移 shift");
        CHECK(cs && Near(cs->ttCSEndSpread, -0.0011 + shift),   "A10 CS 让利：EndSpread 上移 shift");
        delete cs;
    }

    // A11 32 个触发参数整份拷贝
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        auto& op = pi.orderParams;
        op.ttOLStartSpread = 0.011; op.ttOLEndSpread = 0.012;
        op.ttOLStartVolume = 0.013; op.ttOLEndVolume = 0.014;
        op.mtCSStartSpread = -0.021; op.mtCSEndSpread = -0.022;
        op.mtCSStartVolume = -0.023; op.mtCSEndVolume = -0.024;
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        CHECK(o && Near(o->ttOLStartSpread, 0.011) && Near(o->ttOLEndSpread, 0.012), "A11 ttOL 起止价差拷贝");
        CHECK(o && Near(o->ttOLStartVolume, 0.013) && Near(o->ttOLEndVolume, 0.014), "A11 ttOL 起止量拷贝");
        CHECK(o && Near(o->mtCSStartSpread, -0.021) && Near(o->mtCSEndSpread, -0.022), "A11 mtCS 起止价差拷贝");
        CHECK(o && Near(o->mtCSStartVolume, -0.023) && Near(o->mtCSEndVolume, -0.024), "A11 mtCS 起止量拷贝");
        CHECK(o && o->ttOLSwitch == true, "A11 本次触发的开关被强制打开");
        delete o;
    }

    // A12 固定参数块
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.manualFlag = true;
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0);
        CHECK(o && Near(o->passiveVolumePct, 0.5), "A12 passiveVolumePct = 0.5");
        CHECK(o && o->activeMakerCancelOrderTime == 5LL * 1000 * 1000, "A12 主动 maker 撤单时间 = 5s(us)");
        CHECK(o && o->activeTakerCancelOrderTime == 5LL * 1000 * 1000, "A12 主动 taker 撤单时间 = 5s(us)");
        CHECK(o && o->passiveMakerCancelOrderTime == 5LL * 1000 * 1000, "A12 被动 maker 撤单时间 = 5s(us)");
        CHECK(o && o->passiveTakerCancelOrderTime == 5LL * 1000 * 1000, "A12 被动 taker 撤单时间 = 5s(us)");
        CHECK(o && Near(o->activePassiveCancelOrderPct, 0.001) && Near(o->activeMakerCancelOrderPct, 0.001) &&
                   Near(o->activeTakerCancelOrderPct, 0.001) && Near(o->passiveMakerCancelOrderPct, 0.001) &&
                   Near(o->passiveTakerCancelOrderPct, 0.001), "A12 五个撤单比例 = 0.001");
        CHECK(o && !o->activeDepthMakerCheck && !o->activeDepthTakerCheck &&
                   !o->passiveDepthMakerCheck && !o->passiveDepthTakerCheck, "A12 四个深度检查 = false");
        CHECK(o && Near(o->activePriceTakerPct, 0.0) && Near(o->activePriceMakerPct, 0.0) &&
                   Near(o->passivePriceTakerPct, 0.0) && Near(o->passivePriceMakerPct, 0.0),
              "A12 四个价格比例 = 0");
        CHECK(o && o->mtRebalanceSwitch && o->ttRebalanceSwitch && o->mtRebalanceFlag && o->ttRebalanceFlag,
              "A12 rebalance 开关/标志 = true");
        CHECK(o && !o->mtPriceTrendProtectFlag && !o->ttPriceTrendProtectFlag, "A12 价格趋势保护 = false");
        CHECK(o && Near(o->maxTTOrderSize, 1.0) && Near(o->maxMTOrderSize, 1.0), "A12 max{TT,MT}OrderSize = 1.0");
        CHECK(o && o->targetSpreadType == stra::TargetSpredPrice_NOW, "A12 targetSpreadType = NOW");
        CHECK(o && o->activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct,
              "A12 activeVolumeCalcualteType = PassiveVolumePct");
        CHECK(o && o->isManual == true, "A12 isManual 跟随 pi.manualFlag");
        delete o;
    }

    // A13 SubmitAlgoOrder 的副作用
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0);
        CHECK(fx.N() == 1, "A13 报单回调被调用一次");
        CHECK(pi.hasActiveAlgoOrder, "A13 hasActiveAlgoOrder = true");
        CHECK(fx.N() == 1 && std::to_string(fx.submits[0]->algoOrderId) == std::string(pi.currentAlgoOrderId),
              "A13 currentAlgoOrderId == to_string(算法单 algoOrderId)");
        CHECK(pi.satisfyTime > 0 && pi.algoModifyTime == pi.satisfyTime,
              "A13 satisfyTime / algoModifyTime 一起刷新且相等");
    }

    // A14 建单失败 -> 不占对子、不回调
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.orderParams.ttOLSwitch = false;      // 建单会失败
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0);
        CHECK(fx.N() == 0, "A14 建单失败 -> 回调不触发");
        CHECK(!pi.hasActiveAlgoOrder, "A14 建单失败 -> 不占用对子");
    }

    // A15 无回调时安全返回
    {
        Fx fx; Boot(fx); GoTrading(fx);
        fx.ctx.SetAlgoCommandCallback(nullptr);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0);
        CHECK(fx.N() == 0 && !pi.hasActiveAlgoOrder, "A15 未注册回调 -> 直接返回，不占对子");
    }

    // A16 ID 生成
    {
        int64_t a = PairTradingContext::GenerateAlgoOrderId();
        int64_t b = PairTradingContext::GenerateAlgoOrderId();
        CHECK(a > 0 && b > 0 && a != b, "A16 GenerateAlgoOrderId 唯一且为正");
        bool numeric = true;
        for (char c : std::to_string(a)) if (c < '0' || c > '9') numeric = false;
        CHECK(numeric, "A16 ID 是纯数字（ScanFinishedAlgoOrders 要 stoll 还原）");
    }
}

// ===========================================================================
// B. 开仓
// ===========================================================================
static void TestB_Open() {
    Section("B. 开仓 (ProcessPairSignal -> SubmitAlgoOrder)");

    struct Case { const char* sw; const char* mode; const char* dir; };
    const Case cases[] = {
        {"ttOL", "TT", "OL"}, {"ttOS", "TT", "OS"},
        {"mtOL", "MT", "OL"}, {"mtOS", "MT", "OS"},
    };

    for (const Case& c : cases) {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, c.sw, 0.0);
        fx.ctx.ProcessPairSignal(pi);
        bool ok = (fx.N() == 1);
        CHECK(ok, (std::string("B ") + c.sw + " 信号 -> 报出一单").c_str());
        if (ok) {
            const auto* o = fx.submits[0];
            CHECK(o->activeOrderType == (std::string(c.mode) == "TT" ? OT_MARKET : OT_LIMIT),
                  (std::string("B ") + c.sw + " 主动腿类型跟随 TT/MT").c_str());
        }
    }

    // B5 平仓优先于开仓
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& op = pi.orderParams;
        op.ttOLSwitch = op.ttOSSwitch = op.ttCLSwitch = op.ttCSSwitch = false;
        op.mtOLSwitch = op.mtOSSwitch = op.mtCLSwitch = op.mtCSSwitch = false;
        // 同时让 TT 平多 与 TT 开多 成立
        op.ttCLSwitch = true; op.ttOLSwitch = true;
        pi.rtSpread.spreadAskBid = 1.0;  op.ttCLStartSpread = 0.0; op.ttCLEndVolume = 0.0;
        pi.rtSpread.spreadBidAsk = -1.0; op.ttOLStartSpread = 0.0; op.ttOLEndVolume = -1.0;
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 1, "B5 同时有平仓与开仓信号 -> 只报一单");
        if (fx.N() == 1) {
            const BaseAlgoOrder* o = fx.submits[0];
            CHECK(!o->ttOLSwitch && !o->ttOSSwitch && !o->mtOLSwitch && !o->mtOSSwitch,
                  "B5 平仓优先：报出的是平仓单（四个开仓开关被清）");
            CHECK(o->ttCLSwitch, "B5 平仓优先：本次触发的平仓开关被打开");
        }
    }

    // B6 canOpen = false 时不开仓
    {
        const char* blockers[] = {"stopFlag", "closeFlag", "limitFlag", "errorFlag"};
        for (const char* b : blockers) {
            Fx fx; Boot(fx); GoTrading(fx);
            PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
            ArmOpen(pi, "ttOL", 0.0);
            std::string bs(b);
            if (bs == "stopFlag") pi.stopFlag = true;
            if (bs == "closeFlag") pi.closeFlag = true;
            if (bs == "limitFlag") pi.limitFlag = true;
            if (bs == "errorFlag") pi.errorFlag = true;
            fx.ctx.ProcessPairSignal(pi);
            CHECK(fx.N() == 0, (std::string("B6 ") + b + " -> 不报单").c_str());
        }
    }

    // B7 satisfyTime 在开仓机会成立时被刷新
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.satisfyTime = 0;
        ArmOpen(pi, "ttOL", 0.0);
        fx.ctx.ProcessPairSignal(pi);
        CHECK(pi.satisfyTime > 0, "B7 开仓机会成立 -> satisfyTime 被刷新");
    }

    // B8 已有算法单 -> 不重复报
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, "ttOL", 0.0);
        fx.ctx.ProcessPairSignal(pi);
        const size_t first = fx.N();
        fx.ctx.ProcessPairSignal(pi);
        CHECK(first == 1 && fx.N() == 1, "B8 hasActiveAlgoOrder -> 第二个 tick 不重复报单");
    }

    // B9 errorFlag 阻断派发（平仓侧）
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        ArmClose(pi, "ttCL", -1.0);
        pi.errorFlag = true;
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "B9 errorFlag -> 平仓也不派发（留人工处理）");
    }

    // B10 启动闸门：未放行时一切信号都不派发
    {
        Fx fx; Boot(fx);            // 不调用 GoTrading -> 停在 Reconciling
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, "ttOL", 0.0);
        CHECK(!fx.ctx.IsTradingReady(), "B10 Init 后停在 Reconciling");
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "B10 闸门未开 -> 有信号也不报单");
    }
}

// ===========================================================================
// C. 平仓
// ===========================================================================
static void TestC_Close() {
    Section("C. 平仓");

    struct Case { const char* sw; double vol; };
    const Case cases[] = {{"ttCL", -1.0}, {"ttCS", 1.0}, {"mtCL", -1.0}, {"mtCS", 1.0}};

    for (const Case& c : cases) {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, c.vol);
        ArmClose(pi, c.sw, c.vol);
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 1, (std::string("C ") + c.sw + " 信号 -> 报出一单").c_str());
        if (fx.N() == 1) {
            CHECK(!fx.submits[0]->ttOLSwitch && !fx.submits[0]->ttOSSwitch &&
                  !fx.submits[0]->mtOLSwitch && !fx.submits[0]->mtOSSwitch,
                  (std::string("C ") + c.sw + " 平仓单清掉四个开仓开关").c_str());
        }
    }

    // C5 无持仓 -> CanClose 为 false -> 平仓信号不派发
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);   // 空仓
        ArmClose(pi, "ttCL", 0.0);
        // ttCL 还要求 IsLong()，空仓时该分支本身不成立；这里额外确认 CanClose 的门槛
        std::string reason;
        CHECK(!SignalGenerator::Instance().CanClose(pi, reason), "C5 空仓 -> CanClose = false");
        CHECK(reason == "no position", "C5 CanClose 给出的理由 = no position");
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "C5 空仓 -> 平仓不派发");
    }

    // C6 平仓优先级 TT_CL > TT_CS > MT_CL > MT_CS
    {
        // 让 TT_CL 与 MT_CL 同时成立 -> 应选 TT_CL
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& op = pi.orderParams;
        op.ttOLSwitch = op.ttOSSwitch = op.mtOLSwitch = op.mtOSSwitch = false;
        op.ttCLSwitch = op.ttCSSwitch = op.mtCLSwitch = op.mtCSSwitch = true;
        pi.rtSpread.spreadAskBid = 1.0; op.ttCLStartSpread = 0.0; op.ttCLEndVolume = 0.0;
        pi.rtSpread.spreadBidBid = 1.0; op.mtCLStartSpread = 0.0; op.mtCLEndVolume = 0.0;
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 1, "C6 两个平仓信号 -> 只报一单");
        // TT_CL 的主动腿是 MARKET，MT_CL 是 LIMIT —— 用这个区分走了哪条路
        CHECK(fx.N() == 1 && fx.submits[0]->activeOrderType == OT_MARKET,
              "C6 优先走 TT_CL（主动腿吃单）");
    }

    // C8 CloseSpreadReached 的四个轴
    {
        auto& sg = SignalGenerator::Instance();
        PairInfo pi; MakeHoldable(pi, -1.0);
        pi.rtSpread.spreadAskBid = 1.0; pi.orderParams.ttCLStartSpread = 0.5;
        CHECK(sg.CloseSpreadReached(pi), "C8 多头：spreadAskBid - F_tt > ttCLStartSpread -> 已回归");
        PairInfo pj; MakeHoldable(pj, -1.0);
        pj.rtSpread.spreadBidBid = 1.0; pj.orderParams.mtCLStartSpread = 0.5;
        CHECK(sg.CloseSpreadReached(pj), "C8 多头：spreadBidBid - F_mt > mtCLStartSpread -> 已回归");
        PairInfo pk; MakeHoldable(pk, 1.0);
        pk.rtSpread.spreadBidAsk = -1.0; pk.orderParams.ttCSStartSpread = -0.5;
        CHECK(sg.CloseSpreadReached(pk), "C8 空头：spreadBidAsk + F_tt < ttCSStartSpread -> 已回归");
        PairInfo pl; MakeHoldable(pl, 1.0);
        pl.rtSpread.spreadAskAsk = -1.0; pl.orderParams.mtCSStartSpread = -0.5;
        CHECK(sg.CloseSpreadReached(pl), "C8 空头：spreadAskAsk + F_mt < mtCSStartSpread -> 已回归");
        PairInfo pm; MakeHoldable(pm, -1.0);
        pm.rtSpread.spreadAskBid = 10.0; pm.orderParams.ttCLStartSpread = 10.0;
        CHECK(!sg.CloseSpreadReached(pm), "C8 多头：未穿越 -> 未回归");
        PairInfo pn; MakeHoldable(pn, 0.0);
        CHECK(!sg.CloseSpreadReached(pn), "C8 空仓 -> 未回归（HasPosition 门槛）");
        PairInfo po; MakeHoldable(po, -1.0);
        po.rtSpread.valid = false;
        CHECK(!sg.CloseSpreadReached(po), "C8 行情无效 -> 未回归");
    }
}

// ===========================================================================
// D. 定时更新
// ===========================================================================
static void TestD_Timer() {
    Section("D. 定时更新 (OnTimer)");

    // D1 闸门未开 -> OnTimer 全跳过
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        ArmOpen(pi, "ttOL", -1.0);
        pi.rtSpread.spreadAskBid = 0.0;      // 制造一个平仓风险场景
        pi.rtSpread.spreadAskBid = 0.0;
        fx.ctx.OnTimer(1000 * SEC);
        CHECK(!fx.ctx.IsTradingReady(), "D1 对账未完成 -> 仍冻结");
        CHECK(fx.N() == 0, "D1 冻结期间不报单");
        CHECK(fx.ctx.m_lastVolumeRecalcUs == 0, "D1 冻结期间不做量参数重算");
        CHECK(fx.ctx.m_lastCsvSaveUs == 0, "D1 冻结期间不落快照");
    }

    // D2 闸门放行
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.activePushArrived = true; pi.passivePushArrived = true;
        pi.activeRealPosition = -1.0;
        const int64_t t = 1000 * SEC;
        fx.ctx.OnTimer(t);
        CHECK(fx.ctx.IsTradingReady(), "D2 两腿推送到位 -> 闸门打开");
        CHECK(fx.ctx.m_lastVolumeRecalcUs == t, "D2 放行的同一轮就做量参数重算");
    }

    // D3 量参数重算周期
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        const int64_t t0 = 1000 * SEC;
        fx.ctx.OnTimer(t0);
        const int64_t after = fx.ctx.m_lastVolumeRecalcUs;
        fx.ctx.OnTimer(t0 + 30 * SEC);                      // 周期 60s，未到
        CHECK(fx.ctx.m_lastVolumeRecalcUs == after, "D3 未到 volumeRecalcIntervalSec -> 不重算");
        fx.ctx.OnTimer(t0 + 61 * SEC);
        CHECK(fx.ctx.m_lastVolumeRecalcUs == t0 + 61 * SEC, "D3 到点 -> 重算一次并刷新计时");
    }

    // D4 价差统计刷新 + 立刻重算 orderParams
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.orderParams.ttOLStartSpread = 12345.0;           // 哨兵
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        for (int i = 0; i < 5; ++i) {
            dbp::DbpData d = MakeData(0.001 * i, 0.002 * i, 0.003 * i, -0.001 * i, 1000 * SEC + i * SEC);
            fx.ctx.OnSpread(&topic, &d);
        }
        const int64_t t0 = 1000 * SEC + 10 * SEC;
        fx.ctx.OnTimer(t0);
        CHECK(pi.largeStats.IsValid(), "D4 样本足够 -> largeStats 建立");
        CHECK(pi.orderParams.ttOLStartSpread != 12345.0, "D4 统计一更新就重算 orderParams（哨兵被覆盖）");
        const double snapshot = pi.orderParams.ttOLStartSpread;
        fx.ctx.OnTimer(t0 + 10 * SEC);
        CHECK(Near(pi.orderParams.ttOLStartSpread, snapshot), "D4 未到统计周期 -> orderParams 不再变");
    }

    // D5 风控每个 tick 都跑（无持仓时也要跑，用来清计时）
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.positionStartTime = 12345;                       // 陈旧的持仓计时
        pi.spreadNoRegressionStartTime = 54321;
        fx.ctx.OnTimer(1000 * SEC);
        CHECK(pi.positionStartTime == 0, "D5 无持仓 -> 风控每个 tick 都跑，清掉持仓计时");
        CHECK(pi.spreadNoRegressionStartTime == 0, "D5 无持仓 -> 清掉未回归计时");
    }

    // D6/D7/D8 改参
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 999999LL);
        pi.algoModifyTime = 1000 * SEC;

        // D6 未到周期 -> 不改参
        fx.ctx.ProcessModify(pi, 1000 * SEC + 30 * SEC);
        CHECK(fx.modifyCalls.empty(), "D6 未到 modifyTimespanSec -> 不改参");

        // D7 到周期 -> 普通改参
        pi.orderParams.ttOLSwitch = true;
        pi.orderParams.ttCLSwitch = true;
        pi.orderParams.mtCLSwitch = true;
        pi.profitSwitch = true;
        fx.ctx.ProcessModify(pi, 1000 * SEC + 61 * SEC);
        CHECK(fx.modifyCalls.size() == 1, "D7 到周期 -> 触发一次改参");
        CHECK(fx.LastCmd() == stra::CommandType_MODIFY, "D7 回调命令 = CommandType_MODIFY");
        CHECK(fx.modifyCalls.size() == 1 && fx.modifyCalls[0].first == 999999LL, "D7 回调用 algoOrderId 指回算法单");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads[0];
            CHECK(m.ttOLSwitch && m.ttCLSwitch && m.mtCLSwitch, "D7 普通改参：8 个开关整份带上（含平仓开关）");
            CHECK(m.profitSwitch, "D7 普通改参：profitSwitch 跟随 pair_info");
        }
        CHECK(pi.algoModifyTime == 1000 * SEC + 61 * SEC, "D7 改参后刷新 algoModifyTime");

        // D7b 流动性危险 -> 更激进的平仓改参
        fx.modifyCalls.clear(); fx.modifyPayloads.clear();
        pi.activeLiquidStatus = 2;
        pi.algoModifyTime = 1000 * SEC;
        fx.ctx.ProcessModify(pi, 1000 * SEC + 61 * SEC);
        CHECK(fx.modifyCalls.size() == 1, "D7b 流动性危险 -> 仍触发改参");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads[0];
            CHECK(!m.profitSwitch && Near(m.profitPct, 0.0), "D7b 激进平仓：关掉盈利保护");
            CHECK(!m.ttOLSwitch && !m.ttOSSwitch && !m.mtOLSwitch && !m.mtOSSwitch, "D7b 激进平仓：关掉全部开仓开关");
            CHECK(!m.ttCLSwitch && !m.ttCSSwitch && m.mtCLSwitch && m.mtCSSwitch, "D7b 激进平仓：只留 MT 平仓");
        }

        // D8 stopFlag/closeFlag -> 压掉开仓开关
        fx.modifyCalls.clear(); fx.modifyPayloads.clear();
        pi.activeLiquidStatus = 0;
        pi.stopFlag = true;
        pi.algoModifyTime = 1000 * SEC;
        fx.ctx.ProcessModify(pi, 1000 * SEC + 61 * SEC);
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads[0];
            CHECK(!m.ttOLSwitch && !m.ttOSSwitch && !m.mtOLSwitch && !m.mtOSSwitch,
                  "D8 stopFlag -> 改参时压掉四个开仓开关");
        }
    }

    // D9 没有活跃算法单 -> 不改参
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = false;
        pi.algoModifyTime = 0;
        fx.ctx.ProcessModify(pi, 1000 * SEC);
        CHECK(fx.modifyCalls.empty(), "D9 无活跃算法单 -> 不改参");
    }

    // D10 坏 id -> 不改参、不崩
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "not-a-number");
        pi.algoModifyTime = 0;
        fx.ctx.ProcessModify(pi, 1000 * SEC);
        CHECK(fx.modifyCalls.empty(), "D10 坏 currentAlgoOrderId -> 不改参（不崩）");
    }

    // D11 快照落盘周期
    {
        const std::string csv = "/tmp/ptsuite/out/timer_snapshot.csv";
        std::remove(csv.c_str());
        Fx fx; Boot(fx, csv); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        const int64_t t0 = 1000 * SEC;
        fx.ctx.OnTimer(t0);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0, "D11 首轮落一次快照");
        struct stat st{};
        CHECK(::stat(csv.c_str(), &st) == 0 && st.st_size > 0, "D11 快照文件已写出");
        fx.ctx.OnTimer(t0 + 5 * SEC);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0, "D11 未到 csvSaveIntervalSec -> 不重复落盘");
        fx.ctx.OnTimer(t0 + 11 * SEC);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0 + 11 * SEC, "D11 到点 -> 再落一次");
    }
}

// ===========================================================================
// E. 撤单
// ===========================================================================
static void TestE_Cancel() {
    Section("E. 撤单");

    const int64_t t0 = 1000 * SEC;
    const int64_t TIMEOUT = 150000LL * 1000LL;   // 150s in us

    // E1 无活跃算法单 -> 不撤
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = false;
        pi.satisfyTime = 0;
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + 10 * TIMEOUT);
        CHECK(fx.modifyCalls.empty(), "E1 无活跃算法单 -> 不撤单");
    }

    // E2 手动单永不撤
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.autoFlag = false;
        pi.satisfyTime = 0;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 777LL);
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + 100 * TIMEOUT);
        CHECK(fx.modifyCalls.empty(), "E2 autoFlag = false（手动单）-> 永不撤单");
    }

    // E3 窗口内 -> 不撤
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.satisfyTime = t0;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 777LL);
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + TIMEOUT - 1);
        CHECK(fx.modifyCalls.empty(), "E3 idle <= algoOrderTimeoutMs -> 不撤单");
    }

    // E4 超时 -> 撤单
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.satisfyTime = t0;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 777LL);
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + TIMEOUT + 1);
        CHECK(fx.modifyCalls.size() == 1, "E4 idle > 超时 -> 请求撤单一次");
        CHECK(fx.LastCmd() == stra::CommandType_CANCEL, "E4 回调命令 = CommandType_CANCEL");
        CHECK(fx.modifyCalls.size() == 1 && fx.modifyCalls[0].first == 777LL, "E4 按 id 指回算法单");
        CHECK(fx.modifyPayloads.size() == 1 && Near(fx.modifyPayloads[0].profitPct, 0.0),
              "E4 撤单回调的 modify 载荷为空");
    }

    // E5 风控强平单不豁免超时
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.riskCloseOrderInFlight = true;       // 这是一张在途的强平单
        pi.satisfyTime = t0;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 777LL);
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + TIMEOUT + 1);
        CHECK(fx.modifyCalls.size() == 1, "E5 强平单同样受超时约束 -> 到点撤单（档位升级的唯一驱动）");
    }

    // E6 RequestCancelAlgoOrder 幂等 / 无回调
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = false;
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.empty(), "E6 无活跃算法单 -> 请求撤单被忽略");

        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 888LL);
        fx.ctx.SetAlgoOrderModifyCallback(nullptr);
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.empty(), "E6 未注册回调 -> 安全返回");
    }

    // E7 坏 id
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "garbage");
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.empty(), "E7 坏 currentAlgoOrderId -> 不回调（不崩）");
    }

    // E8 敞口异常 -> errorFlag + 撤单
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.activeRealPosition = 10.0;   pi.passiveRealPosition = -2.0;
        pi.activeAvgPrice = 100.0;      pi.passiveAvgPrice = 100.0;
        pi.activeParam.multiple = 1.0;  pi.passiveParam.multiple = 1.0;
        pi.ttTargetVolume = 1.0;        // 阈值 = 4 * 1 = 4；净敞口 = |10 - 2| = 8 > 4
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 555LL);
        fx.ctx.CheckExposureAbnormal(pi);
        CHECK(pi.errorFlag, "E8 净敞口超阈值 -> errorFlag 置位（判死、留人工）");
        CHECK(fx.modifyCalls.size() == 1 && fx.LastCmd() == stra::CommandType_CANCEL,
              "E8 同时撤掉在跑的算法单");
    }

    // E9 只有单腿有仓 -> 正常的开仓中间态，不判异常
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.activeRealPosition = 10.0;   pi.passiveRealPosition = 0.0;
        pi.activeAvgPrice = 100.0;      pi.passiveAvgPrice = 100.0;
        pi.ttTargetVolume = 1.0;
        fx.ctx.CheckExposureAbnormal(pi);
        CHECK(!pi.errorFlag, "E9 单腿有仓 -> 不判敞口异常");
    }

    // E10 已 errorFlag -> 不重复处理
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.errorFlag = true;
        pi.activeRealPosition = 100.0; pi.passiveRealPosition = -2.0;
        pi.activeAvgPrice = 100.0;     pi.passiveAvgPrice = 100.0;
        pi.ttTargetVolume = 1.0;
        fx.ctx.CheckExposureAbnormal(pi);
        CHECK(fx.modifyCalls.empty(), "E10 已 errorFlag -> 不重复撤单");
    }

    // E11 阈值无效（ttTargetVolume <= 0 / NaN）-> 不判异常
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.activeRealPosition = 100.0; pi.passiveRealPosition = -2.0;
        pi.activeAvgPrice = 100.0;     pi.passiveAvgPrice = 100.0;
        pi.ttTargetVolume = 0.0;
        fx.ctx.CheckExposureAbnormal(pi);
        CHECK(!pi.errorFlag, "E11 ttTargetVolume = 0 -> 阈值无效，不判异常（否则任何敞口都算异常）");
        pi.ttTargetVolume = std::nan("");
        fx.ctx.CheckExposureAbnormal(pi);
        CHECK(!pi.errorFlag, "E11 ttTargetVolume = NaN -> 不判异常");
    }

    // E12 撤单是异步的：请求撤单不释放对子
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 444LL);
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.size() == 1, "E12 撤单请求已发出");
        CHECK(pi.hasActiveAlgoOrder, "E12 撤单请求不释放对子（要等终态回传）");
        CHECK(std::string(pi.currentAlgoOrderId) == "444", "E12 currentAlgoOrderId 保持不动");
    }

    // E13/E14 ProcessRisk：不撤自己的在途强平单；撤掉非强平单后本轮不再报
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);

        // 制造强平：资金费异常
        pi.rtSpread.activeFundingRate = 0.0;
        pi.rtSpread.passiveFundingRate = 0.4;
        pi.pairTotalVolume = -1.0;
        pi.rtSpread.activePriceTema = 100.0;

        fx.ctx.ProcessRisk(pi, t0);
        CHECK(fx.N() == 1, "E13 风控触发 -> 报出强平单");
        CHECK(pi.riskCloseOrderInFlight, "E13 报出后置 riskCloseOrderInFlight");

        // 同一张强平单占着对子 -> 不应被 ProcessRisk 撤掉
        fx.ctx.ProcessRisk(pi, t0 + 1);
        CHECK(fx.modifyCalls.empty(), "E13 ProcessRisk 不撤自己刚发的在途强平单");
        CHECK(fx.N() == 1, "E13 强平单在途时不重复报单");

        // 换成一张非强平的算法单 -> 先撤，本轮不报
        pi.riskCloseOrderInFlight = false;
        fx.ctx.ProcessRisk(pi, t0 + 2);
        CHECK(fx.modifyCalls.size() == 1 && fx.LastCmd() == stra::CommandType_CANCEL,
              "E14 对子被非强平算法单占着 -> 先请求撤单");
        CHECK(fx.N() == 1, "E14 撤单后本轮 return，不叠加报新单");
    }
}

// ===========================================================================
// F. 风控触发
// ===========================================================================
static void TestF_Risk() {
    Section("F. 风控触发");

    const int64_t t0 = 1000 * DAY;

    // F1 无持仓 + 风控不要求强平 -> 不报单
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.ProcessRisk(pi, t0);
        CHECK(fx.N() == 0, "F1 空仓 -> 不报强平单");
    }

    // F2/F3 多 -> CL / 空 -> CS；autoFlag -> TT，否则 MT
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.rtSpread.passiveFundingRate = 0.4;    // 触发资金费异常
        fx.ctx.ProcessRisk(pi, t0);
        CHECK(fx.N() == 1, "F2 多头持仓 + 风控 -> 报单");
        if (fx.N() == 1) CHECK(fx.submits[0]->ttCSStartSpread != 0.0 || fx.submits[0]->ttCLSwitch,
                               "F2 多头 -> 平多方向（CL）");
        CHECK(pi.riskCloseOrderInFlight, "F4 报单成功后 riskCloseOrderInFlight = true");
    }

    // F5 碎单风控
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -0.1);
        pi.rtSpread.activePriceTema = 100.0;      // 市值 = 0.1 * 100 = 10 USDT < 25
        RiskCheckResult r = RiskManager::Instance().CheckRisk(pi, t0);
        CHECK(r.hasRisk && r.isTinyClose && r.needForceClose, "F5 持仓市值 < 25 USDT -> 碎单强平");
    }

    // F6 档位升级：撤单 -> OnAlgoFinished -> tierNTimes++ -> 下一档
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.rtSpread.passiveFundingRate = 0.4;

        fx.ctx.ProcessRisk(pi, t0);
        CHECK(fx.N() == 1, "F6 第一次强平：报单");
        CHECK(pi.fundingAbnormal.triggered && pi.fundingAbnormal.currentTier == 1, "F6 档位 = 1");
        CHECK(pi.riskCloseOrderInFlight, "F6 强平单在途");

        // 强平单被撤 / 未成交而终结：OnAlgoOrderUpdate 终态 + 仍持仓
        BaseAlgoOrder* o1 = fx.submits[0];
        o1->pairTotalVolume = -1.0;
        o1->pairActiveTotalPrice = 100.0;
        o1->pairPassiveTotalPrice = 100.0;
        o1->algoOrderStatus = stra::ALGO_OS_CANCELED;
        fx.ctx.OnAlgoOrderUpdate(o1);
        CHECK(pi.fundingAbnormal.tier1Times == 1, "F6 终结后 tier1Times = 1（档位升级的唯一驱动）");
        CHECK(!pi.hasActiveAlgoOrder, "F6 终态释放对子");
        CHECK(!pi.riskCloseOrderInFlight, "F6 释放时清掉在途标记");

        // 未到 tier1WaitUs -> 不再报（等下一档）
        fx.ctx.ProcessRisk(pi, t0 + 1);
        CHECK(fx.N() == 1, "F6 未到 tier1WaitUs -> 不再报单");

        // 过了 tier1WaitUs -> 升到 tier2
        fx.ctx.ProcessRisk(pi, t0 + RiskManager::Instance().GetConfig().tier1WaitUs + 1);
        CHECK(fx.N() == 2, "F6 过了 tier1WaitUs -> 报出 tier2 强平单");
        CHECK(pi.fundingAbnormal.currentTier == 2, "F6 档位升到 2");
        if (fx.N() == 2) {
            const double t1 = RiskManager::Instance().GetConfig().tier1ForgoProfit;
            const double t2 = RiskManager::Instance().GetConfig().tier2ForgoProfit;
            CHECK(t2 > t1, "F6 tier2 让利幅度大于 tier1（越等越激进）");
        }

        // tier2 终结 -> tier2Times++
        BaseAlgoOrder* o2 = fx.submits[1];
        o2->pairTotalVolume = -1.0;
        o2->algoOrderStatus = stra::ALGO_OS_CANCELED;
        fx.ctx.OnAlgoOrderUpdate(o2);
        CHECK(pi.fundingAbnormal.tier2Times == 1, "F6 tier2 终结 -> tier2Times = 1");

        // 过了 tier1+tier2 -> tier3
        const auto& rc = RiskManager::Instance().GetConfig();
        fx.ctx.ProcessRisk(pi, t0 + rc.tier1WaitUs + rc.tier2WaitUs + 2);
        CHECK(fx.N() == 3, "F6 再过 tier2WaitUs -> 报出 tier3 强平单");
        CHECK(pi.fundingAbnormal.currentTier == 3, "F6 档位升到 3");

        // tier3 是最后一档：不再升级、也不再停手
        BaseAlgoOrder* o3 = fx.submits[2];
        o3->pairTotalVolume = -1.0;
        o3->algoOrderStatus = stra::ALGO_OS_CANCELED;
        fx.ctx.OnAlgoOrderUpdate(o3);
        fx.ctx.ProcessRisk(pi, t0 + 100 * DAY);
        CHECK(fx.N() == 4, "F6 tier3 之后持续重试（不停手、不升级）");
        CHECK(pi.fundingAbnormal.currentTier == 3, "F6 档位停在 3");
    }

    // F7/F8 终态结算与完全平仓复位
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.rtSpread.passiveFundingRate = 0.4;
        fx.ctx.ProcessRisk(pi, t0);
        CHECK(fx.N() == 1, "F7 报出强平单");

        BaseAlgoOrder* o = fx.submits[0];
        o->pairTotalVolume = 0.0;               // 已平干净
        o->algoOrderStatus = stra::ALGO_OS_FILLED;
        pi.openSmallSpreadBidBidUQ = 0.5;
        pi.openSmallSpreadAskAskDQ = -0.5;
        fx.ctx.OnAlgoOrderUpdate(o);
        CHECK(!pi.hasActiveAlgoOrder, "F7 终态 -> 释放对子");
        CHECK(!pi.fundingAbnormal.triggered && pi.fundingAbnormal.currentTier == 0,
              "F8 完全平仓 -> 风控档位复位");
        CHECK(std::isnan(pi.openSmallSpreadBidBidUQ) && std::isnan(pi.openSmallSpreadAskAskDQ),
              "F8 完全平仓 -> 建仓基准复位为 NaN");
    }

    // F9 过期回传被丢弃
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 111LL);
        BaseAlgoOrder stale;
        stale.algoOrderId = 222;                // 不是当前算法单
        std::strncpy(stale.pairInstrumentKey, kPairKey, sizeof(stale.pairInstrumentKey) - 1);
        stale.algoOrderStatus = stra::ALGO_OS_FILLED;
        stale.pairTotalVolume = 0.0;
        fx.ctx.OnAlgoOrderUpdate(&stale);
        CHECK(pi.hasActiveAlgoOrder, "F9 过期回传（id 不符）-> 不释放对子");
        CHECK(pi.pairTotalVolume == -1.0, "F9 过期回传 -> 不改账本");
    }

    // F10 null / 未知对子
    {
        Fx fx; Boot(fx); GoTrading(fx);
        fx.ctx.OnAlgoOrderUpdate(nullptr);
        CHECK(true, "F10 OnAlgoOrderUpdate(nullptr) 不崩");
        BaseAlgoOrder ghost;
        ghost.algoOrderId = 1;
        std::strncpy(ghost.pairInstrumentKey, "NO.SUCH.PAIR", sizeof(ghost.pairInstrumentKey) - 1);
        fx.ctx.OnAlgoOrderUpdate(&ghost);
        CHECK(true, "F10 未知对子的回传不崩");
    }

    // F11 非终态只同步量价，不释放对子
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.hasActiveAlgoOrder = true;
        std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 333LL);
        BaseAlgoOrder o;
        o.algoOrderId = 333;
        std::strncpy(o.pairInstrumentKey, kPairKey, sizeof(o.pairInstrumentKey) - 1);
        o.algoOrderStatus = stra::ALGO_OS_PARTFILLED;    // 非终态
        o.pairTotalVolume = -2.5;
        o.pairActiveTotalPrice = 0.11;
        o.pairPassiveTotalPrice = 0.12;
        fx.ctx.OnAlgoOrderUpdate(&o);
        CHECK(Near(pi.pairTotalVolume, -2.5), "F11 非终态 -> 账本量已同步");
        CHECK(Near(pi.pairActiveTotalPrice, 0.11) && Near(pi.pairPassiveTotalPrice, 0.12),
              "F11 非终态 -> 记账均价已同步");
        CHECK(pi.hasActiveAlgoOrder, "F11 非终态 -> 不释放对子");
    }

    // F12 ADL 风控的三个前置条件
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -30.0);
        pi.rtSpread.activePriceTema = 100.0;         // 市值 = 3000 USDT >= 2000
        pi.activeAdlRank = 0.9;                      // >= 0.8

        RiskCheckResult r = RiskManager::Instance().CheckRisk(pi, t0);
        CHECK(!r.needForceClose, "F12 首次观察 -> 只记持仓计时起点，不立即强平");
        CHECK(pi.positionExceedThresholdStartTime == t0, "F12 持仓计时起点已建立");

        r = RiskManager::Instance().CheckRisk(pi, t0 + 3 * DAY);
        CHECK(!r.needForceClose, "F12 未满 4 天 -> 不触发");

        r = RiskManager::Instance().CheckRisk(pi, t0 + 4 * DAY + 1);
        CHECK(r.needForceClose && r.riskType == AbnormalClose_ADL, "F12 满 4 天 + ADL 分位高 -> ADL 强平");

        PairInfo pj; MakeHoldable(pj, -30.0);
        pj.rtSpread.activePriceTema = 100.0;
        pj.activeAdlRank = 0.1;                      // < 0.8
        RiskManager::Instance().CheckRisk(pj, t0);
        RiskCheckResult r2 = RiskManager::Instance().CheckRisk(pj, t0 + 5 * DAY);
        CHECK(!r2.needForceClose, "F12 ADL 分位不足 -> 不触发");

        PairInfo pk; MakeHoldable(pk, -1.0);
        pk.rtSpread.activePriceTema = 100.0;         // 市值 = 100 < 2000
        pk.activeAdlRank = 0.9;
        RiskManager::Instance().CheckRisk(pk, t0);
        RiskCheckResult r3 = RiskManager::Instance().CheckRisk(pk, t0 + 5 * DAY);
        CHECK(!r3.needForceClose, "F12 持仓市值不足阈值 -> 不触发");
    }

    // F13 资金费异常
    {
        PairInfo pi; MakeHoldable(pi, -1.0);
        pi.rtSpread.activePriceTema = 100.0;
        pi.rtSpread.activeFundingRate = 0.0;
        pi.rtSpread.passiveFundingRate = 0.4;         // fundingRisk = -100 * 0.4 = -40 <= -30
        RiskCheckResult r = RiskManager::Instance().CheckRisk(pi, t0);
        CHECK(r.needForceClose && r.riskType == AbnormalClose_FUNDING_ABNORMAL, "F13 资金费亏损超 30 USDT -> 强平");

        PairInfo pj; MakeHoldable(pj, -1.0);
        pj.rtSpread.activePriceTema = 100.0;
        pj.rtSpread.activeFundingRate = 0.0;
        pj.rtSpread.passiveFundingRate = 0.1;         // fundingRisk = -10 > -30
        RiskCheckResult r2 = RiskManager::Instance().CheckRisk(pj, t0);
        CHECK(!r2.needForceClose, "F13 资金费亏损未超阈值 -> 不触发");
    }

    // F14 价差不回归：最短持有期 + 统计有效性
    {
        PairInfo pi; MakeHoldable(pi, -1.0);
        pi.rtSpread.activePriceTema = 100.0;
        pi.orderParams.ttCLStartSpread = 10.0;        // 不可达 -> 未回归
        pi.orderParams.mtCLStartSpread = 10.0;
        pi.largeStats.valid = true; pi.largeStats.count = 100;
        pi.largeStats.bidBidUQ = 0.001; pi.largeStats.askAskDQ = -0.001;

        RiskManager::Instance().CheckRisk(pi, t0);
        RiskCheckResult r = RiskManager::Instance().CheckRisk(pi, t0 + HOUR - 1);
        CHECK(!r.needForceClose, "F14 未满最短持有期(1h) -> 不触发（且不启动未回归计时）");
        CHECK(pi.spreadNoRegressionStartTime == 0, "F14 最短持有期内不启动未回归计时");

        RiskManager::Instance().CheckRisk(pi, t0 + HOUR + 1);
        CHECK(pi.spreadNoRegressionStartTime == t0 + HOUR + 1, "F14 持有期满 -> 未回归计时启动");
        r = RiskManager::Instance().CheckRisk(pi, t0 + HOUR + 1 + DAY + 1);
        CHECK(r.needForceClose && r.riskType == AbnormalClose_SPREAD_REGRESSION,
              "F14 连续 24h 未回归 -> 渐进式平仓");

        PairInfo pj; MakeHoldable(pj, -1.0);
        pj.rtSpread.activePriceTema = 100.0;
        pj.orderParams.ttCLStartSpread = 10.0; pj.orderParams.mtCLStartSpread = 10.0;
        pj.largeStats = SpreadStats();                // 统计无效
        RiskManager::Instance().CheckRisk(pj, t0);
        RiskManager::Instance().CheckRisk(pj, t0 + HOUR + 1);
        RiskCheckResult r2 = RiskManager::Instance().CheckRisk(pj, t0 + HOUR + 1 + 30 * DAY);
        CHECK(!r2.needForceClose, "F14 统计无效 -> 这条风控暂缓（阈值是默认 0，判回归没有意义）");
    }
}

// ===========================================================================
// G. 重启
// ===========================================================================
static void TestG_Restart() {
    Section("G. 重启");

    // G1 Init 停在 Reconciling
    {
        Fx fx; Boot(fx);
        CHECK(!fx.ctx.IsTradingReady(), "G1 Init 结束在 Reconciling（交易冻结）");
        CHECK(fx.ctx.m_startupTimeUs > 0, "G1 记录启动时刻");
        CHECK(fx.ctx.m_positionBatchDoneUs.empty(), "G1 批次表为空（任何记录必然来自本次启动之后）");
    }

    // G2 快照恢复
    {
        const std::string csv = "/tmp/ptsuite/out/restore.csv";
        std::remove(csv.c_str());
        {
            Fx fx; Boot(fx, csv);
            PairInfo& pi = *fx.pi();
            MakeHoldable(pi, -7.5);
            pi.pairActiveTotalPrice = 0.111;
            pi.pairPassiveTotalPrice = 0.222;
            pi.adlClose.triggered = true; pi.adlClose.currentTier = 2; pi.adlClose.tier1Times = 1;
            pi.autoFlag = false; pi.stopFlag = true; pi.profitPct = 0.003;
            pi.positionExceedThresholdStartTime = 123456;
            PairInfoManager::Instance().SaveSnapshot(csv);
        }
        // 重启：全新的 ctx，同一份配置
        Fx fx; Boot(fx, csv);
        PairInfo& pi = *fx.pi();
        CHECK(Near(pi.pairTotalVolume, -7.5), "G2 账本量恢复");
        CHECK(Near(pi.pairActiveTotalPrice, 0.111) && Near(pi.pairPassiveTotalPrice, 0.222), "G2 记账均价恢复");
        CHECK(pi.adlClose.triggered && pi.adlClose.currentTier == 2 && pi.adlClose.tier1Times == 1,
              "G2 风控档位恢复（跨轮次累积的耐心）");
        CHECK(!pi.autoFlag && pi.stopFlag && Near(pi.profitPct, 0.003), "G2 运维意图恢复");
        CHECK(pi.positionExceedThresholdStartTime == 123456, "G2 风控计时起点恢复");
        CHECK(!pi.activePushArrived && !pi.passivePushArrived, "G2 推送标记是运行态 -> 不恢复");
        CHECK(pi.activeRealPosition == 0.0, "G2 账户真值不恢复（由推送自愈）");
        CHECK(!fx.ctx.IsTradingReady(), "G2 重启后仍在 Reconciling");
    }

    // G3 没有快照
    {
        const std::string csv = "/tmp/ptsuite/out/does_not_exist.csv";
        std::remove(csv.c_str());
        Fx fx; Boot(fx, csv);
        PairInfo& pi = *fx.pi();
        CHECK(pi.pairTotalVolume == 0.0, "G3 无快照 -> 按无持仓启动");
        CHECK(pi.adlClose.currentTier == 0, "G3 无快照 -> 风控档位默认");
    }

    // G4 列数不兼容的快照被拒绝
    {
        const std::string csv = "/tmp/ptsuite/out/bad_header.csv";
        std::remove(csv.c_str());
        { std::ofstream ofs(csv); ofs << "a,b,c\n1,2,3\n"; }
        Fx fx; Boot(fx, csv);
        CHECK(true, "G4 列数不兼容的快照被忽略（不崩、按无持仓启动）");
        PairInfo& pi = *fx.pi();
        CHECK(pi.pairTotalVolume == 0.0, "G4 账本保持默认");
    }

    // G5 孤儿算法单 -> 告警 + 冻结
    {
        const std::string csv = "/tmp/ptsuite/out/orphan.csv";
        std::remove(csv.c_str());
        {
            Fx fx; Boot(fx, csv);
            PairInfo& pi = *fx.pi();
            MakeHoldable(pi, -5.0);
            pi.hasActiveAlgoOrder = true;
            std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 123456789LL);
            PairInfoManager::Instance().SaveSnapshot(csv);
        }
        Fx fx; Boot(fx, csv);
        PairInfo& pi = *fx.pi();
        CHECK(pi.errorFlag, "G5 快照里 hasActiveAlgoOrder -> errorFlag 冻结（账本不可信）");
        CHECK(pi.hasActiveAlgoOrder, "G5 冻结时不静默清掉残留标记");
    }

    // G6 freezeOnOrphanAlgoOrder = false -> 只告警
    {
        const std::string csv = "/tmp/ptsuite/out/orphan2.csv";
        std::remove(csv.c_str());
        {
            Fx fx; Boot(fx, csv);
            PairInfo& pi = *fx.pi();
            MakeHoldable(pi, -5.0);
            pi.hasActiveAlgoOrder = true;
            std::snprintf(pi.currentAlgoOrderId, sizeof(pi.currentAlgoOrderId), "%lld", 123456789LL);
            PairInfoManager::Instance().SaveSnapshot(csv);
        }
        ResetWorld();
        Fx fx;
        fx.smc.Clear();
        fx.smc.Set(kActive, md::InstrumentInfo{1.0, 0.0001, 0.1, 0});
        fx.smc.Set(kPassive, md::InstrumentInfo{1.0, 0.0001, 0.1, 0});
        PairTradingConfig cfg;
        cfg.pairKeys = {kPairKey};
        cfg.activeAccountId = 10000; cfg.passiveAccountId = 10001;
        cfg.csvStatePath = csv;
        cfg.freezeOnOrphanAlgoOrder = false;
        fx.Wire();
        fx.ctx.Init(cfg, &fx.smc);
        PairInfo& pi = *fx.pi();
        CHECK(!pi.errorFlag, "G6 freezeOnOrphanAlgoOrder = false -> 不冻结");
    }

    // G7 对账等待
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        CHECK(!fx.ctx.TryReconcile(1000 * SEC), "G7 两腿推送都没到 -> 不放行");
        CHECK(!fx.ctx.IsTradingReady(), "G7 phase 仍 Reconciling");
        pi.activePushArrived = true;   // 只有一条腿
        CHECK(!fx.ctx.TryReconcile(1000 * SEC), "G7 只有一条腿到位 -> 仍不放行");
    }

    // G8 批次到达也算"已知"；启动前的批次不算
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        fx.ctx.m_startupTimeUs = 5000;
        fx.ctx.m_positionBatchDoneUs[10000] = 1000;    // 启动前
        fx.ctx.m_positionBatchDoneUs[10001] = 1000;
        CHECK(!fx.ctx.TryReconcile(6000), "G8 启动前的批次不算数");
        fx.ctx.m_positionBatchDoneUs[10000] = 6000;
        fx.ctx.m_positionBatchDoneUs[10001] = 6000;
        CHECK(fx.ctx.TryReconcile(7000), "G8 启动后的批次到达 -> 放行");
    }

    // G9 对账一致 -> 放行，账本/档位保留
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.adlClose.triggered = true; pi.adlClose.currentTier = 2;
        pi.activeRealPosition = -10.0;
        pi.activePushArrived = true; pi.passivePushArrived = true;
        CHECK(fx.ctx.TryReconcile(1000 * SEC), "G9 一致 -> 放行");
        CHECK(fx.ctx.IsTradingReady(), "G9 phase -> Trading");
        CHECK(Near(pi.pairTotalVolume, -10.0), "G9 账本保留");
        CHECK(pi.adlClose.currentTier == 2, "G9 档位保留");
    }

    // G10 账本有仓、交易所空仓 -> 全套复位
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.adlClose.triggered = true; pi.adlClose.currentTier = 2; pi.adlClose.tier1Times = 1;
        pi.spreadNoRegression.triggered = true;
        pi.positionExceedThresholdStartTime = 999;
        pi.spreadNoRegressionStartTime = 888;
        pi.openSmallSpreadBidBidUQ = 0.5;
        pi.activeRealPosition = 0.0;                  // 交易所空仓
        pi.activePushArrived = true; pi.passivePushArrived = true;
        fx.ctx.TryReconcile(1000 * SEC);
        CHECK(pi.pairTotalVolume == 0.0, "G10 账本清零");
        CHECK(pi.pairActiveTotalPrice == -1.0 && pi.pairPassiveTotalPrice == -1.0, "G10 记账均价 -> -1 哨兵");
        CHECK(std::isnan(pi.openSmallSpreadBidBidUQ), "G10 建仓基准 -> NaN");
        CHECK(!pi.adlClose.triggered && pi.adlClose.currentTier == 0, "G10 风控档位复位");
        CHECK(pi.positionExceedThresholdStartTime == 0 && pi.spreadNoRegressionStartTime == 0,
              "G10 风控计时复位");
    }

    // G11 账本有仓、交易所仍持仓 -> 只改量，保留均价与档位
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.pairActiveTotalPrice = 0.111;
        pi.adlClose.triggered = true; pi.adlClose.currentTier = 2;
        pi.activeRealPosition = -12.0;                // 与账本不一致
        pi.activePushArrived = true; pi.passivePushArrived = true;
        fx.ctx.TryReconcile(1000 * SEC);
        CHECK(Near(pi.pairTotalVolume, -12.0), "G11 以系统推送为准改量");
        CHECK(Near(pi.pairActiveTotalPrice, 0.111), "G11 记账均价保留（建仓基准）");
        CHECK(pi.adlClose.currentTier == 2, "G11 档位保留（重启不重置风控耐心）");
    }

    // G12 符号翻转算不一致
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.activeRealPosition = 10.0;                 // 量级相同、多空翻转
        pi.activePushArrived = true; pi.passivePushArrived = true;
        fx.ctx.TryReconcile(1000 * SEC);
        CHECK(Near(pi.pairTotalVolume, 10.0), "G12 符号翻转 -> 判不一致，以实时为准");
    }

    // G13 容差内不动
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -10.0);
        pi.activeRealPosition = -10.000001;           // 在 1e-6 相对容差内
        pi.activePushArrived = true; pi.passivePushArrived = true;
        fx.ctx.TryReconcile(1000 * SEC);
        CHECK(Near(pi.pairTotalVolume, -10.0), "G13 容差内 -> 账本不动");
        CHECK(fx.ctx.IsTradingReady(), "G13 容差内 -> 放行");
    }

    // G14 重启后闸门未开 -> 有信号也不报单
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, "ttOL", 0.0);
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "G14 重启后未对账 -> 有信号也不报单（防在孤儿仓上再开一笔）");
    }

    // G15 快照往返（含 NaN）
    {
        const std::string csv = "/tmp/ptsuite/out/roundtrip.csv";
        std::remove(csv.c_str());
        double vol = 0.0, p1 = 0.0;
        int tier = 0;
        {
            Fx fx; Boot(fx, csv);
            PairInfo& pi = *fx.pi();
            MakeHoldable(pi, -3.25);
            pi.pairActiveTotalPrice = 0.777;
            pi.fundingAbnormal.triggered = true; pi.fundingAbnormal.currentTier = 3;
            PairInfoManager::Instance().SaveSnapshot(csv);
            vol = pi.pairTotalVolume; p1 = pi.pairActiveTotalPrice; tier = pi.fundingAbnormal.currentTier;
        }
        Fx fx; Boot(fx, csv);
        PairInfo& pi = *fx.pi();
        CHECK(Near(pi.pairTotalVolume, vol), "G15 量往返无损");
        CHECK(Near(pi.pairActiveTotalPrice, p1), "G15 价往返无损");
        CHECK(pi.fundingAbnormal.currentTier == tier && pi.fundingAbnormal.triggered, "G15 档位往返无损");
        CHECK(std::isnan(pi.openSmallSpreadBidBidUQ), "G15 NaN 往返无损（哨兵语义保持）");
    }

    // G16 PairCmd_RESUME 复活被冻结的对子
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -5.0);
        pi.errorFlag = true;
        pi.stopFlag = true;
        pi.autoFlag = false;
        PairInfoManager::Instance().ApplyCommand(kPairKey, PairCmd_RESUME);
        CHECK(!pi.errorFlag, "G16 RESUME 清掉 errorFlag（孤儿冻结 / 敞口异常的唯一复活路径）");
        CHECK(!pi.stopFlag && pi.autoFlag, "G16 RESUME 恢复自动交易");
    }
}

// ===========================================================================
// H. 持仓推送（OnPosition -> PairInfoManager::UpdateOnPosition）
//
// 这是 activeRealPosition 的唯一来源，也是启动闸门放行的唯一路径。
// 符号约定：pos.direction == DT_SHORT -> 量取负（与 pairTotalVolume "负 = 多" 同号，
// 因为开多时主动腿本身是空腿）。
// ===========================================================================
static void TestH_Position() {
    Section("H. 持仓推送（OnPosition）");

    // H1 主动腿：整组字段写入 + 推送到位标记
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(ActivePos(DT_LONG, 3.0));
        CHECK(Near(pi.activeRealPosition, 3.0), "H1 主动腿持仓量写入");
        CHECK(Near(pi.activeAvgPrice, 0.5),     "H1 主动腿均价写入");
        CHECK(Near(pi.activeFloatPnl, 1.2),     "H1 主动腿浮盈写入");
        CHECK(Near(pi.activeLiquidPrice, 0.3),  "H1 主动腿强平价写入");
        CHECK(Near(pi.activeMarkPrice, 0.55),   "H1 主动腿标记价写入");
        CHECK(Near(pi.activeAdlRank, 2.0),      "H1 主动腿 ADL 分位写入");
        CHECK(pi.activePushArrived,             "H1 主动腿推送到位标记");
        CHECK(!pi.passivePushArrived && Near(pi.passiveRealPosition, 0.0),
              "H1 被动腿完全不受影响");
    }

    // H2 DT_SHORT 取负 —— "负 = 多" 符号约定的入口
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(ActivePos(DT_SHORT, 3.0));
        CHECK(Near(pi.activeRealPosition, -3.0),
              "H2 DT_SHORT -> 取负（开多时主动腿是空腿，故 负 = 多）");
    }

    // H3 direction 非法（DT_MIN）-> 该腿持仓被清 0
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.activeRealPosition = 4.0;
        fx.ctx.OnPosition(ActivePos(DT_MIN, 3.0));
        CHECK(Near(pi.activeRealPosition, 0.0),
              "H3 direction 非法 -> 量写成 0（volume 初值）——未知方向被当成空仓");
        CHECK(pi.activePushArrived, "H3 但推送到位标记仍然置位");
    }

    // H4 被动腿走 passive 通道
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(PassivePos(DT_SHORT, 2.5));
        CHECK(Near(pi.passiveRealPosition, -2.5), "H4 被动腿持仓量写入（DT_SHORT -> 负）");
        CHECK(pi.passivePushArrived,              "H4 被动腿推送到位标记");
        CHECK(!pi.activePushArrived && Near(pi.activeRealPosition, 0.0),
              "H4 主动腿完全不受影响");
    }

    // H5 未注册的 instId -> 整条推送丢弃
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.activeRealPosition = 7.0;
        fx.ctx.OnPosition(MakePosition(BINANCE, USDT_SWAP, "BTC-USDT", DT_LONG, 9.0,
                                       0.5, 1.2, 0.3, 0.55, 2.0, 10000, true));
        CHECK(Near(pi.activeRealPosition, 7.0),
              "H5 未注册合约 -> 整条推送丢弃（FindPairsByInstrument 返回空）");
        CHECK(!pi.activePushArrived, "H5 推送到位标记也不置位");
    }

    // H6 交易所不同但 symbol 相同 -> 不命中
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(MakePosition(OKX, USDT_SWAP, "DOGE-USDT", DT_LONG, 9.0,
                                       0.5, 1.2, 0.3, 0.55, 2.0, 10000, true));
        CHECK(Near(pi.activeRealPosition, 0.0),
              "H6 OKX 的 DOGE-USDT 不落到 BINANCE 腿上（instrKey 带交易所/合约类型前缀）");
    }

    // H7 isLast -> 该账户"持仓批次已完整到达"
    {
        Fx fx; Boot(fx);
        CHECK(!fx.ctx.BatchDoneAfterStart(10000), "H7 初始未收到主动腿账户的批次尾");
        fx.ctx.OnPosition(ActivePos(DT_LONG, 1.0, /*isLast=*/true));
        CHECK(fx.ctx.BatchDoneAfterStart(10000),  "H7 isLast -> 主动腿账户批次尾已记账");
        CHECK(!fx.ctx.BatchDoneAfterStart(10001), "H7 另一账户不受影响（isLast 是单账户批次尾）");
    }

    // H8 非批次尾 -> 不记账
    {
        Fx fx; Boot(fx);
        fx.ctx.OnPosition(ActivePos(DT_LONG, 1.0, /*isLast=*/false));
        CHECK(!fx.ctx.BatchDoneAfterStart(10000), "H8 isLast=false -> 不记批次尾");
    }

    // H9 强平风险等级：|liquidPrice/markPrice - 1| 三档
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(MakePosition(BINANCE, USDT_SWAP, "DOGE-USDT", DT_LONG, 1.0,
                                       0.5, 0.0, 0.50, 0.55, 0.0, 10000, true)); // 0.0909
        CHECK(pi.activeLiquidStatus == 2, "H9 强平价距标记价 <30% -> 等级 2（危险）");
        fx.ctx.OnPosition(MakePosition(BINANCE, USDT_SWAP, "DOGE-USDT", DT_LONG, 1.0,
                                       0.5, 0.0, 0.30, 0.55, 0.0, 10000, true)); // 0.4545
        CHECK(pi.activeLiquidStatus == 1, "H9 距标记价 <60% -> 等级 1（警告）");
        fx.ctx.OnPosition(MakePosition(BINANCE, USDT_SWAP, "DOGE-USDT", DT_LONG, 1.0,
                                       0.5, 0.0, 0.10, 0.55, 0.0, 10000, true)); // 0.8181
        CHECK(pi.activeLiquidStatus == 0, "H9 距离充足 -> 等级 0");
        fx.ctx.OnPosition(MakePosition(BINANCE, USDT_SWAP, "DOGE-USDT", DT_LONG, 1.0,
                                       0.5, 0.0, 0.0, 0.55, 0.0, 10000, true));  // liq <= 0
        CHECK(pi.activeLiquidStatus == 0, "H9 无强平价（<=0）-> 等级 0");
    }

    // H10 启动闸门：两腿推送到位才放行（走 OnTimer 的真实路径）
    {
        Fx fx; Boot(fx);
        const int64_t t = fx.ctx.m_startupTimeUs + 1 * SEC;
        CHECK(!fx.ctx.IsTradingReady(), "H10 Init 后处于 Reconciling（交易冻结）");

        fx.ctx.OnPosition(ActivePos(DT_LONG, 0.0, true));
        fx.ctx.OnTimer(t);
        CHECK(!fx.ctx.IsTradingReady(),
              "H10 只推主动腿 -> 闸门保持冻结（不能按'被动腿空仓'放行）");

        fx.ctx.OnPosition(PassivePos(DT_LONG, 0.0, true));
        fx.ctx.OnTimer(t + 1 * SEC);
        CHECK(fx.ctx.IsTradingReady(), "H10 两腿都到位 -> 放行 Trading");
    }

    // H11 批次尾可以替代推送（交易所不为"从未持有过"的腿推零仓）
    {
        Fx fx; Boot(fx);
        fx.ctx.OnPosition(ActivePos(DT_LONG, 0.0, true));
        // 被动腿账户的批次尾到了，但批次里没有这条腿 —— 该腿即空仓
        fx.ctx.OnPosition(MakePosition(GATEIO, USDT_SWAP, "BTC-USDT", DT_LONG, 0.0,
                                       0.5, 0.0, 0.3, 0.55, 0.0, 10001, true));
        fx.ctx.OnTimer(fx.ctx.m_startupTimeUs + 2 * SEC);
        CHECK(fx.ctx.IsTradingReady(),
              "H11 被动腿用批次尾（批次里没有它 = 空仓）放行 —— 只等 push 会永远等不到");
    }

    // H12 对账不一致 -> 以实时持仓为准，但保留建仓基准与风控档位
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.pairTotalVolume      = 5.0;      // 快照恢复出来的账本
        pi.pairActiveTotalPrice = 0.777;    // 建仓基准
        pi.fundingAbnormal.currentTier = 3; // 风控耐心
        fx.ctx.OnPosition(ActivePos(DT_SHORT, 2.0, true));   // -> activeRealPosition = -2.0
        fx.ctx.OnPosition(PassivePos(DT_LONG, 0.0, true));
        CHECK(fx.ctx.TryReconcile(fx.ctx.m_startupTimeUs + 1 * SEC), "H12 对账返回可放行");
        CHECK(Near(pi.pairTotalVolume, -2.0), "H12 不一致 -> 账本改成实时持仓（以推送为准）");
        CHECK(Near(pi.pairActiveTotalPrice, 0.777),
              "H12 交易所仍持仓 -> 记账均价保留（建仓基准不被推送覆盖）");
        CHECK(pi.fundingAbnormal.currentTier == 3,
              "H12 风控档位保留（重启不该重置风控耐心）");
    }

    // H13 对账时交易所已空仓 -> 账本清零 + 持仓期状态全部复位
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.pairTotalVolume      = 5.0;
        pi.pairActiveTotalPrice = 0.777;
        pi.fundingAbnormal.triggered = true;
        pi.fundingAbnormal.currentTier = 3;
        pi.positionStartTime = 12345;
        fx.ctx.OnPosition(ActivePos(DT_LONG, 0.0, true));    // -> activeRealPosition = 0
        fx.ctx.OnPosition(PassivePos(DT_LONG, 0.0, true));
        fx.ctx.TryReconcile(fx.ctx.m_startupTimeUs + 1 * SEC);
        CHECK(Near(pi.pairTotalVolume, 0.0),       "H13 交易所空仓 -> 账本清零");
        CHECK(Near(pi.pairActiveTotalPrice, -1.0), "H13 建仓基准复位为 -1 哨兵");
        CHECK(!pi.fundingAbnormal.triggered && pi.fundingAbnormal.currentTier == 0,
              "H13 风控档位复位（与 RiskManager::OnAlgoFinished(fullyFlat) 同一套不变量）");
        CHECK(pi.positionStartTime == 0,           "H13 风控计时起点复位");
    }

    // H14 同一条腿重复推送 -> 后到覆盖先到
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnPosition(ActivePos(DT_LONG, 1.0));
        fx.ctx.OnPosition(ActivePos(DT_LONG, 4.0));
        CHECK(Near(pi.activeRealPosition, 4.0), "H14 同腿重复推送 -> 后到覆盖先到");
    }
}

// ===========================================================================
// I. 资金推送（OnBalance -> PairInfoManager::UpdateOnBalance）
//
// ⚠️ 用户确认：**保留当前实现，不做修改**。本组用例的作用是把当前行为钉住，
//    包括其中一处已知的坑（I2 / I3）—— 将来谁改这里，用例会先红。
//
// 当前实现的匹配方式是 strstr（子串包含），key 形如
//     <EXCHANGE>.<INST_TYPE>.<currency>-<baseAsset>
// 而腿的 instrumentKey 形如 <EXCHANGE>.<INST_TYPE>.<SYMBOL>。
// 两者能对上的唯一条件是 currency 恰好等于 SYMBOL（如 "DOGE"）。
// 真实推送里 currency 是保证金资产（"USDT"）-> 拼出 "…USDT-USDT" -> 永远对不上 -> 整条空转。
// ===========================================================================
static void TestI_Balance() {
    Section("I. 资金推送（OnBalance）");

    // I1 真实场景：currency 是保证金资产 -> 整条推送空转
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.activeRealPosition = 1.0;
        pi.activeFloatPnl     = 2.0;
        fx.ctx.OnBalance(MakeBalance(BINANCE, USDT_SWAP, "USDT", 12345.0, 9.9));
        CHECK(Near(pi.activeRealPosition, 1.0), "I1 currency=USDT -> 拼不出腿的 symbol，持仓量不动");
        CHECK(Near(pi.activeFloatPnl, 2.0),     "I1 浮盈也不动（整条推送空转）");
    }

    // I2 万一 currency 恰好是 SYMBOL -> 把"账户权益"当成"持仓量"写进去（已知坑，按当前逻辑保留）
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnBalance(MakeBalance(BINANCE, USDT_SWAP, "DOGE", 12345.0, 9.9));
        CHECK(Near(pi.activeRealPosition, 12345.0),
              "I2 currency=DOGE -> balance.total(账户权益) 写进 activeRealPosition（已知坑，保留）");
        CHECK(Near(pi.activeFloatPnl, 9.9), "I2 浮盈同写");
    }

    // I3 被动腿：symKey 带交易所前缀，所以 GATEIO 的推送只落被动腿
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnBalance(MakeBalance(GATEIO, USDT_SWAP, "DOGE", 777.0, 1.1));
        CHECK(Near(pi.passiveRealPosition, 777.0), "I3 被动腿命中 -> 写入");
        CHECK(Near(pi.activeRealPosition, 0.0),    "I3 主动腿不受影响（交易所前缀不同）");
    }

    // I4 别的交易所不命中
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnBalance(MakeBalance(OKX, USDT_SWAP, "DOGE", 555.0, 0.0));
        CHECK(Near(pi.activeRealPosition, 0.0) && Near(pi.passiveRealPosition, 0.0),
              "I4 OKX 的资金推送不落到 BINANCE / GATEIO 腿上");
    }

    // I5 currency 为空 -> 不命中
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        fx.ctx.OnBalance(MakeBalance(BINANCE, USDT_SWAP, "", 999.0, 0.0));
        CHECK(Near(pi.activeRealPosition, 0.0), "I5 currency 为空 -> 不命中，不写");
    }
}

// ===========================================================================
// J. 账户总览推送（OnTotalAccount -> PairInfoManager::UpdateOnTotalAccount）
//
// ⚠️ UpdateOnTotalAccount 目前是**空实现**（函数体里什么都没有）。
//    本组用例把"它确实什么都没做"钉住 —— 将来接上真实逻辑时这里会先红。
// ===========================================================================
static void TestJ_TotalAccount() {
    Section("J. 账户总览推送（OnTotalAccount）");

    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        pi.pairTotalVolume     = 3.0;
        pi.activeRealPosition  = -3.0;
        pi.passiveRealPosition = -3.0;
        pi.errorFlag           = true;

        pubsub::TotalAccount ta;
        ta.totalEquity = 1e6;
        fx.ctx.OnTotalAccount(ta);

        CHECK(Near(pi.pairTotalVolume, 3.0),      "J1 UpdateOnTotalAccount 空实现 -> 账本不变");
        CHECK(Near(pi.activeRealPosition, -3.0),  "J1 主动腿实时持仓不变");
        CHECK(Near(pi.passiveRealPosition, -3.0), "J1 被动腿实时持仓不变");
        CHECK(pi.errorFlag,                       "J1 errorFlag 不变");
    }
}

// ===========================================================================
int main() {
    // 固定配置，让成本口径可预测
    SignalGenerator::Instance().SetConfig(FeeSlippageConfig{});
    RiskManager::Instance().SetConfig(RiskConfig{});
    // 快照用例的输出目录。强转 void：glibc 的 mkdir 可能带 warn_unused_result，
    // g++ -Wall 下会报 "ignoring return value"。
    (void)::mkdir("/tmp/ptsuite/out", 0755);

    TestA_Create();
    TestB_Open();
    TestC_Close();
    TestD_Timer();
    TestE_Cancel();
    TestF_Risk();
    TestG_Restart();
    TestH_Position();
    TestI_Balance();
    TestJ_TotalAccount();

    std::printf("\n=================================================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
