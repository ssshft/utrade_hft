// ===========================================================================
// 算法单执行（拆单 / 成交 / 撤单 / 风控平仓）+ 全链路串联
//
// 与 ../market_to_algo 的分工：
//   market_to_algo —— 策略侧「行情 -> 创建算法单」。执行端（AlgoContext / BaseAlgoOrder /
//                     PositionManager / OrderManager / PairOrderManager）在那里全是桩。
//   algo_exec      —— 执行侧「算法单 -> 拆子单 -> 报单 -> 回报 -> 更新 -> 撤单」，
//                     外加一条把两边串起来的循环（G 组）。
//
// 这里编译的是**真实的**执行侧全部源码：
//   algo/AlgoContext.cpp            执行侧总入口（OnSpread / OnOrder / OnTimer）
//   basic/BaseAlgoOrder.cpp         算法单基类：撤单 / 成交累加 / 拆单
//   basic/AlgoPairOrder.cpp         配对单：目标单计算 / 目标价 / 目标量
//   basic/PairManager.cpp           PairOrder + PairOrderManager
//   basic/PositionManager.cpp       持仓 / 冻结 / 浮盈
//   basic/OrderManager.cpp          子单簿
//   basic/LimitManager.cpp + LimitBoard.cpp   每秒报单/撤单频率限制
//   basic/AccountManager.cpp        资金 / 持仓 / 验资
//   basic/SpreadManager.cpp         价差 BBO
//   basic/AlgoOrderManager.cpp      算法单簿
//   basic/DataStruct.h              真实头（不再是桩）
//
// 唯一的出向边界是 om::TradeClient —— 换成记录器，报单/撤单/查询全部可逐字段断言。
// 外部依赖（fmt / json / dbp / pubsub / securitymanager / log_engine / time_util /
// program_util / command_helper）与策略侧共用 ../market_to_algo/stubs/ext。
//
// 全部在进程内：不读配置、不起 dbprocess / tb、不碰共享内存、不联网。
//
// ---------------------------------------------------------------------------
// 一个贯穿全篇的事实（读代码前先记住，否则 B 组会看不懂）：
//
//   策略层建的是 **TAKER_TAKER** 的 ttOL 算法单（PairInfo::autoFlag 默认 true ->
//   algoMode = "TT"）。落到 AlgoContext::OnSpread 里，它走的是 TT 分支，
//   CreatePairOrder(TAKER_TAKER) -> GetTargetPairOrder(TAKER_TAKER, OPEN_LONG)。
//
//   OPEN_LONG 在这个项目里的语义是「做多**这一对**」，落腿方向是：
//       主动腿（BINANCE）= DT_SHORT    被动腿（GATEIO）= DT_LONG
//   主动腿 OT_MARKET（TT 主动腿吃单），被动腿也 OT_MARKET（被动腿永远是吃单腿）。
//   这不是笔误，是配对交易的腿约定；B1 会把这些逐字段钉住。
//
//   同一时刻 MT 分支也会被进入一次（两个 if 是并列的，不是 else if），
//   但 mtOLSwitch/mtOSSwitch 都是 false -> CreatePairOrder(MAKER_TAKER) 内部
//   一个 GetTargetPairOrder 都不调，pairOrder.pairId 保持 -1，于是不报单。
//   所以 B1 只应该看到 **一条** 出向报单。
// ===========================================================================

// ---- 所有 std 头必须先包含完（#define private public 的要求，见 README）----
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

// ⚠️ `#define private public` 必须在**所有 std 头包含完之后**才生效。
//    原因见 ../market_to_algo/README.md「怎么绕过 private」。
#define private public
#include "algo/AlgoContext.h"
#include "algo/PairTradingContext.h"
// AlgoContext.h 只声明了它自己的成员，执行侧那几个单例要显式包含 —— 而且必须在宏生效
// 期间包含，才能清它们的私有容器（见 ResetWorld）。AlgoContext.cpp 里也是各自 include 的。
#include "basic/SpreadManager.h"
#include "basic/AccountManager.h"
#include "basic/LimitManager.h"
#include "basic/AlgoOrderManager.h"
#include "basic/AlgoPairOrder.h"
#include "basic/QuantTrade.h"
#undef private

// PairTradingContext.h 把整套策略侧类型包在 namespace pt 里（PairInfo / PairInfoManager /
// PairTradingConfig / PairTradingContext ...），与 ../market_to_algo/suite.cpp 一样放开。
using namespace pt;

// ===========================================================================
// 断言框架
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
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

static bool Near(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) <= tol;
}

static const int64_t SEC  = 1000000LL;
static const int64_t MIN_ = 60LL * SEC;
// 与 ../market_to_algo 的时长常量保持同构；本套件暂时用不到 HOUR，
// 显式标 [[maybe_unused]] 而不是删掉，免得下次要对齐两边时又要找回来。
[[maybe_unused]] static const int64_t HOUR = 60LL * MIN_;

static const char* kPairKey = "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT";
static const char* kActive = "BINANCE.USDT_SWAP.DOGE-USDT";
static const char* kPassive = "GATEIO.USDT_SWAP.DOGE-USDT";

static const int kActiveAccountId  = 10000;
static const int kPassiveAccountId = 10001;

// ===========================================================================
// 世界重置
//
// 执行侧引入了四个单例（SpreadManager / AccountManager / LimitManager /
// StrategyConfig）+ 一个全局（mAccountNameAccountId），它们**跨用例不会自己清**。
// 不清干净的话，前一个用例留下的 BBO / 资金 / 频率板会污染后一个用例，
// 而且症状通常是"某条断言莫名其妙过了"而不是失败 —— 最难查的那种。
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
// 合约信息
// ===========================================================================
static md::InstrumentInfo MakeInstrumentInfo(double value = 1.0,
                                             double tickSize = 0.0001,
                                             double minSize = 0.1,
                                             int calcType = 0) {
    md::InstrumentInfo info;
    info.value = value;
    info.tickSize = tickSize;
    info.lotSize = tickSize;
    info.minSize = minSize;
    info.calcType = calcType;
    // 执行侧的 FundVerify 用 info.margin 当资产名（USDT 本位合约就是 USDT），
    // 用 info.base / info.quote 判方向。不填的话验资会去查一个不存在的资产。
    std::strncpy(info.margin, "USDT", sizeof(info.margin) - 1);
    std::strncpy(info.quote, "USDT", sizeof(info.quote) - 1);
    std::strncpy(info.base, "DOGE", sizeof(info.base) - 1);
    return info;
}

// ===========================================================================
// 行情构造
// ===========================================================================
static dbp::DbpTopic MakeTopic(const std::string& key) {
    dbp::DbpTopic t;
    std::strncpy(t.__name, key.c_str(), sizeof(t.__name) - 1);
    std::strncpy(t.activeInstrumentKey, kActive, sizeof(t.activeInstrumentKey) - 1);
    std::strncpy(t.passiveInstrumentKey, kPassive, sizeof(t.passiveInstrumentKey) - 1);
    return t;
}

