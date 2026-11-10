// ===========================================================================
// e2e 夹具 —— 每个功能一个 .cpp，共用这一个头
//
// 一个用例文件的全部骨架就三行：
//
//     #include "harness.h"
//     static void Case() { Fx fx; Boot(fx); /* 摆场景 + CHECK */ }
//     int main() { return RunCase("01 开仓", Case); }
//
// 这里只放**两个以上用例会用到**的东西。一个用例独有的编排写在它自己文件里
// （比如 ArmExposureAbnormal 只服务 06，就放在 06 里），免得夹具变成第二个 suite.cpp。
//
// 编译/运行别手敲，用同目录的 run.sh：`./run.sh 01` 或 `./run.sh`（全跑）。
//
// ---------------------------------------------------------------------------
// 三个必须先知道的事实（踩过，写在这里省得下次再查一遍）
//
// ① 夹具必须自己补生产 pre_start 的两件事：
//        SpreadManager::AddSpreadPara(pairKey) + dbpReader.Subscribe(pairKey)
//    （生产在 PairTradingStrategy.cpp:41-46）。不补 ⇒ 没有价差槽位 ⇒
//    GetSpread 返回 nullptr ⇒ 一条子单都报不出来。Boot() 里已经做了。
//
// ② 子单刚报出去是 OS_PEND，不是 OS_NEW。要测"撤单"必须先喂一条交易所确认
//    （OnOrder(..., OS_NEW, ...)），否则撤单分支的时间门槛跨不过去。
//
// ③ pairTotalVolume 是**主动腿持仓量，且符号反着**：负 = 对子多，正 = 对子空
//    （PairInfo::IsLong() 是 vol < -1e-9）。所以"平多"要喂 -5.0，
//    喂 +5.0 的话 IsLong() 为假，平仓信号根本不成立。
// ===========================================================================
#pragma once

// ---- 所有 std 头必须先包含完（下面要 #define private public）----
#include <algorithm>
#include <array>
#include <atomic>
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
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#define private public
#include "algo/AlgoContext.h"
#include "algo/PairTradingContext.h"
#include "basic/SpreadManager.h"
#include "basic/AccountManager.h"
#include "basic/LimitManager.h"
#include "basic/AlgoOrderManager.h"
#include "basic/AlgoPairOrder.h"
#include "basic/AlgoRebalanceOrder.h"
#include "basic/QuantTrade.h"
#include "signal/SignalGenerator.h"
#include "risk/RiskManager.h"
#undef private

using namespace pt;

// ===========================================================================
// 断言
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
static const char* g_caseName = "";

static void CHECK(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s   <<< %s\n", what, g_caseName);
    }
}

static bool Near(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) <= tol;
}

static const int64_t SEC  = 1000000LL;
static const int64_t MIN_ = 60LL * SEC;