// 一档盘口 + 四个价差口径。
// 执行侧的 AlgoPairOrder::GetTargetPairOrder 会读 activeBid/Ask、passiveBid/Ask 的 [0] 档，
// 所以这里必须把盘口填满，否则拆出来的目标价是 0，报单会被各种守卫拦掉。
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
    // ⚠️ 被动腿的盘口量必须**大于**主动腿，不能一样。
    //    AlgoPairOrder::GetTargetPairOrder 在 MAKER_TAKER 分支有一道守卫
    //    （AlgoPairOrder.cpp:93-114）：
    //        activeAskAmount1 = activeAskVolume[0] * activeAskPrice[0] * value
    //        passiveAskAmount1 = passiveAskVolume[0] * passiveAskPrice[0] * value
    //        if (activeAskAmount1 >= passiveAskAmount1) return pairOrder;   // 返回空单
    //    语义是"被动腿的盘口必须比主动腿厚，否则挂 maker 腿等于让主动腿去吃掉自己"。
    //    两腿量填成一样时 activeAskAmount1 == passiveAskAmount1，守卫命中，
    //    表现是 MT 单一条子单都报不出来（而 TT 单没有这道守卫，照报不误）——
    //    一个只在 MT 路径上现形的假阴性。
    d.passiveBidVolume[0] = 30000.0;
    d.passiveAskVolume[0] = 30000.0;

    d.activePriceTema = activePriceTema;
    d.passivePriceTema = 100.0;

    d.activeDepthTs = ts;
    d.passiveDepthTs = ts;
    d.generateTs = ts;
    return d;
}

// 一段"刚刚发生"的行情：时间戳取当前，且买卖价差足够宽，能过 CreatePairOrder 的宽度守卫。
// B/C/D/E/F 组用这个。
static dbp::DbpData FreshData() {
    return MakeData(100.00, 100.01, 100.00, 100.01,
                    -1.0, -1.0, -1.0, -1.0, crypto::getCurrentTime());
}

// 喂给**策略层** PairTradingContext::OnSpread 的行情。
//
// 和 FreshData 的区别只有一个：spreadAskAsk 不能是 -1.0。
// SignalGenerator::CanOpen 有一条行情 sanity（SignalGenerator.cpp:266-270）：
//     double s = pi.rtSpread.spreadAskAsk;
//     if (!std::isnan(s) && std::abs(s) > 0.1) -> "spreadAskAsk abnormal > 10%" -> 拒绝开仓
// PairTradingContext::OnSpread 会用 pdata 覆盖 pi.rtSpread，所以把四个口径全填 -1.0
// 会让策略层直接不开仓 —— 而 ttOL 真正需要的是 spreadBidAsk（-1.0），
// 另外三个口径留在正常范围即可。
static dbp::DbpData FreshStrategyData() {
    return MakeData(100.00, 100.01, 100.00, 100.01,
                    -1.0, -0.001, -0.001, -0.001, crypto::getCurrentTime());
}

// ===========================================================================
// 夹具
// ===========================================================================
struct Fx {
    // ---- 策略侧 ----
    PairTradingContext ptCtx;
    std::vector<BaseAlgoOrder*> created;          // 策略层建出来的算法单（所有权已转给 algoCtx）
    std::vector<std::pair<int64_t, stra::CommandType>> modifyCalls;

    // ---- 执行侧 ----
    AlgoContext algoCtx;
    om::TradeClient tradeClient{100031};          // 出向记录器
    dbp::DbpReader dbpReader{"", ""};

    sm::SecurityManager smc;

    Fx() = default;
    Fx(const Fx&) = delete;
    Fx& operator=(const Fx&) = delete;
    ~Fx() = default;
    // ⚠️ 这里**不能** delete created 里的指针。
    //    AlgoOrderManager 是 AlgoContext 的**成员**（不是指针），而
    //    AlgoOrderManager::~AlgoOrderManager() 会把 mAlgoOrder 里每一个算法单 delete 掉
    //    （AlgoOrderManager.cpp:9-17）。也就是说：
    //        SubmitAlgoOrder(p) 一旦被调用，算法单的**所有权就转移给了 AlgoContext**。
    //    Fx 再 delete 一遍就是 double free（症状是进程直接 abort，栈上看不到原因）。
    //    created 只作为"策略层报出了哪些单"的观察日志。
    //    成员声明顺序也保证了析构顺序是对的：algoCtx 先析构，created 只是 vector。

    // ---- 出向指令视图 ----
    std::vector<pubsub::TCommand>& sent() { return tradeClient.Sent(); }
    size_t NNew()    { return tradeClient.CountNewOrders(); }
    size_t NCancel() { return tradeClient.CountCancels(); }
    size_t NQuery()  { return tradeClient.CountQueries(); }

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
    // 出向报单里 clientOrderId == soid 的那一条
    const pubsub::NewOrder* NewBySoid(int64_t soid) {
        for (const auto* n : News()) {
            if (n->clientOrderId == soid) return n;
        }
        return nullptr;
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

    // 子单里主动腿 / 被动腿各几条
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
// 世界启动
// ===========================================================================
static void Boot(Fx& fx) {
    ResetWorld();
    fx.smc.Clear();
    fx.smc.Set(kActive, MakeInstrumentInfo());
    fx.smc.Set(kPassive, MakeInstrumentInfo());

    // 执行侧的 StrategyConfig：账户 -> strategyId 的映射，出向指令上盖的就是它
    StrategyConfig::GetInstance().Clear();
    StrategyConfig::GetInstance().SetAccount(kActiveAccountId,  "test1", "test1", BINANCE, USDT_SWAP);
    StrategyConfig::GetInstance().SetAccount(kPassiveAccountId, "test1", "test1", GATEIO,  USDT_SWAP);

    // LimitManager::Init 遍历的是这个全局表（AlgoContext.cpp 里定义）
    mAccountNameAccountId["test1"] = kActiveAccountId;

    // ---- 策略侧 ----
    PairTradingConfig cfg;
    cfg.pairKeys = {kPairKey};
    cfg.activeAccountId  = kActiveAccountId;
    cfg.passiveAccountId = kPassiveAccountId;
    cfg.spreadStatsMinSamples = 3;
    cfg.spreadSampleIntervalMs = 0;
    fx.ptCtx.SetAlgoCommandCallback([&fx](BaseAlgoOrder* o) {
        fx.created.push_back(o);
        // ★ 链路的关键一步：策略层建好的算法单，立刻注册进执行层
        fx.algoCtx.SubmitAlgoOrder(o);
    });
    fx.ptCtx.SetAlgoOrderModifyCallback([&fx](int64_t id, stra::CommandType c, const stra::AlgoOrderModify* m) {
        fx.modifyCalls.emplace_back(id, c);
        fx.algoCtx.SubmitAlgoOrder(id, c, m);
    });
    fx.ptCtx.Init(cfg, &fx.smc);

    // ---- 执行侧 ----
    fx.algoCtx.Init(&fx.smc);
    fx.algoCtx.SetTradeClient(&fx.tradeClient);
    fx.algoCtx.SetDbp(&fx.dbpReader);
    fx.tradeClient.Clear();
}

// 策略侧启动闸门打开（跳过对账）
static void GoTrading(Fx& fx) {
    fx.ptCtx.m_phase = PairTradingContext::StartupPhase::Trading;
    fx.ptCtx.m_startupTimeUs = 1;
}

// 给执行侧的账户注入资金（走真实的 OnBalance 入口，不直接改内部结构）
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

// 把算法单的每个子单都"变老" us 微秒 —— 用来跨过超时撤单的时间门槛，
// 比 sleep 稳（不受 CI 负载影响，也不拖慢用例）。
static void AgeChildOrders(BaseAlgoOrder* o, int64_t us) {
    if (!o) return;
    for (auto& kv : o->orderMgr.GetAllOrders()) kv.second.updateTime -= us;
}

// ===========================================================================
// 策略侧辅助（与 ../market_to_algo/suite.cpp 同源）
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

// 平仓方向的开关编排。当前 A~H 都只跑开仓方向，但这条 helper 是"策略侧开关编排"
// 的完整一半（ArmOpen 的另一半），删掉会让下一个要加平仓用例的人从头猜一遍。
[[maybe_unused]] static void ArmClose(PairInfo& pi, const char* which, double vol) {
    OnlySwitch(pi, which);
    pi.rtSpread.valid = true;
    pi.pairTotalVolume = vol;
    std::string w(which);
    if (w == "ttCL") { pi.rtSpread.spreadAskBid = 1.0; pi.orderParams.ttCLStartSpread = 0.0; pi.orderParams.ttCLEndVolume = 0.0; }
    if (w == "ttCS") { pi.rtSpread.spreadBidAsk = -1.0; pi.orderParams.ttCSStartSpread = 0.0; pi.orderParams.ttCSEndVolume = 0.0; }
    if (w == "mtCL") { pi.rtSpread.spreadBidBid = 1.0; pi.orderParams.mtCLStartSpread = 0.0; pi.orderParams.mtCLEndVolume = 0.0; }
    if (w == "mtCS") { pi.rtSpread.spreadAskAsk = -1.0; pi.orderParams.mtCSStartSpread = 0.0; pi.orderParams.mtCSEndVolume = 0.0; }
}

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

// 建单：策略层报出一张 ttOL 算法单，经回调注册进 AlgoContext
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

// "algoOrderId_pairId" —— QuantTrade::CreateOrder 里 sprintf(ref, "%ld_%ld", algoPairId, pairId)
static std::string MakeRef(BaseAlgoOrder* o, int64_t pairId) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld_%lld",
                  (long long)o->algoOrderId, (long long)pairId);
    return std::string(buf);
}