// 用例的 main 一行写完：跑完打印本用例的 PASS/FAIL，失败返回非 0
static int RunCase(const char* name, void (*fn)()) {
    g_caseName = name;
    std::printf("=== %s ===\n", name);
    fn();
    std::printf("--- %s: PASS %d  FAIL %d\n", name, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

// ===========================================================================
// 固定标的
// ===========================================================================
static const char* kPairKey = "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT";
static const char* kActive  = "BINANCE.USDT_SWAP.DOGE-USDT";
static const char* kPassive = "GATEIO.USDT_SWAP.DOGE-USDT";
static const int kActiveAccountId  = 10000;
static const int kPassiveAccountId = 10001;

// ===========================================================================
// 临时文件
//
// run.sh 会把 E2E_TMP 指到 /tmp/utrade_e2e/<用例名>/ 并保证是空目录 ——
// 所以每个用例可以放心用固定文件名（pair_info.csv 等），不会互相串味。
// ===========================================================================
static std::string TempPath(const char* name) {
    const char* d = std::getenv("E2E_TMP");
    std::string base = d ? d : "/tmp/utrade_e2e";
    ::mkdir(base.c_str(), 0755);
    return base + "/" + name;
}

static bool WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

static std::string ReadTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ===========================================================================
// 世界重置
//
// 执行侧有几个跨用例不会自己清的单例（价差 / 资金 / 频率板 / 账户映射），
// 不清的话症状是"某条断言莫名其妙过了"，最难查。
// ===========================================================================
static void ResetWorld() {
    auto& pim = PairInfoManager::Instance();
    pim.m_pairInfoMap.clear();
    pim.m_instrToPairs.clear();
    pim.m_pairKeys.clear();
    pim.smc = nullptr;

    SpreadManager::Instance().mSpread.clear();
    SpreadManager::Instance().mInstEntry.clear();
    AccountManager::Instance().mAccount.clear();
    LimitManager::Instance().mAccountLimitBoard.clear();
    mAccountNameAccountId.clear();
}

// ===========================================================================
// 行情 / 合约
// ===========================================================================
static md::InstrumentInfo MakeInstrumentInfo(double value = 1.0,
                                             double tickSize = 0.0001,
                                             double minSize = 0.1) {
    md::InstrumentInfo info;
    info.value = value;
    info.tickSize = tickSize;
    info.lotSize = tickSize;
    info.minSize = minSize;
    info.calcType = 0;
    // FundVerify 用 margin 当资产名、base/quote 判方向，不填会去查一个不存在的资产
    std::strncpy(info.margin, "USDT", sizeof(info.margin) - 1);
    std::strncpy(info.quote, "USDT", sizeof(info.quote) - 1);
    std::strncpy(info.base, "DOGE", sizeof(info.base) - 1);
    return info;
}

static dbp::DbpTopic MakeTopic(const std::string& key) {
    dbp::DbpTopic t;
    std::strncpy(t.__name, key.c_str(), sizeof(t.__name) - 1);
    std::strncpy(t.activeInstrumentKey, kActive, sizeof(t.activeInstrumentKey) - 1);
    std::strncpy(t.passiveInstrumentKey, kPassive, sizeof(t.passiveInstrumentKey) - 1);
    return t;
}

// 一档盘口 + 四个价差口径。
// 盘口必须填满：GetTargetPairOrder 读 activeBid/Ask[0]、passiveBid/Ask[0]，
// 空的话目标价是 0，报单会被各种守卫拦掉。
// ⚠️ 被动腿盘口量必须**大于**主动腿：MAKER_TAKER 分支有一道守卫
//    （AlgoPairOrder.cpp:93-114）"被动腿要比主动腿厚"，相等会返回空单 ——
//    只在 MT 路径现形的假阴性。
static dbp::DbpData MakeData(double activeBid, double activeAsk,
                             double passiveBid, double passiveAsk,
                             double spreadBidAsk, double spreadBidBid,
                             double spreadAskBid, double spreadAskAsk,
                             int64_t ts, double activePriceTema = 100.0) {
    dbp::DbpData d;
    d.spreadEffective = true;
    d.statEffective = true;

    d.spreadBidAsk = spreadBidAsk;
    d.spreadBidBid = spreadBidBid;
    d.spreadAskBid = spreadAskBid;
    d.spreadAskAsk = spreadAskAsk;
    d.spreadBidAskTema = spreadBidAsk;
    d.spreadBidBidTema = spreadBidBid;
    d.spreadAskBidTema = spreadAskBid;
    d.spreadAskAskTema = spreadAskAsk;

    d.activeBidPrice[0] = activeBid;
    d.activeAskPrice[0] = activeAsk;
    d.activeBidVolume[0] = 10000.0;
    d.activeAskVolume[0] = 10000.0;

    d.passiveBidPrice[0] = passiveBid;
    d.passiveAskPrice[0] = passiveAsk;
    d.passiveBidVolume[0] = 30000.0;
    d.passiveAskVolume[0] = 30000.0;

    d.activePriceTema = activePriceTema;
    d.passivePriceTema = 100.0;

    d.activeDepthTs = ts;
    d.passiveDepthTs = ts;
    d.generateTs = ts;
    return d;
}

// 喂给**执行层**的行情（B/C/D 类用例）
static dbp::DbpData FreshData() {
    return MakeData(100.00, 100.01, 100.00, 100.01,
                    -1.0, -1.0, -1.0, -1.0, crypto::getCurrentTime());
}

// 喂给**策略层** OnSpread 的行情。
// 与 FreshData 的唯一区别：spreadAskAsk 不能是 -1.0 ——
// SignalGenerator::CanOpen 有一条 sanity（|spreadAskAsk| > 0.1 就拒开仓），
// 而策略层 OnSpread 会用这条行情覆盖 pi.rtSpread。
static dbp::DbpData FreshStrategyData() {
    return MakeData(100.00, 100.01, 100.00, 100.01,
                    -1.0, -0.001, -0.001, -0.001, crypto::getCurrentTime());
}

// 喂给 **MAKER_TAKER 拆单**的行情（14~16 用）。
//
// ⚠️ 拆单读的是 **DbpData 上的价差**，不是 pi.rtSpread —— 信号侧用 pi.rtSpread、
//    拆单侧用 pdata，是两份数据。所以这里必须把 MT 的轴显式喂进去：
//    开空/平多 = spreadBidBid，开多/平空 = spreadAskAsk。
//    用 FreshData()（四个口径全 -1）"凑巧"能让开多成立（-1 落进负阶梯），
//    但**平多会直接掉进空单分支**（tempSpread=-1 < startSpread=0 -> 量取 startVolume=0
//    -> `targetActiveVolume <= expectActiveVolume` -> 静默返回空单，一条子单都没有）。
static dbp::DbpData MtData(double spreadBidBid, double spreadAskAsk) {
    return MakeData(100.00, 100.01, 100.00, 100.01,
                    -1.0, spreadBidBid, -1.0, spreadAskAsk,
                    crypto::getCurrentTime());
}

// ===========================================================================
// 夹具
// ===========================================================================
struct Fx {
    // ---- 策略侧 ----
    PairTradingContext ptCtx;
    std::vector<BaseAlgoOrder*> created;   // 策略层建出来的算法单（所有权已转给 algoCtx）
    std::vector<std::pair<int64_t, stra::CommandType>> modifyCalls;

    // ---- 执行侧 ----
    AlgoContext algoCtx;
    om::TradeClient tradeClient{100031};   // 出向记录器（报单/撤单/查询全留在进程内）
    dbp::DbpReader dbpReader{"", ""};
    sm::SecurityManager smc;

    Fx() = default;
    Fx(const Fx&) = delete;
    Fx& operator=(const Fx&) = delete;

    // ⚠️ 不要 delete created 里的指针：SubmitAlgoOrder 一旦被调，算法单所有权就转给
    //    AlgoContext（AlgoOrderManager 的析构会 delete 它们），再 delete 就是 double free。
    //    created 只是"策略层报出了哪些单"的观察日志。

    // ---- 出向指令视图 ----
    std::vector<pubsub::TCommand>& sent() { return tradeClient.Sent(); }
    size_t NNew()    { return tradeClient.CountNewOrders(); }
    size_t NCancel() { return tradeClient.CountCancels(); }

    std::vector<const pubsub::NewOrder*> News() {
        std::vector<const pubsub::NewOrder*> v;
        for (const auto& c : tradeClient.Sent()) {
            if (c.cmdTypeEnum == pubsub::CMD_NEW_ORDER) v.push_back(&c.body.newOrder);
        }
        return v;
    }
    std::vector<const pubsub::CancelOrder*> Cancels() {
        std::vector<const pubsub::CancelOrder*> v;
        for (const auto& c : tradeClient.Sent()) {
            if (c.cmdTypeEnum == pubsub::CMD_CANCEL_ORDER) v.push_back(&c.body.cancelOrder);
        }
        return v;
    }

    // ---- 算法单视图 ----
    BaseAlgoOrder* AlgoOrder(int64_t id) {
        return algoCtx.alogOrderManager.SeletAlgoOrderByAlgoOrderId(id);
    }
    BaseAlgoOrder* Order0() { return created.empty() ? nullptr : created[0]; }

    std::vector<stra::QuantOrder> ChildOrders(BaseAlgoOrder* o) {
        std::vector<stra::QuantOrder> v;
        if (!o) return v;
        for (auto& kv : o->orderMgr.GetAllOrders()) v.push_back(kv.second);
        return v;
    }
    std::vector<PairOrder> PairOrders(BaseAlgoOrder* o) {
        std::vector<PairOrder> v;
        if (!o) return v;
        for (auto& kv : o->pairOrderMgr.GetAllPairOrders()) v.push_back(kv.second);
        return v;
    }
    size_t NActiveChild(BaseAlgoOrder* o) {
        size_t n = 0;
        for (const auto& c : ChildOrders(o)) if (c.isActiveOrder) ++n;
        return n;
    }
    size_t NPassiveChild(BaseAlgoOrder* o) {
        size_t n = 0;
        for (const auto& c : ChildOrders(o)) if (!c.isActiveOrder) ++n;
        return n;
    }

    PairInfo* pi() { return PairInfoManager::Instance().GetPairInfo(kPairKey); }
};

// ===========================================================================
// 启动
//
// keepState = true 时不删落盘文件 —— 12_restart 用它模拟"进程重启"：
// 第一个 Fx 写盘、析构，第二个 Fx 在**同一组路径**上 Init，就会把状态读回来。
// ===========================================================================
static void Boot(Fx& fx, bool keepState = false) {
    ResetWorld();
    fx.smc.Clear();
    fx.smc.Set(kActive, MakeInstrumentInfo());
    fx.smc.Set(kPassive, MakeInstrumentInfo());

    StrategyConfig::GetInstance().Clear();
    StrategyConfig::GetInstance().SetAccount(kActiveAccountId,  "test1", "test1", BINANCE, USDT_SWAP);
    StrategyConfig::GetInstance().SetAccount(kPassiveAccountId, "test1", "test1", GATEIO,  USDT_SWAP);
    mAccountNameAccountId["test1"] = kActiveAccountId;

    const std::string cfgPath   = TempPath("config.json");
    const std::string snapPath  = TempPath("pair_info.csv");
    const std::string statsPath = TempPath("spread_stats.csv");
    if (!keepState) {
        // 每个用例都从"没有配置 / 没有快照"开始。
        // 不删的话：上一个用例写的 pairCommands 会被这一轮 RunSignalOnce 读进来，
        // 上一个用例的快照会被 Init 的 LoadSnapshot 读回来 —— 串味，而且症状是断言"莫名过了"。
        ::remove(cfgPath.c_str());
        ::remove(snapPath.c_str());
        ::remove(statsPath.c_str());
    }

    PairTradingConfig cfg;
    cfg.pairKeys = {kPairKey};
    cfg.activeAccountId  = kActiveAccountId;
    cfg.passiveAccountId = kPassiveAccountId;
    cfg.spreadStatsMinSamples = 3;
    cfg.spreadSampleIntervalSec = 0;       // 逐 tick 全存，别降频
    cfg.csvStatePath = snapPath;
    cfg.spreadStatsStatePath = statsPath;
    cfg.configPath = cfgPath;

    fx.ptCtx.SetAlgoCommandCallback([&fx](BaseAlgoOrder* o) {
        fx.created.push_back(o);
        // 链路关键一步：策略层建好的算法单立刻注册进执行层
        fx.algoCtx.SubmitAlgoOrder(o);
    });
    fx.ptCtx.SetAlgoOrderModifyCallback([&fx](int64_t id, stra::CommandType c, const stra::AlgoOrderModify* m) {
        fx.modifyCalls.emplace_back(id, c);
        fx.algoCtx.SubmitAlgoOrder(id, c, m);
    });
    fx.ptCtx.Init(cfg, &fx.smc);

    fx.algoCtx.Init(&fx.smc);
    fx.algoCtx.SetTradeClient(&fx.tradeClient);
    fx.algoCtx.SetDbp(&fx.dbpReader);
    fx.tradeClient.Clear();

    // 生产 pre_start 的两件事，夹具必须自己补（见文件头 ①）
    SpreadManager::Instance().AddSpreadPara(kPairKey);
    fx.dbpReader.Subscribe(kPairKey);
}

// 跳过启动对账，直接进交易态
static void GoTrading(Fx& fx) {
    fx.ptCtx.m_phase = PairTradingContext::StartupPhase::Trading;
    fx.ptCtx.m_startupTimeUs = 1;
}

// 给执行侧注入资金（走真实的 OnBalance 入口）
static void SeedFunds(Fx& fx, double usdt = 1e6) {
    pubsub::Balance b;
    b.instTypeEnum = USDT_SWAP;
    b.accountId = kActiveAccountId;
    b.exchangeTypeEnum = BINANCE;
    std::strncpy(b.currency, "USDT", sizeof(b.currency) - 1);
    b.total = usdt;
    fx.algoCtx.OnBalance(b);

    b.accountId = kPassiveAccountId;
    b.exchangeTypeEnum = GATEIO;
    fx.algoCtx.OnBalance(b);
}

// 把子单"变老" us 微秒 —— 用来跨过撤单的时间门槛，比 sleep 稳
static void AgeChildOrders(BaseAlgoOrder* o, int64_t us) {
    if (!o) return;
    for (auto& kv : o->orderMgr.GetAllOrders()) kv.second.updateTime -= us;
}

// ===========================================================================
// 策略侧：开关编排
// ===========================================================================
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

// 摆一个"开仓信号一定成立"的局面：只开 which，价差刚好越过阈值。
static void ArmOpen(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    std::string w(which);
    if (w == "ttOL") { pi.rtSpread.spreadBidAsk = -1.0; pi.orderParams.ttOLStartSpread = 0.0; pi.orderParams.ttOLEndVolume = -1.0; }
    if (w == "ttOS") { pi.rtSpread.spreadAskBid =  1.0; pi.orderParams.ttOSStartSpread = 0.0; pi.orderParams.ttOSEndVolume =  1.0; }
    // mtOL 用 spreadAskAsk 判方向，这个值要同时过三道闸，区间很窄：
    //   ① CanOpen 的 sanity  |spreadAskAsk| <= 0.1
    //   ② 方向闸             spreadAskAsk + F_mt < mtOLStartSpread
    //   ③ 净利闸             (mtCLStartSpread - spreadAskAsk - F_mt) >= minProfitTarget
    // -0.01 三条都过。填 0 会卡在③，填 -1.0 会卡在①。
    if (w == "mtOL") { pi.rtSpread.spreadAskAsk = -0.01; pi.orderParams.mtOLStartSpread = 0.05; pi.orderParams.mtOLEndVolume = -1.0; }
    if (w == "mtOS") { pi.rtSpread.spreadBidBid =  1.0; pi.orderParams.mtOSStartSpread = 0.0;  pi.orderParams.mtOSEndVolume =  1.0; }
}

// 摆一个"平仓信号一定成立"的局面。vol 用 PairInfo 口径（负 = 多）。
static void ArmClose(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    std::string w(which);
    if (w == "ttCL") { pi.rtSpread.spreadAskBid = 1.0;  pi.orderParams.ttCLStartSpread = 0.0; pi.orderParams.ttCLEndVolume = 0.0; }
    if (w == "ttCS") { pi.rtSpread.spreadBidAsk = -1.0; pi.orderParams.ttCSStartSpread = 0.0; pi.orderParams.ttCSEndVolume = 0.0; }
    if (w == "mtCL") { pi.rtSpread.spreadBidBid = 1.0;  pi.orderParams.mtCLStartSpread = 0.0; pi.orderParams.mtCLEndVolume = 0.0; }
    if (w == "mtCS") { pi.rtSpread.spreadAskAsk = -1.0; pi.orderParams.mtCSStartSpread = 0.0; pi.orderParams.mtCSEndVolume = 0.0; }
}

// ===========================================================================
// MAKER_TAKER（MT）专用编排 —— 14~16 用
//
// MT 与 TT 在**拆单侧**（AlgoPairOrder::GetTargetPairOrder）有三处不同，任何一处配错，
// 症状都一样：信号成立、ProcessPairSignal 也派发了算法单，但**一条子单都报不出来**，
// 日志里只有一句 `targetActiveVolume <= expectActiveVolume or targetActiveVolume <= 0`。
//   ① 价差轴不同：MT 开多/平空用 spreadAskAsk，开空/平多用 spreadBidBid；
//      TT 是 bidAsk / askBid。轴用错时 tempSpread 落在阶梯外面。
//   ② 执行成本不同：MT 扣 F_mt（maker 费率），TT 扣 F_tt（taker 费率）。
//   ③ 报单类型不同：MT 主动腿恒 OT_POST_ONLY，目标价取**对手价**（买挂 bid、卖挂 ask），
//      再由 CreateActiveOrder 夹一档（买 <= ask-tick、卖 >= bid+tick）。
//      所以 MT 子单价永远贴着盘口内一档，而 TT 是穿价。
// 开关判据的方向也和 TT 相反，跟 pairTotalVolume 的多负空正一致：
//   开多 vol > mtOLEndVolume（负值）  开空 vol < mtOSEndVolume（正值）
//   平多 vol < mtCLEndVolume          平空 vol > mtCSEndVolume
//
// 阈值刻意写死成一串常数（不依赖 OrderParams 的默认值）：阶梯的 Start/End 必须与
// 价差轴同号，靠默认值 0 凑巧成立的话，改一次默认值整个用例就变成假绿。
// ===========================================================================
static void ArmMtOpen(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    auto& op = pi.orderParams;
    const std::string w(which);

    if (w == "mtOL") {
        // 信号: spreadAskAsk + F_mt < mtOLStartSpread 且 vol > mtOLEndVolume
        // 阶梯: tempSpread < mtOLEndSpread -> 量取 mtOLEndVolume（开多终点是负）
        // 净利: mtCLStartSpread - spreadAskAsk - F_mt >= minProfitTarget
        pi.rtSpread.spreadAskAsk = -0.02;
        op.mtOLStartSpread =  0.0;
        op.mtOLEndSpread   = -0.001;
        op.mtOLStartVolume =  0.0;
        op.mtOLEndVolume   = -1.0;
        op.mtCLStartSpread =  0.0;   // 只服务净利闸，别删
    }
    else if (w == "mtOS") {
        pi.rtSpread.spreadBidBid = 0.02;
        op.mtOSStartSpread = 0.0;
        op.mtOSEndSpread   = 0.001;
        op.mtOSStartVolume = 0.0;
        op.mtOSEndVolume   = 1.0;
        op.mtCSStartSpread = 0.0;    // 只服务净利闸
    }
}

static void ArmMtClose(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    auto& op = pi.orderParams;
    const std::string w(which);

    if (w == "mtCL") {              // 平多：vol 是负
        pi.rtSpread.spreadBidBid = 0.02;
        op.mtCLStartSpread = 0.0;
        op.mtCLEndSpread   = 0.001;
        op.mtCLStartVolume = 0.0;
        op.mtCLEndVolume   = 0.0;   // 平到 0 才算完
    }
    else if (w == "mtCS") {         // 平空：vol 是正
        pi.rtSpread.spreadAskAsk = -0.02;
        op.mtCSStartSpread = 0.0;
        op.mtCSEndSpread   = -0.001;
        op.mtCSStartVolume = 0.0;
        op.mtCSEndVolume   = 0.0;
    }
}

// 报单量参数就绪（否则 BuildAlgoOrderJson 会因为 targetVolume <= 0 直接返回 nullptr）
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

// 走真实链路建一张 ttOL 开仓单
static BaseAlgoOrder* OpenTtOL(Fx& fx) {
    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmOpen(pi, "ttOL", 0.0);
    fx.ptCtx.ProcessPairSignal(pi);
    return fx.created.empty() ? nullptr : fx.created.back();
}

// ===========================================================================
// 回报构造
// ===========================================================================
static pubsub::OrderResponse MakeRtn(int64_t clientOrderId,
                                     const char* strategyRef,
                                     OrderStatus st,
                                     double tradedVolume,
                                     double tradePrice,
                                     ExchangeType exch = BINANCE,
                                     InstType inst = USDT_SWAP,
                                     ApiSource api = AS_ADD_NEW_ORDER) {
    pubsub::OrderResponse r;
    std::memset(&r, 0, sizeof(r));
    r.exchangeTypeEnum = exch;
    r.instTypeEnum = inst;
    r.clientOrderId = clientOrderId;
    std::strncpy(r.strategyRef, strategyRef, sizeof(r.strategyRef) - 1);
    r.orderStatus = st;
    r.volumeTraded = tradedVolume;
    r.tradePrice = tradePrice;
    r.fillPrice = tradePrice;
    r.tradeDiff = tradedVolume;
    std::strncpy(r.orderSysId, "SYS-1", sizeof(r.orderSysId) - 1);
    std::strncpy(r.orderId, "EX-1", sizeof(r.orderId) - 1);
    r.updateTime = crypto::getCurrentTime();
    r.apiSourceEnum = api;
    return r;
}

// "algoOrderId_pairId" —— QuantTrade::CreateOrder 里就是 sprintf 成这个格式
static std::string MakeRef(BaseAlgoOrder* o, int64_t pairId) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld_%lld",
                  (long long)o->algoOrderId, (long long)pairId);
    return std::string(buf);
}

// ===========================================================================
// 运行时配置（热改）—— 09~13 用
//
// 走的是生产同一条路：写 config.json -> T_signal 的 stat 指纹发现变了
// -> ReadRuntimeConfigFile -> m_cfgQ.push -> OnTimer 0.6 落地。
// **不要直接写 pi.stopFlag** —— 那样解析层和闸门都没被测到。
//
// ⚠️ OnTimer 第 1 步 RecalcVolumeParams 会按 cfg.maxAmount / targetAmount **重算**
//    maxVolume / ttTargetVolume，把夹具 MakeHoldable 手摆的那组值覆盖掉
//    （实测 maxAmount=50、腿价 100 时 10 / 10 -> 5 / 0.5）。所以 OnTimer 之后要重新
//    MakeHoldable，否则后面依赖报单量和阶梯末端的断言测的就不是你以为的那组数。
// ===========================================================================
// ⚠️ 写文件之后必须让指纹失效，不能指望 mtime 变化。
//    RunSignalOnce 只在 (ino, mtimeNs, size) 变了才重读文件，而本函数会连着写两次
//    **等长**的内容（cmdType 1 -> 3，都是 122 字节）。Linux 的文件时间戳取自粗粒度时钟
//    （1 jiffy ≈ 1~4 ms），同一个 tick 内两次写的 mtime 完全相同 ⇒ 指纹不变 ⇒
//    第二次配置被**静默跳过**。macOS 的 APFS 是 ns 级，所以本机看不出来 ——
//    症状是部署机上 11 号用例从 10/0 变成 4/5（2026-11-09 实测复现）。
//    测试要的是"这条配置现在生效"，不是"验证指纹检测"，所以这里直接作废指纹。
//    产品侧不受影响：1 Hz 轮询下"读完 1~4 ms 内又改一次"的概率可以忽略。
static void ApplyRuntimeConfig(Fx& fx, const std::string& json) {
    WriteTextFile(TempPath("config.json"), json);
    fx.ptCtx.m_cfgStamp.valid = false;
    int64_t lastStats = crypto::getCurrentTime();   // 别顺手触发统计重算
    fx.ptCtx.RunSignalOnce(crypto::getCurrentTime(), lastStats);
    fx.ptCtx.OnTimer(crypto::getCurrentTime());
}

// 一条针对 kPairKey 的 pairCommands 配置（完整 config.json 文本）
// extraFields 直接拼在 cmdType 后面，例如 ",\"profitPct\":-0.002,\"closeRemainVolume\":3"
static std::string PairCmdJson(int cmdType, const std::string& extraFields = "") {
    return std::string("{\"op\":{\"pairTrading\":{\"pairCommands\":[{\"pairKey\":\"") +
           kPairKey + "\",\"cmdType\":" + std::to_string(cmdType) + extraFields + "}]}}}";
}

// 全局开关配置，name 只能是 "closeOnly" / "stopAllAlgo"
static std::string GlobalSwitchJson(const char* name, bool on) {
    return std::string("{\"op\":{\"pairTrading\":{\"") + name + "\":" + (on ? "true" : "false") + "}}}";
}