// ===========================================================================
// A. 执行侧注册：策略层建好的算法单交给 AlgoContext
// ===========================================================================
static void TestA_Register() {
    Section("A. 注册（SubmitAlgoOrder：入册 / 订阅价差 / 初始状态）");

    // A1 建单即入册 + 订阅价差
    {
        Fx fx; Boot(fx); GoTrading(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);

        CHECK(fx.created.size() == 1, "A1 策略层报出一单");
        if (o) {
            CHECK(o->algoOrderId != 0, "A1 算法单拿到 algoOrderId");
            CHECK(fx.AlgoOrder(o->algoOrderId) == o, "A1 已在执行侧入册（同一个对象指针）");
            CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "A1 初始状态 ALGO_OS_NEW");
            CHECK(o->commandType == stra::CommandType_TRADING, "A1 commandType = TRADING");
            CHECK(o->algoType == stra::AlgoType_PairTrading, "A1 algoType = PairTrading");

            // 订阅：执行侧在入册时把 pairInstrumentKey 挂到 dbp 上
            bool subscribed = false;
            for (const auto& s : fx.dbpReader.subscribed) {
                if (s == o->pairInstrumentKey) subscribed = true;
            }
            CHECK(subscribed, "A1 已向 dbp 订阅该价差");
            CHECK(SpreadManager::Instance().IsPairInstrumentKeyExist(o->pairInstrumentKey),
                  "A1 SpreadManager 里也建好了价差槽位");
        }
    }

    // A2 重复注册同一个 pairInstrumentKey 不重复订阅
    {
        Fx fx; Boot(fx); GoTrading(fx);
        BaseAlgoOrder* o1 = OpenTtOL(fx);
        size_t n = fx.dbpReader.subscribed.size();

        AlgoPairOrder* o2 = new AlgoPairOrder();
        *static_cast<BaseAlgoOrder*>(o2) = *o1;
        o2->algoOrderId = 999;
        fx.algoCtx.SubmitAlgoOrder(o2);   // 所有权转移给 algoCtx，不能再 delete
        CHECK(fx.dbpReader.subscribed.size() == n, "A2 价差已存在 -> 不重复订阅");
        CHECK(fx.AlgoOrder(999) == o2, "A2 但仍然入册");
    }

    // A3 空指针安全
    {
        Fx fx; Boot(fx);
        fx.algoCtx.SubmitAlgoOrder(nullptr);
        CHECK(true, "A3 SubmitAlgoOrder(nullptr) 不崩");
    }

    // A4 变更路径：改一个不存在的 id 不产生任何副作用
    {
        Fx fx; Boot(fx); GoTrading(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        auto before = o->algoOrderStatus;
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId + 12345, stra::CommandType_CANCEL, nullptr);
        CHECK(o->algoOrderStatus == before, "A4 改一个不存在的 id -> 在册单不受影响");
    }
}

// ===========================================================================
// B. 拆单报单（OnSpread -> CreatePairOrder -> CreateActiveOrder -> CreateOrder）
// ===========================================================================
static void TestB_SplitAndPlace() {
    Section("B. 拆单报单（OnSpread）");

    // B1 行情有效 -> 拆出主动腿子单并报出去，逐字段核对
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        CHECK(o != nullptr, "B1 先有算法单");
        if (!o) return;

        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);

        CHECK(fx.NNew() == 1, "B1 行情有效 -> 恰好报出 1 条子单");
        if (fx.NNew() != 1) {
            std::printf("      [诊断] 出向指令数=%zu（报单 %zu / 撤单 %zu / 查询 %zu）\n",
                        fx.sent().size(), fx.NNew(), fx.NCancel(), fx.NQuery());
            return;
        }

        // 子单簿
        auto kids = fx.ChildOrders(o);
        CHECK(kids.size() == 1, "B1 子单已登记进算法单的 orderMgr");
        CHECK(fx.NActiveChild(o) == 1, "B1 该子单是主动腿");
        if (kids.size() == 1) {
            CHECK(kids[0].orderStatus == OS_PEND, "B1 子单初始状态 OS_PEND（等待交易所）");
            CHECK(Near(kids[0].volume, 1.0), "B1 子单量 = 1.0（受 min(targetVolume, ttTargetVolume) 约束）");
            CHECK(kids[0].isActiveOrder, "B1 isActiveOrder = true");
        }

        // 配对单簿
        auto pos = fx.PairOrders(o);
        CHECK(pos.size() == 1, "B1 pairOrder 已登记进 pairOrderMgr");
        if (pos.size() == 1) {
            CHECK(pos[0].tradingTypeOrder == stra::TAKER_TAKER, "B1 配对单 tradingTypeOrder = TAKER_TAKER");
            CHECK(pos[0].tradingTypeOffset == stra::OPEN_LONG, "B1 配对单 tradingTypeOffset = OPEN_LONG");
            CHECK(pos[0].activeDirection == DT_SHORT, "B1 主动腿方向 = DT_SHORT（OPEN_LONG 的腿约定）");
            CHECK(pos[0].passiveDirection == DT_LONG, "B1 被动腿方向 = DT_LONG");
            CHECK(pos[0].algoPairId == o->algoOrderId, "B1 配对单 algoPairId == algoOrderId");
        }

        // 出向指令逐字段
        const pubsub::NewOrder* n = fx.News()[0];
        CHECK(n->exchangeTypeEnum == BINANCE, "B1 出向: 交易所 = BINANCE（主动腿）");
        CHECK(std::strcmp(n->instId, "DOGE-USDT") == 0, "B1 出向: instId = DOGE-USDT");
        CHECK(n->offsetFlag == OF_OPEN, "B1 出向: offsetFlag = OF_OPEN");
        CHECK(n->direction == DT_SHORT, "B1 出向: direction = DT_SHORT");
        CHECK(n->orderType == OT_MARKET, "B1 出向: orderType = OT_MARKET（TT 主动腿吃单）");
        CHECK(Near(n->volumeTotal, 1.0), "B1 出向: volume = 1.0");
        CHECK(n->clientOrderId == kids[0].strategyOrderId, "B1 出向: clientOrderId == 子单 strategyOrderId");
        CHECK(std::strcmp(n->strategyId, "test1") == 0, "B1 出向: strategyId = test1");
        CHECK(std::strcmp(n->strategyRef, MakeRef(o, pos[0].pairId).c_str()) == 0,
              "B1 出向: strategyRef == \"algoOrderId_pairId\"（OnOrder 就是靠它定位的）");
    }

    // B2 行情无效（spreadEffective = false）-> 不报单
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        pdata.spreadEffective = false;
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 0, "B2 spreadEffective = false -> 不报单");
        CHECK(fx.PairOrders(o).empty(), "B2 也不产生 pairOrder");
    }

    // B3 行情延迟过大 -> openOrderFlag = false -> 不报单
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        // generateTs 落后 1 秒；curSpreadDelay = 50ms -> curDelay = false
        dbp::DbpData pdata = MakeData(100.00, 100.01, 100.00, 100.01,
                                      -1.0, -1.0, -1.0, -1.0,
                                      crypto::getCurrentTime() - 1 * SEC);
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 0, "B3 行情延迟 1s > curSpreadDelay(50ms) -> 不报单");
        (void)o;
    }

    // B4 rebalance 模式下同一算法单只允许一张在途 pairOrder -> 第二帧不重复报
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 1, "B4 第一帧报出 1 条");
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 1, "B4 第二帧：rebalance 模式已有一张在途 pairOrder -> 不重复报");
        CHECK(fx.PairOrders(o).size() == 1, "B4 pairOrder 仍然只有 1 张");
    }

    // B5 行情不属于该算法单 -> 一条都不报
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        OpenTtOL(fx);
        dbp::DbpTopic other = MakeTopic("BINANCE.USDT_SWAP.ETH-USDT|GATEIO.USDT_SWAP.ETH-USDT");
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&other, &pdata);
        CHECK(fx.NNew() == 0, "B5 价差 key 不匹配 -> 不报单");
    }
}

// ===========================================================================
// C. 成交回报（OnOrder -> UpdateAlgoPairOrderByQuantOrder -> PairOrderTrade）
// ===========================================================================
static void TestC_Fill() {
    Section("C. 成交回报（OnOrder）");

    // C1 主动腿全成 -> 子单状态/成交量更新 + 立刻补报被动腿
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);

        CHECK(fx.NNew() == 1, "C1 前置：主动腿已报出");
        auto pos = fx.PairOrders(o);
        if (fx.NNew() != 1 || pos.empty()) return;

        const int64_t soid = fx.News()[0]->clientOrderId;
        const int64_t pairId = pos[0].pairId;
        const double  px = fx.News()[0]->limitPrice;
        const std::string sref = MakeRef(o, pairId);

        pubsub::OrderResponse rtn = MakeRtn(soid, sref.c_str(),
                                            OS_FILLED, 1.0, px,
                                            BINANCE, USDT_SWAP);
        fx.algoCtx.OnOrder(rtn);

        // 子单簿
        //
        // ⚠️ 主动腿成交后**会被从 orderMgr 删掉**，不是留在里面变 OS_FILLED。
        //    AlgoContext::OnOrder 对 FILLED/REJECTED/CANCELED 三种终态会调
        //    UpdateAlgoPairOrderByDeleteQuantOrder -> orderMgr.DeleteOrderByOrder
        //    （"订单完结解冻"，见 BaseAlgoOrder.cpp:160-172）。
        //    所以成交之后 orderMgr 里留下的只有那条**新报的被动腿**。
        //    主动腿的成交量要去看 pairOrder.activeTotalVolumeOnOrder。
        auto kids = fx.ChildOrders(o);
        CHECK(kids.size() == 1, "C1 主动腿成交后从 orderMgr 出清，只剩新报的被动腿");
        CHECK(fx.NActiveChild(o) == 0, "C1 orderMgr 里已无主动腿");
        CHECK(fx.NPassiveChild(o) == 1, "C1 orderMgr 里有 1 条被动腿");
        if (kids.size() == 1) {
            CHECK(!kids[0].isActiveOrder, "C1 留下的那条是被动腿");
            CHECK(kids[0].orderStatus == OS_PEND, "C1 被动腿状态 OS_PEND（刚报出）");
            CHECK(Near(kids[0].volume, 1.0), "C1 被动腿量 = 1.0（按已成交的主动腿量对冲）");
        }

        // 成交要落到 pairOrder 上
        auto pos2 = fx.PairOrders(o);
        CHECK(pos2.size() == 1, "C1 pairOrder 仍在（被动腿还在途，尚未收口）");
        if (!pos2.empty()) {
            CHECK(Near(pos2[0].activeTotalVolumeOnOrder, 1.0), "C1 pairOrder.activeTotalVolumeOnOrder = 1.0");
        }

        // 补报被动腿
        CHECK(fx.NNew() == 2, "C1 主动腿成交 -> 立刻补报被动腿");
        if (fx.NNew() == 2) {
            const pubsub::NewOrder* pn = fx.News()[1];
            CHECK(pn->exchangeTypeEnum == GATEIO, "C1 被动腿出向: 交易所 = GATEIO");
            CHECK(pn->direction == DT_LONG, "C1 被动腿出向: direction = DT_LONG");
            CHECK(pn->offsetFlag == OF_OPEN, "C1 被动腿出向: offsetFlag = OF_OPEN");
            CHECK(std::strcmp(pn->strategyRef, sref.c_str()) == 0, "C1 被动腿与主动腿同一个 strategyRef（同一个 pairOrder）");
        }
    }

    // C2 乱序回报（volumeTraded 小于已有累计量）被丢弃
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        auto pos = fx.PairOrders(o);
        if (fx.NNew() != 1 || pos.empty()) { CHECK(false, "C2 前置失败"); return; }

        const int64_t soid = fx.News()[0]->clientOrderId;
        const std::string sref = MakeRef(o, pos[0].pairId);
        const double px = fx.News()[0]->limitPrice;

        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_PARTFILLED, 0.6, px, BINANCE, USDT_SWAP));
        size_t nAfterFirst = fx.NNew();
        // 再来一条更旧的回报：volumeTraded=0.3 < 累计 0.6 -> 应被丢弃
        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_PARTFILLED, 0.3, px, BINANCE, USDT_SWAP));

        auto kids = fx.ChildOrders(o);
        for (const auto& c : kids) {
            if (c.isActiveOrder) {
                CHECK(Near(c.totalVolumeOnOrder, 0.6), "C2 乱序回报被丢弃（累计量仍是 0.6）");
            }
        }
        (void)nAfterFirst;
    }

    // C3 strategyRef 解析不了 -> 丢弃，不崩
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        OpenTtOL(fx);
        fx.algoCtx.OnOrder(MakeRtn(1, "not-a-valid-ref", OS_FILLED, 1.0, 100.0));
        CHECK(true, "C3 strategyRef 非法 -> 丢弃不崩");
        CHECK(fx.NNew() == 0, "C3 也不会报出新单");
    }

    // C4 回报找不到算法单 -> 丢弃，不崩
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        OpenTtOL(fx);
        fx.algoCtx.OnOrder(MakeRtn(1, "123456_789", OS_FILLED, 1.0, 100.0));
        CHECK(true, "C4 算法单不存在 -> 丢弃不崩");
        CHECK(fx.NNew() == 0, "C4 不报新单");
    }

    // C5 回报找不到 pairOrder -> 丢弃，不崩
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        const int64_t soid = fx.News()[0]->clientOrderId;
        // 故意用一个不存在的 pairId
        fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, 424242).c_str(), OS_FILLED, 1.0, 100.0, BINANCE, USDT_SWAP));
        CHECK(true, "C5 pairId 对不上 -> 丢弃不崩");
        CHECK(fx.NNew() == 1, "C5 不额外报单");
    }

    // C6 部分成交（PARTFILLED）也要触发被动腿，且不终结子单
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        auto pos = fx.PairOrders(o);
        if (fx.NNew() != 1 || pos.empty()) { CHECK(false, "C6 前置失败"); return; }

        const int64_t soid = fx.News()[0]->clientOrderId;
        fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, pos[0].pairId).c_str(),
                                   OS_PARTFILLED, 0.5, fx.News()[0]->limitPrice,
                                   BINANCE, USDT_SWAP));

        auto kids = fx.ChildOrders(o);
        const stra::QuantOrder* act = nullptr;
        for (const auto& c : kids) if (c.isActiveOrder) act = &c;
        CHECK(act && act->orderStatus == OS_PARTFILLED, "C6 主动腿状态 = OS_PARTFILLED");
        CHECK(act && Near(act->totalVolumeOnOrder, 0.5), "C6 主动腿累计成交量 = 0.5");
        CHECK(fx.NNew() == 2, "C6 部分成交也补报被动腿（按已成交量对冲）");
    }
}

// ===========================================================================
// D. 撤单（SubmitAlgoOrder(CANCEL) -> CANCELLING -> 撤子单 -> OnTimer 收尾）
// ===========================================================================
static void TestD_Cancel() {
    Section("D. 撤单（本地请求 -> CANCELLING -> 撤子单 -> CANCELED）");

    // D1 CANCEL 请求把算法单推进 CANCELLING
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING, "D1 CANCEL -> ALGO_OS_CANCELLING");
        CHECK(o->commandType == stra::CommandType_UCANCELLING, "D1 commandType = UCANCELLING");
        CHECK(o->cancelOrderTime > 0, "D1 记下了 cancelOrderTime");
    }

    // D2 重复 CANCEL 幂等
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        const int64_t t1 = o->cancelOrderTime;
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        CHECK(o->cancelOrderTime == t1, "D2 重复 CANCEL 幂等（不改状态、不刷时间）");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING, "D2 仍在 CANCELLING");
    }

    // D3 CANCELLING 下 OnSpread 撤掉在途主动腿（门槛 10ms）
    //
    // ⚠️ 必须先把子单推到 OS_NEW。CancelOrderOnSpread 的 CANCELLING 分支只撤
    //    `OS_NEW || OS_PARTFILLED || OS_FILLED` 三种状态，而子单刚建出来是 OS_PEND
    //    （CreateActiveOrder 里写死的"等交易所"），OS_PEND 不在可撤列表里 —— 直接撤会一条都不发。
    //    生产里这一跳是交易所的报单回报（OnOrder, apiSource=AS_ADD_NEW_ORDER）补上的，
    //    所以测试里也必须补这一步，否则测的就不是真实时序。
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);              // 先报出主动腿
        CHECK(fx.NNew() == 1, "D3 前置：主动腿已报出");

        auto pos = fx.PairOrders(o);
        if (pos.empty()) return;
        const int64_t soid = fx.News()[0]->clientOrderId;
        fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, pos[0].pairId).c_str(),
                                   OS_NEW, 0.0, fx.News()[0]->limitPrice,
                                   BINANCE, USDT_SWAP));
        auto kids = fx.ChildOrders(o);
        CHECK(!kids.empty() && kids[0].orderStatus == OS_NEW, "D3 前置：交易所已确认 -> 子单 OS_NEW");

        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        // 把子单变老 20ms，跨过 nowTime - updateTime > 1000*10 的门槛
        AgeChildOrders(o, 20 * 1000);

        // spreadEffective=false：只走撤单分支，不再报新单，断言才干净
        pdata.spreadEffective = false;
        fx.algoCtx.OnSpread(&topic, &pdata);

        CHECK(fx.NCancel() == 1, "D3 CANCELLING 下撤掉在途主动腿 -> 1 条撤单指令");
        if (fx.NCancel() == 1) {
            const pubsub::CancelOrder* c = fx.Cancels()[0];
            CHECK(c->clientOrderId == soid, "D3 撤的就是那条子单");
            CHECK(c->exchangeTypeEnum == BINANCE, "D3 撤单发给主动腿交易所");
        }
        CHECK(fx.NNew() == 1, "D3 CANCELLING 下不再报新单");
    }

    // D4 子单撤单回报回来 -> pairOrder 清零 -> OnTimer 把算法单推进 CANCELED 并回传策略层
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);

        auto pos = fx.PairOrders(o);
        if (fx.NNew() != 1 || pos.empty()) { CHECK(false, "D4 前置失败"); return; }
        const int64_t soid = fx.News()[0]->clientOrderId;
        const std::string sref = MakeRef(o, pos[0].pairId);

        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, fx.News()[0]->limitPrice,
                                   BINANCE, USDT_SWAP));
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        // 撤单回报：OS_CANCELED
        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));

        CHECK(fx.PairOrders(o).empty(), "D4 子单撤掉后 pairOrder 被删除");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING,
              "D4 此时还停在 CANCELLING（等 OnTimer 收尾）");

        fx.algoCtx.OnTimer(crypto::getCurrentTime());
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELED, "D4 OnTimer -> ALGO_OS_CANCELED");
        CHECK(o->commandType == stra::CommandType_CANCELED, "D4 commandType = CANCELED");
    }

    // D5 已终结的算法单拒绝再变更
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        o->algoOrderStatus = stra::ALGO_OS_CANCELED;
        const int64_t t = o->updateTime;
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELED, "D5 已终结的单拒绝 CANCEL");
        CHECK(o->updateTime == t, "D5 也不动它的 updateTime");
    }

    // D6 MODIFY 走整份快照覆盖
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        stra::AlgoOrderModify mod;
        std::memset(&mod, 0, sizeof(mod));
        mod.profitSwitch = true;
        mod.profitPct = 0.0042;
        mod.ttOSSwitch = true;
        mod.ttOSStartSpread = 0.0077;
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_MODIFY, &mod);
        CHECK(o->profitSwitch && Near(o->profitPct, 0.0042), "D6 MODIFY 覆盖 profitSwitch/profitPct");
        CHECK(o->ttOSSwitch && Near(o->ttOSStartSpread, 0.0077), "D6 MODIFY 覆盖 ttOS 参数");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "D6 MODIFY 不改状态");
    }

    // D7 CANCELLING 期间拒绝 MODIFY（避免两套流程互相覆盖）
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_CANCEL, nullptr);
        stra::AlgoOrderModify mod;
        std::memset(&mod, 0, sizeof(mod));
        mod.profitPct = 0.0042;
        fx.algoCtx.SubmitAlgoOrder(o->algoOrderId, stra::CommandType_MODIFY, &mod);
        CHECK(!Near(o->profitPct, 0.0042), "D7 CANCELLING 期间 MODIFY 被拒");
    }
}

// ===========================================================================
// E. 超时撤单（OnSpread 里的时间撤单，与 D 的"本地请求撤单"是两条不同的路）
// ===========================================================================
static void TestE_TimeoutCancel() {
    Section("E. 超时撤单（OnSpread 时间撤单）");

    // E1 主动腿 Taker 超过 activeTakerCancelOrderTime(5s) -> 撤
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 1, "E1 前置：主动腿已报出");
        CHECK(o->activeOrderType == OT_MARKET, "E1 前置：主动腿是 Taker（OT_MARKET）");
        CHECK(o->activeTakerCancelOrderTime == 5 * SEC, "E1 前置：超时门槛 = 5s");

        // 先补上交易所确认。CancelOrderOnSpread 只撤
        // OS_NEW / OS_PARTFILLED / OS_FILLED，刚建出来的 OS_PEND 不在其中。
        auto pos = fx.PairOrders(o);
        if (pos.empty()) return;
        const int64_t soid = fx.News()[0]->clientOrderId;
        fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, pos[0].pairId).c_str(),
                                   OS_NEW, 0.0, fx.News()[0]->limitPrice, BINANCE, USDT_SWAP));

        AgeChildOrders(o, 6 * SEC);            // 变老 6 秒
        pdata.spreadEffective = false;          // 只走撤单分支
        fx.algoCtx.OnSpread(&topic, &pdata);

        CHECK(fx.NCancel() == 1, "E1 主动腿 Taker 超 5s -> 撤单");
    }

    // E2 没超时 -> 不撤
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        pdata.spreadEffective = false;
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NCancel() == 0, "E2 刚报出未超时 -> 不撤单");
        (void)o;
    }

    // E3 主动腿 Maker 的价格偏离撤单
    //    OT_POST_ONLY 的判定落在**子单**的 orderType 上（CancelOrderOnSpread 读的是
    //    orderMgr 里的 QuantOrder），而 OT_POST_ONLY 只有 MAKER_TAKER 配对单才会产生
    //    （GetTargetPairOrder 里 acOrderType = OT_POST_ONLY）。所以这一条要用 MT 单。
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        PairInfo& pi = *fx.pi();
        MakeHoldable(pi, 0.0);
        pi.autoFlag = false;                  // 手动模式 -> algoMode = "MT"
        ArmOpen(pi, "mtOL", 0.0);
        fx.ptCtx.ProcessPairSignal(pi);
        BaseAlgoOrder* o = fx.Order0();
        CHECK(o != nullptr, "E3 前置：报出 MT 算法单");
        if (!o) return;
        CHECK(o->mtOLSwitch, "E3 前置：mtOLSwitch = true");

        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NNew() == 1, "E3 前置：MT 主动腿已报出");
        if (fx.NNew() != 1) return;

        auto kids = fx.ChildOrders(o);
        CHECK(!kids.empty() && kids[0].orderType == OT_POST_ONLY,
              "E3 前置：MT 主动腿子单是 OT_POST_ONLY（走 Maker 撤单分支）");

        auto pos = fx.PairOrders(o);
        if (pos.empty()) return;

        // 与 E1/D3 同一个前置：CancelOrderOnSpread 的 else 分支（Maker 价格偏离走这条）
        // 第 448 行先卡 orderStatus ∈ {OS_NEW, OS_PARTFILLED, OS_FILLED}，
        // 刚 CreateActiveOrder 出来的 OS_PEND 进不去，必须先喂交易所确认。
        const int64_t soid = fx.News()[0]->clientOrderId;
        fx.algoCtx.OnOrder(MakeRtn(soid, MakeRef(o, pos[0].pairId).c_str(),
                                   OS_NEW, 0.0, fx.News()[0]->limitPrice, BINANCE, USDT_SWAP));

        // 主动腿价格被行情甩开 5% >> activeMakerCancelOrderPct(0.001)
        //   DT_SHORT: (activeAsk / pairOrder.activeAskPrice1 - 1) < -pct  -> 把 ask 压低
        //   DT_LONG : (activeBid / pairOrder.activeBidPrice1 - 1) > +pct  -> 把 bid 抬高
        //   注意这条"主动腿价格变化撤单"（BaseAlgoOrder.cpp:505-514）**没有时间门槛**，
        //   所以不用 AgeChildOrders 也能命中；上面那条"时间撤单"（第 464 行）才要。
        if (pos[0].activeDirection == DT_SHORT) {
            pdata.activeAskPrice[0] = pos[0].activeAskPrice1 * 0.95;
        } else {
            pdata.activeBidPrice[0] = pos[0].activeBidPrice1 * 1.05;
        }
        pdata.spreadEffective = false;
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NCancel() >= 1, "E3 主动腿价格偏离 > activeMakerCancelOrderPct -> 撤单");
    }
}

// ===========================================================================
// F. 风控平仓（策略层 ProcessRisk -> RequestCancelAlgoOrder -> 执行侧撤单）
//
// ⚠️ RiskManager::RiskConfig 里**没有** "强制平仓" 这种开关 —— 风控是真的算出来的。
//    所以这里不能用配置去硬开，而是构造一个真实会命中的风控条件：
//    CheckRisk 的第一条就是 CheckTinyClose（碎单）：
//        |pairTotalVolume| > 1e-9                      （有持仓）
//        CalcPositionValue() = |pairTotalVolume * 腿价 * multiple| < 25 USDT
//    取 pairTotalVolume = 0.01、腿价 100、multiple 1 -> 市值 1 USDT < 25 -> 命中。
//    这是一条真实存在的风控分支，不是为了测试新造的开关。
// ===========================================================================
static void ArmTinyCloseRisk(PairInfo& pi) {
    pi.pairTotalVolume = 0.01;          // 有持仓，且远小于碎单门槛
    pi.activeParam.calcType = 0;
    pi.activeParam.multiple = 1.0;
    pi.rtSpread.activePriceTema = 100.0;
    pi.rtSpread.valid = true;
}

static void TestF_RiskClose() {
    Section("F. 风控平仓（ProcessRisk -> 撤单 / 强平报单）");

    // F1 对子被算法单占着时，风控只请求撤单，不另报强平单
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        PairInfo& pi = *fx.pi();
        // OpenTtOL 走的是真实的 SetActiveAlgoOrder 路径，这两个字段应当已被置上
        CHECK(pi.hasActiveAlgoOrder, "F1 前置：对子已被算法单占住");
        CHECK(std::strcmp(pi.currentAlgoOrderId, std::to_string(o->algoOrderId).c_str()) == 0,
              "F1 前置：currentAlgoOrderId == algoOrderId");

        ArmTinyCloseRisk(pi);
        CHECK(std::abs(pi.CalcPositionValue()) < 25.0, "F1 前置：持仓市值 < tinyCloseThresholdUsdt");

        const size_t before = fx.modifyCalls.size();
        fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());

        CHECK(fx.modifyCalls.size() == before + 1, "F1 风控 -> 发出 1 次变更回调");
        if (fx.modifyCalls.size() == before + 1) {
            CHECK(fx.modifyCalls.back().second == stra::CommandType_CANCEL,
                  "F1 回调的 cmd = CommandType_CANCEL");
            CHECK(fx.modifyCalls.back().first == o->algoOrderId, "F1 回调的 id 就是在册算法单");
        }
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING,
              "F1 执行侧收到 CANCEL -> 算法单进入 CANCELLING");
        CHECK(fx.NNew() == 0, "F1 对子被占着 -> 不另报强平单");
    }

    // F2 强平单在途时不重复请求撤单（riskCloseOrderInFlight 早退）
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        PairInfo& pi = *fx.pi();
        ArmTinyCloseRisk(pi);
        pi.riskCloseOrderInFlight = true;

        const size_t before = fx.modifyCalls.size();
        fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
        CHECK(fx.modifyCalls.size() == before, "F2 强平单在途 -> ProcessRisk 早退，不再发撤单");
        CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "F2 算法单状态未被改动");
    }

    // F3 风控撤单走完整条链：CANCEL -> 撤子单 -> 撤单回报 -> OnTimer -> CANCELED
    {
        Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);
        BaseAlgoOrder* o = OpenTtOL(fx);
        dbp::DbpTopic topic = MakeTopic(kPairKey);
        dbp::DbpData pdata = FreshData();
        fx.algoCtx.OnSpread(&topic, &pdata);
        auto pos = fx.PairOrders(o);
        if (fx.NNew() != 1 || pos.empty()) { CHECK(false, "F3 前置失败"); return; }
        const int64_t soid = fx.News()[0]->clientOrderId;
        const std::string sref = MakeRef(o, pos[0].pairId);
        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0,
                                   fx.News()[0]->limitPrice, BINANCE, USDT_SWAP));

        PairInfo& pi = *fx.pi();
        ArmTinyCloseRisk(pi);
        fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING, "F3 风控把算法单推进 CANCELLING");

        AgeChildOrders(o, 20 * 1000);
        pdata.spreadEffective = false;
        fx.algoCtx.OnSpread(&topic, &pdata);
        CHECK(fx.NCancel() == 1, "F3 执行侧撤掉在途主动腿");

        fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
        fx.algoCtx.OnTimer(crypto::getCurrentTime());
        CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELED, "F3 收尾 -> ALGO_OS_CANCELED");
    }
}

// ===========================================================================
// G. 全链路一：成交闭环
//    行情 -> 建单 -> 拆单报主动腿 -> 成交 -> 补报被动腿 -> 被动腿成交 -> 收口
//
// 这一组不做"单元"断言，而是像生产那样把事件按顺序喂进去，
// 每一步都检查算法单有没有走到预期的状态。任何一环断了，后面的断言会连锁失败，
// 定位点就是第一条 FAIL。
//
// 为什么要拆成 G/H 两条链：一条链只能走到一个终局。成交链路的终局是
// "两腿都成交 -> pairOrder 收口删除"；撤单链路的终局是
// "撤掉在途主动腿 -> 子单清零 -> OnTimer -> ALGO_OS_CANCELED"。
// 硬塞进一条链会得到一个既不是成交也不是撤单的四不像。
// ===========================================================================
static void TestG_FillLoop() {
    Section("G. 全链路一（行情 -> 建单 -> 拆单 -> 成交 -> 被动腿 -> 收口）");

    Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);

    // ---- 1) 行情进来：策略层收行情、喂价差样本、算信号 ----
    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmOpen(pi, "ttOL", 0.0);
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshStrategyData();
    // 注意 OnSpread 内部会 UpdateRtSpread + ProcessPairSignal，也就是说行情本身
    // 就可能直接把算法单建出来。这里不假设"行情不建单"，只要求最终恰好一张。
    fx.ptCtx.OnSpread(&topic, &pdata);

    // ---- 2) 信号成立：策略层建算法单，回调注册进执行层 ----
    fx.ptCtx.ProcessPairSignal(pi);
    CHECK(fx.created.size() == 1, "G1 信号成立 -> 策略层报出 1 张算法单");
    BaseAlgoOrder* o = fx.Order0();
    if (!o) return;
    CHECK(fx.AlgoOrder(o->algoOrderId) == o, "G1 算法单已在执行侧入册");
    CHECK(o->algoOrderStatus == stra::ALGO_OS_NEW, "G1 算法单状态 = ALGO_OS_NEW");
    CHECK(pi.hasActiveAlgoOrder, "G1 策略侧已把对子挂上这张算法单");

    // ---- 3) 执行侧拆单报单 ----
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "G2 执行侧拆出并报出主动腿子单");
    auto pos = fx.PairOrders(o);
    if (fx.NNew() != 1 || pos.empty()) return;
    const int64_t soid = fx.News()[0]->clientOrderId;
    const int64_t pairId = pos[0].pairId;
    const std::string sref = MakeRef(o, pairId);
    const double px = fx.News()[0]->limitPrice;

    // ---- 3b) 交易所确认（OS_PEND -> OS_NEW）----
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, px, BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 1, "G2b 交易所确认后主动腿仍在子单簿");

    // ---- 4) 主动腿成交 -> 子单出清 + 补报被动腿 ----
    //
    // ⚠️ 主动腿成交后**不会**以 OS_FILLED 留在 orderMgr 里 ——
    //    AlgoContext::OnOrder 对 FILLED/REJECTED/CANCELED 三种终态调
    //    UpdateAlgoPairOrderByDeleteQuantOrder -> orderMgr.DeleteOrderByOrder
    //    （BaseAlgoOrder.cpp:160-172，注释是"订单完结解冻"）。
    //    所以这里的断言方向是"出清"，成交量要去 pairOrder 上看。
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_FILLED, 1.0, px, BINANCE, USDT_SWAP));
    CHECK(fx.NNew() == 2, "G3 主动腿成交 -> 补报被动腿");
    CHECK(fx.NActiveChild(o) == 0, "G3 主动腿终态后从子单簿出清");
    CHECK(fx.NPassiveChild(o) == 1, "G3 子单簿里只剩新报的被动腿");
    {
        auto pos3 = fx.PairOrders(o);
        CHECK(pos3.size() == 1, "G3 pairOrder 仍在（被动腿在途，尚未收口）");
        if (!pos3.empty()) {
            CHECK(Near(pos3[0].activeTotalVolumeOnOrder, 1.0),
                  "G3 pairOrder.activeTotalVolumeOnOrder = 1.0（主动腿已成交 1.0）");
        }
    }

    // ---- 5) 被动腿确认 + 成交 -> 整张 pairOrder 收口 ----
    if (fx.NNew() == 2) {
        const pubsub::NewOrder* pn = fx.News()[1];
        const int64_t psoid = pn->clientOrderId;
        const double  ppx   = pn->limitPrice;
        fx.algoCtx.OnOrder(MakeRtn(psoid, sref.c_str(), OS_NEW,    0.0, ppx, GATEIO, USDT_SWAP));
        fx.algoCtx.OnOrder(MakeRtn(psoid, sref.c_str(), OS_FILLED, 1.0, ppx, GATEIO, USDT_SWAP));

        CHECK(fx.ChildOrders(o).empty(), "G4 两腿都终结 -> 子单簿清零");
        // PairOrderTrade 在"已经没量可报"时走完结分支：
        //   pairOrder.status = 1 + pairOrderMgr.DeletePairOrderByPairOrder
        //   （BaseAlgoOrder.cpp:757-765 与 863）
        CHECK(fx.PairOrders(o).empty(), "G4 两腿成交 -> pairOrder 收口并从 pairOrderMgr 删除");
        CHECK(fx.NNew() == 2, "G4 收口后不再报新单");
    }

    // ---- 6) 策略层收到执行侧回传：对子拿到持仓 ----
    //      PairOrderTrade -> UpdateAlgoPairOrderByPairOrder -> NotifyAlgoOrderUpdate
    //      -> PairTradingContext::OnAlgoOrderUpdate（同线程直接回调，替代原来的 QuantPub）。
    //
    // ⚠️ 回传分两档（PairTradingContext.cpp:1011-1029）：
    //      非终态 -> **只同步量/价**，不释放对子、不做结算；
    //      终态   -> OnAlgoFinished + RecalcOrderParams + ClearActiveAlgoOrder。
    //    成交收口后算法单状态仍是 ALGO_OS_NEW（四个 switch 还开着，策略会继续追），
    //    所以这里"对子拿到持仓"成立、而"对子被释放"**不**成立。
    CHECK(pi.HasPosition(), "G5 策略侧已收到执行侧回传：对子拿到持仓");
    CHECK(pi.hasActiveAlgoOrder, "G5 算法单未终结 -> 对子仍被占着（非终态回传不释放对子）");
}

// ===========================================================================
// H. 全链路二：撤单闭环（由**真实风控**驱动，不是直接喊 SubmitAlgoOrder(CANCEL)）
//    行情 -> 建单 -> 拆单报主动腿 -> 风控命中碎单 -> 策略层要求撤单
//         -> 执行侧 CANCELLING -> 撤在途主动腿 -> 撤单回报 -> OnTimer -> CANCELED
//
// 与 F 组的区别：F 是"风控这一环"的单元测试，H 是把风控当成链路里的一环跑通整条。
// ===========================================================================
static void TestH_CancelLoop() {
    Section("H. 全链路二（行情 -> 建单 -> 拆单 -> 风控 -> 撤单 -> 终结）");

    Fx fx; Boot(fx); GoTrading(fx); SeedFunds(fx);

    // ---- 1) 行情 + 信号 -> 建单 ----
    PairInfo& pi = *fx.pi();
    MakeHoldable(pi, 0.0);
    ArmOpen(pi, "ttOL", 0.0);
    dbp::DbpTopic topic = MakeTopic(kPairKey);
    dbp::DbpData pdata = FreshStrategyData();
    fx.ptCtx.OnSpread(&topic, &pdata);
    fx.ptCtx.ProcessPairSignal(pi);
    BaseAlgoOrder* o = fx.Order0();
    CHECK(o != nullptr, "H1 行情 + 信号 -> 算法单已建");
    if (!o) return;

    // ---- 2) 执行侧拆单报主动腿 + 交易所确认 ----
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NNew() == 1, "H2 执行侧报出主动腿子单");
    auto pos = fx.PairOrders(o);
    if (fx.NNew() != 1 || pos.empty()) return;
    const int64_t soid = fx.News()[0]->clientOrderId;
    const std::string sref = MakeRef(o, pos[0].pairId);
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_NEW, 0.0, fx.News()[0]->limitPrice,
                               BINANCE, USDT_SWAP));
    CHECK(fx.NActiveChild(o) == 1, "H2 确认后主动腿在途（可撤）");

    // ---- 3) 风控命中碎单 -> 策略层算出要撤 -> 回调执行侧 ----
    ArmTinyCloseRisk(pi);
    CHECK(pi.hasActiveAlgoOrder, "H3 前置：对子已被算法单占住");
    CHECK(std::strcmp(pi.currentAlgoOrderId, std::to_string(o->algoOrderId).c_str()) == 0,
          "H3 前置：currentAlgoOrderId == algoOrderId");

    const size_t before = fx.modifyCalls.size();
    fx.ptCtx.ProcessRisk(pi, crypto::getCurrentTime());
    CHECK(fx.modifyCalls.size() == before + 1, "H3 风控命中 -> 策略层发出 1 次变更回调");
    if (fx.modifyCalls.size() <= before) return;
    CHECK(fx.modifyCalls[before].first == o->algoOrderId, "H3 撤的就是这张算法单");
    CHECK(fx.modifyCalls[before].second == stra::CommandType_CANCEL, "H3 指令类型 = CANCEL");
    CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELLING, "H3 执行侧收到 -> 算法单进入 CANCELLING");

    // ---- 4) CANCELLING -> 撤掉在途主动腿 ----
    //   CANCELLING 分支（BaseAlgoOrder.cpp:407-444）比 else 分支更保守：
    //   它只撤主动腿，且要求 nowTime - order.updateTime > 1000*10（10ms）。
    //   子单是刚报出来的，必须先把它的 updateTime 拨老才跨得过这道门槛。
    AgeChildOrders(o, 20 * 1000);
    pdata.spreadEffective = false;
    fx.algoCtx.OnSpread(&topic, &pdata);
    CHECK(fx.NCancel() >= 1, "H4 CANCELLING -> 撤掉在途主动腿");
    {
        auto cs = fx.Cancels();
        CHECK(!cs.empty() && cs[0]->clientOrderId == soid, "H4 撤的正是那条主动腿子单");
    }

    // ---- 5) 撤单回报 -> 子单清零 -> pairOrder 删除 ----
    fx.algoCtx.OnOrder(MakeRtn(soid, sref.c_str(), OS_CANCELED, 0.0, 0.0, BINANCE, USDT_SWAP));
    CHECK(fx.ChildOrders(o).empty(), "H5 撤单回报 -> 子单簿清零");
    CHECK(fx.PairOrders(o).empty(), "H5 子单清零 -> pairOrder 收口删除");

    // ---- 6) OnTimer 收尾 ----
    fx.algoCtx.OnTimer(crypto::getCurrentTime());
    CHECK(o->algoOrderStatus == stra::ALGO_OS_CANCELED, "H6 OnTimer -> ALGO_OS_CANCELED，全链路走完");
    CHECK(fx.AlgoOrder(o->algoOrderId) == o, "H6 终结后在宽限期内仍能查到（留给策略层读状态）");
}

// ===========================================================================
// 主
// ===========================================================================
int main() {
    SignalGenerator::Instance().SetConfig(FeeSlippageConfig{});
    RiskManager::Instance().SetConfig(RiskConfig{});

    TestA_Register();
    TestB_SplitAndPlace();
    TestC_Fill();
    TestD_Cancel();
    TestE_TimeoutCancel();
    TestF_RiskClose();
    TestG_FillLoop();
    TestH_CancelLoop();

    std::printf("\n=================================================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
