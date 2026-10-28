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
#include <mutex>
#include <numeric>
#include <optional>
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
#include <utility>
#include <vector>

// ⚠️ `#define private public` 必须在**所有 std 头包含完之后**才生效。
//    原因：项目头（PairTradingContext.h / SignalGenerator.h / SpreadStatsBuilder.h …）
//    会在宏生效期间被解析，它们内部还会 include 若干 std 头；如果那些 std 头此刻才第一次
//    被包含，它们的内部实现就会在 `private` 被改写成 `public` 的情况下解析 ——
//    libc++ 上通常还能过，libstdc++（Ubuntu 服务器用的 g++）更容易炸。
//    所以这里先把 std 头**一次包含足**，让项目头里的 include 全部命中 include guard。
//    上面这份清单是「按需 + 冗余」的：不要求精确，多包含几个没有代价。
//
//    boost 同理，而且更要紧：PairTradingContext.h 现在 include 了
//    <boost/lockfree/spsc_queue.hpp>（分位数计算的分线程通道用它做 SPSC 队列）。
//    boost 头里 `private:` 极多，绝不能在 `private` 已被改写成 `public` 时第一次解析。
//    这里先包含掉，宏生效期间那次 include 就命中 guard 了。
//    （header-only，不需要链 boost 库；macOS 上没有 boost 时 run.sh 会用等价桩顶上。）
#include <boost/lockfree/spsc_queue.hpp>

// Utility.h 放在宏之外：它只有 splitString 与三个 id 生成器，没有需要提权的私有成员。
// A16 直接调 GenerateStrategyAlgoPairId()，所以要在这里可见
// （BuildAlgoOrderJson 内部也用它生成算法单 id）。
#include "basic/Utility.h"

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

// 合约信息。真实的 md::InstrumentInfo 字段很多（前 7 个是枚举和字符串），
// 不能再用聚合初始化 {1.0, 0.0001, 0.1, 0} —— 那样会把 1.0 塞进 exchangeTypeEnum。
// 改成按字段名赋值，顺序就和真实头无关了。
static md::InstrumentInfo MakeInstrumentInfo(double value = 1.0,
                                             double tickSize = 0.0001,
                                             double minSize = 0.1,
                                             int calcType = 0) {
    md::InstrumentInfo info;
    info.value = value;
    info.tickSize = tickSize;
    info.minSize = minSize;
    info.calcType = calcType;
    info.lotSize = tickSize;
    return info;
}

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

    // SignalGenerator 也是单例，它现在带两份**跨对子**的可变状态（全局持仓闸门 + 配置里的只平不开）。
    // 不清的话 D13 / K 组结尾留下的 `true` 会带进后面每一个用例 —— 症状是"某几个用例的
    // 开仓开关莫名其妙是 false"，而且跟用例本身毫无关系，极难定位。
    // 生产上这两个状态分别由 OnTimer 第 0.5/0.6 步每轮刷新，冷启动默认 false，
    // 所以这里清成 false 与真实初值一致。
    SignalGenerator::Instance().SetGlobalPositionExceeded(false);
    SignalGenerator::Instance().SetCloseOnly(false);
}

// ---------------------------------------------------------------------------
// 夹具
// ---------------------------------------------------------------------------
struct Fx {
    // ⚠️ ctx 必须**放堆上**，不能是 inline 成员。
    // PairTradingContext 里有 `PairInfoQueue`（spsc_queue<PairInfoRecord, capacity<1024>>），
    // 1024 × 304 B ≈ 304 KB 的 inline 数组，整个 Fx 就 320 KB 上下。
    // 而 clang **不做栈槽复用**：同一个函数里每个 `Fx fx;` 各占一份独立栈槽，
    // TestG_Restart 里有 27 个 ⇒ 27 × 320 KB ≈ 8.6 MB > 主线程 8 MB 栈，
    // 于是**函数序言里一碰栈就 SIGSEGV**（症状很误导人：上一节最后一条 PASS 打完就崩，
    // 连下一节的 `=== G. 重启 ===` 标题都打不出来，看着像上一条断言挂的）。
    // 用"堆对象 + 引用"而不是改成 `fx.ctx->`：105 处调用点一行都不用动。
    std::unique_ptr<PairTradingContext> ctxStore{std::make_unique<PairTradingContext>()};
    PairTradingContext& ctx{*ctxStore};
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
// statsPath 是分位数快照路径（重启免预热）。默认**留空 = 关掉**：
// 绝大多数用例不关心它，开着会让每个用例都往相对路径 data/spread_stats.csv
// 写盘、还可能把上一轮留下的统计带进来污染断言。需要它的用例（G17-G19）显式传。
static void Boot(Fx& fx, const std::string& csv = "", const std::string& statsPath = "") {
    ResetWorld();
    fx.smc.Clear();
    fx.smc.Set(kActive, MakeInstrumentInfo());
    fx.smc.Set(kPassive, MakeInstrumentInfo());

    PairTradingConfig cfg;
    cfg.pairKeys = {kPairKey};
    cfg.activeAccountId = 10000;
    cfg.passiveAccountId = 10001;
    cfg.csvStatePath = csv;
    cfg.spreadStatsStatePath = statsPath;
    cfg.spreadStatsMinSamples = 3;      // 让少量样本就能建立统计
    cfg.spreadSampleIntervalSec = 0;     // 不降频
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
// 把 T_signal 该做的一轮，在**调用线程**里同步跑掉。
//
// 为什么需要它：分位数计算（SpreadStatsBuilder::Prune + Build，实测 885 µs/对子）已经从
// 行情线程搬到 T_signal 上（见 docs/thread_split_design.md §8）。而 D4 / G17 / G19 / G20
// 的断言形态是「喂够样本 -> OnTimer -> 统计就该建立、orderParams 就该被重算」——
// T_signal 是异步线程，测试里既不能等 spreadStatsUpdateIntervalSec（默认 60s），
// 也不能依赖另一个线程的调度顺序，所以只能显式驱动一轮。
//
// ⚠️ 必须先 StopSignalThread：RunSignalOnce 会碰 m_spreadWindows 与 m_statsQ，
//    它们的所有权约定是「单线程」（后者还是 SPSC 队列），两个线程同时进去就是数据竞争。
//    本函数只做「停线程 + 手工推一轮」；生产路径由 signal_work() 自己按真实时间循环调
//    RunSignalOnce，走的是同一段逻辑。
//
// lastStatsUpdateUs 传 0 就能让那一轮的「到点没」门控必然成立（nowUs - 0 > 60e6），
// 等价于以前那个已删掉的 force 形参。
static void PumpSignal(PairTradingContext& ctx, int64_t nowUs) {
    ctx.StopSignalThread();
    int64_t lastStatsUpdateUs = 0; // 0 -> nowUs - 0 > 60e6 必然成立
    ctx.RunSignalOnce(nowUs, lastStatsUpdateUs);
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

// 手工造一份"有效且想让开仓"的分位数，用来把 RecalcOrderParams 逼进**完整路径**
// （而不是 !IsValid 的早退分支），这样才验得到"闸门放在最后"这件事。
//
// 为什么这组数一定让 4 个开仓开关为 true：
//   费率是 main() 里写死的那套（F_mt = 0.0009 / F_tt = 0.0013），spreadAdjPct = 0.8，
//   minSpreadTarget = 0.002（2026-10-26 起），所以 RecalcOrderParams 末尾那道"绝对地板"
//   也会参与：
//   canOpenLong  = (mtOLStartSpread < -minSpreadTarget) && !closeFlag
//                = (askAskDQ*0.8 + F_mt < -0.002)              <- -0.8+0.0009 => -0.7991 ✓
//   canOpenShort = (mtOSStartSpread > -minSpreadTarget)        <- 0.8-0.0009 => 0.7991 ✓
//   地板本身不生效：|±0.7991| 远深于 0.002005，夹逼只抬高不压低。
//   spreadSpan   = bidBidUQ - askAskDQ = 2.0 >= minSpreadSpan(0) => 不触发"套利空间不足"早退 ✓
// 其余四个分位数只用来填 CalcExpectSpread 的入参，取 ±1.0 保证不是 NaN。
static void MakeOpenFriendlyStats(PairInfo& pi) {
    auto& s = pi.largeStats;
    s.bidAskUQ = s.bidAskDQ = -1.0;
    s.bidBidUQ = s.askBidUQ =  1.0;
    s.bidBidDQ = s.askBidDQ = -1.0;
    s.askAskUQ =  1.0;
    s.askAskDQ = -1.0;
    s.count = 10;
    s.valid = true;
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
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
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
    //
    // ⚠️ 期望值在 2026-10-01 随生产代码一起变了（提交 4f3e2a5）：
    //      改前：TT 主动腿 OT_MARKET、MT 主动腿 OT_LIMIT、被动腿 OT_MARKET
    //      改后：TT 主动腿 OT_LIMIT、 MT 主动腿 OT_POST_ONLY、被动腿 OT_LIMIT
    //    语义上仍然成立 —— TT 主动腿用 OT_LIMIT 配一个穿价的价格，效果是吃单但把
    //    最差成交价钉住；MT 主动腿要挂单所以必须 POST_ONLY（否则会被当吃单拒掉）。
    //    这三个值现在都是 PairTradingConfig 的配置项（默认值同生产），
    //    改配置就能换，不需要再动这里 —— 但默认值变了仍要同步本用例。
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* tt = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        BaseAlgoOrder* mt = fx.ctx.BuildAlgoOrderJson(pi, "MT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(tt && tt->activeOrderType == OT_LIMIT, "A2 TT 主动腿 = OT_LIMIT（穿价限价，效果是吃单）");
        CHECK(mt && mt->activeOrderType == OT_POST_ONLY, "A2 MT 主动腿 = OT_POST_ONLY（必须挂单）");
        CHECK(tt && tt->passiveOrderType == OT_LIMIT, "A2 被动腿恒 = OT_LIMIT");
        CHECK(mt && mt->passiveOrderType == OT_LIMIT, "A2 被动腿恒 = OT_LIMIT (MT)");
        delete tt; delete mt;
    }

    // A3 驱动类型
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(o && o->activeDriveType == stra::DriveType_ACTIVE,  "A3 activeDriveType = ACTIVE");
        CHECK(o && o->passiveDriveType == stra::DriveType_PASSIVE, "A3 passiveDriveType = PASSIVE");
        delete o;
    }

    // A4 费率/滑点与信号侧同源
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
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
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0, AlgoRole::CLOSE_SIGNAL);
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
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr,
              "A6 开仓开关关闭 + 无让利 -> 建单失败(nullptr)");
        BaseAlgoOrder* risk = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.00002, AlgoRole::OPEN_SIGNAL);
        CHECK(risk != nullptr, "A6 开仓开关关闭但带让利（风控强平）-> 仍能建单");
        delete risk;

        pi.orderParams.ttCLSwitch = false;
        BaseAlgoOrder* cl = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0, AlgoRole::CLOSE_SIGNAL);
        CHECK(cl != nullptr, "A6 平仓开关关闭 -> 平仓不受开关限制，仍能建单");
        delete cl;
    }

    // A7 报单量校验
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.ttTargetVolume = 0.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A7 targetVolume = 0 -> nullptr");
        pi.ttTargetVolume = std::nan("");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A7 targetVolume = NaN -> nullptr");
        pi.ttTargetVolume = -1.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A7 targetVolume < 0 -> nullptr");
        pi.ttTargetVolume = 10.0;
        pi.mtTargetVolume = 0.0;
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "MT", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A7 MT 走 mtTargetVolume 校验");
    }

    // A8 非法模式/方向
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "XX", "OL", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A8 非法 algoMode -> nullptr");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "TT", "ZZ", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A8 非法 direction -> nullptr");
        CHECK(fx.ctx.BuildAlgoOrderJson(pi, "", "", 0.0, AlgoRole::OPEN_SIGNAL) == nullptr, "A8 空模式 -> nullptr（不崩）");
    }

    // A9 建单时的开关处理：只有**风控强平**清掉四个开仓开关，普通平仓单不清
    //    2026-10-28 收窄。普通平仓单（CLOSE_SIGNAL）允许在开仓带回补，清了就等于
    //    砍掉它的回补能力；"平到 0 要结束"那件事改由算法单的 role 决定（AlgoContext:1376）。
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& op = pi.orderParams;
        op.ttOLSwitch = op.ttOSSwitch = op.mtOLSwitch = op.mtOSSwitch = true;
        op.ttCLSwitch = op.ttCSSwitch = op.mtCLSwitch = op.mtCSSwitch = true;

        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.0, AlgoRole::CLOSE_SIGNAL);
        CHECK(o && o->ttOLSwitch && o->ttOSSwitch && o->mtOLSwitch && o->mtOSSwitch,
              "A9 普通平仓单：四个开仓开关原样保留（可以回补）");
        CHECK(o && o->role == AlgoRole::CLOSE_SIGNAL, "A9 普通平仓单：role = CLOSE_SIGNAL");
        CHECK(o && o->ttCLSwitch, "A9 平仓单：本次触发的平仓开关被强制打开");
        CHECK(o && o->mtCLSwitch && o->mtCSSwitch, "A9 平仓单：不动其它平仓开关");
        delete o;

        BaseAlgoOrder* r = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", 0.00002, AlgoRole::RISK_CLOSE);
        CHECK(r && !r->ttOLSwitch && !r->ttOSSwitch && !r->mtOLSwitch && !r->mtOSSwitch,
              "A9 风控强平单：四个开仓开关全部清零（永不开仓）");
        CHECK(r && r->role == AlgoRole::RISK_CLOSE, "A9 风控强平单：role = RISK_CLOSE");
        delete r;
    }

    // A10 风控让利把触发价差往更容易成交的方向挪
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.orderParams.ttCLStartSpread = 0.0010;
        pi.orderParams.ttCLEndSpread   = 0.0011;
        const double shift = 0.00002;
        BaseAlgoOrder* cl = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CL", shift, AlgoRole::CLOSE_SIGNAL);
        CHECK(cl && Near(cl->ttCLStartSpread, 0.0010 - shift), "A10 CL 让利：StartSpread 下移 shift");
        CHECK(cl && Near(cl->ttCLEndSpread, 0.0011 - shift),   "A10 CL 让利：EndSpread 下移 shift");
        delete cl;

        pi.orderParams.ttCSStartSpread = -0.0010;
        pi.orderParams.ttCSEndSpread   = -0.0011;
        BaseAlgoOrder* cs = fx.ctx.BuildAlgoOrderJson(pi, "TT", "CS", shift, AlgoRole::CLOSE_SIGNAL);
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
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
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
        BaseAlgoOrder* o = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
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
        // ⚠️ 期望值在 2026-10-01 随生产代码一起变了（提交 4f3e2a5）：
        //    MT 的 rebalance 开关/标志仍是 true，**TT 的两个都改成了 false**
        //    （原来四个都是 true）。所以这里不能再断言"四个都是 true"。
        //    这四个值现在都在 PairTradingConfig 里，改配置即可切换。
        CHECK(o && o->mtRebalanceSwitch && o->mtRebalanceFlag,
              "A12 MT rebalance 开关/标志 = true");
        CHECK(o && !o->ttRebalanceSwitch && !o->ttRebalanceFlag,
              "A12 TT rebalance 开关/标志 = false（2026-10-01 起）");
        CHECK(o && !o->mtPriceTrendProtectFlag && !o->ttPriceTrendProtectFlag, "A12 价格趋势保护 = false");
        CHECK(o && Near(o->maxTTOrderSize, 1.0) && Near(o->maxMTOrderSize, 1.0), "A12 max{TT,MT}OrderSize = 1.0");
        CHECK(o && o->targetSpreadType == stra::TargetSpredPrice_NOW, "A12 targetSpreadType = NOW");
        CHECK(o && o->activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct,
              "A12 activeVolumeCalcualteType = PassiveVolumePct");
        CHECK(o && o->isManual == true, "A12 isManual 跟随 pi.manualFlag");
        delete o;
    }

    // A12b 配置化参数块：改算法单参数 -> 算法单必须跟着变
    //
    // 这一条是"参数配置化"的端到端验证：config.json(op.pairTrading) ->
    // PairTradingConfig -> BaseAlgoOrder。
    // 为什么 A12 不够：A12 断言的**默认值**恰好等于改造前硬编码的那一套，
    // 所以"BuildAlgoOrderJson 真的读了 m_cfg"和"还在写死字面量"
    // 在 A12 眼里完全一样 —— 两条路径都得 PASS。
    // 这里把算法单参数全部改成非默认值再建单，才能证明接线是通的。
    // 顺带覆盖 *CancelOrderTimeMs -> *CancelOrderTime 的 ×1000 换算
    // （解析阶段不换算，×1000 只发生在 BuildAlgoOrderJson 一处）。
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);

        PairTradingConfig& aoc = fx.ctx.m_cfg;
        aoc.ttActiveOrderType = OT_MARKET;      // TT 主动腿
        aoc.mtActiveOrderType = OT_LIMIT;       // MT 主动腿
        aoc.passiveOrderType  = OT_POST_ONLY;   // 被动腿

        aoc.activeDepthMakerCheck  = true;
        aoc.activeDepthTakerCheck  = true;
        aoc.passiveDepthMakerCheck = true;
        aoc.passiveDepthTakerCheck = true;

        aoc.activePriceTakerPct  = 0.011;
        aoc.activePriceMakerPct  = 0.012;
        aoc.passivePriceTakerPct = 0.013;
        aoc.passivePriceMakerPct = 0.014;
        aoc.passiveVolumePct     = 0.6;

        aoc.activeMakerCancelOrderTimeMs  = 7000;
        aoc.activeTakerCancelOrderTimeMs  = 8000;
        aoc.passiveMakerCancelOrderTimeMs = 9000;
        aoc.passiveTakerCancelOrderTimeMs = 10000;

        aoc.activePassiveCancelOrderPct = 0.021;
        aoc.activeMakerCancelOrderPct   = 0.022;
        aoc.activeTakerCancelOrderPct   = 0.023;
        aoc.passiveMakerCancelOrderPct  = 0.024;
        aoc.passiveTakerCancelOrderPct  = 0.025;

        aoc.maxMTOrderSize = 0.3;
        aoc.maxTTOrderSize = 0.4;
        aoc.targetSpreadType = stra::TargetSpredPrice_NOW_MEAN;

        aoc.mtRebalanceSwitch = false;
        aoc.ttRebalanceSwitch = true;
        aoc.mtRebalanceFlag   = false;
        aoc.ttRebalanceFlag   = true;
        aoc.mtPriceTrendProtectFlag = true;
        aoc.ttPriceTrendProtectFlag = true;
        aoc.activePriceTickFlag  = true;
        aoc.activePriceTickNum   = 3;
        aoc.passivePriceTickFlag = true;
        aoc.passivePriceTickNum  = 4;

        BaseAlgoOrder* tt = fx.ctx.BuildAlgoOrderJson(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(tt != nullptr, "A12b TT/OL 能建单");
        if (tt) {
            CHECK(tt->activeOrderType == OT_MARKET,      "A12b TT 主动腿 = 配置的 OT_MARKET");
            CHECK(tt->passiveOrderType == OT_POST_ONLY,  "A12b 被动腿 = 配置的 OT_POST_ONLY");
            CHECK(tt->activeDepthMakerCheck && tt->activeDepthTakerCheck &&
                      tt->passiveDepthMakerCheck && tt->passiveDepthTakerCheck,
                  "A12b 四个深度检查 = 配置的 true");
            CHECK(Near(tt->activePriceTakerPct, 0.011) && Near(tt->activePriceMakerPct, 0.012) &&
                      Near(tt->passivePriceTakerPct, 0.013) && Near(tt->passivePriceMakerPct, 0.014),
                  "A12b 四个价格比例 = 配置值");
            CHECK(Near(tt->passiveVolumePct, 0.6), "A12b passiveVolumePct = 配置的 0.6");
            // ms -> us 的 ×1000 换算
            CHECK(tt->activeMakerCancelOrderTime == 7000LL * 1000,
                  "A12b activeMakerCancelOrderTime = 7000ms × 1000 = 7e6 us");
            CHECK(tt->activeTakerCancelOrderTime == 8000LL * 1000,
                  "A12b activeTakerCancelOrderTime = 8000ms × 1000 = 8e6 us");
            CHECK(tt->passiveMakerCancelOrderTime == 9000LL * 1000,
                  "A12b passiveMakerCancelOrderTime = 9000ms × 1000 = 9e6 us");
            CHECK(tt->passiveTakerCancelOrderTime == 10000LL * 1000,
                  "A12b passiveTakerCancelOrderTime = 10000ms × 1000 = 1e7 us");
            CHECK(Near(tt->activePassiveCancelOrderPct, 0.021) && Near(tt->activeMakerCancelOrderPct, 0.022) &&
                      Near(tt->activeTakerCancelOrderPct, 0.023) && Near(tt->passiveMakerCancelOrderPct, 0.024) &&
                      Near(tt->passiveTakerCancelOrderPct, 0.025),
                  "A12b 五个撤单比例 = 配置值");
            CHECK(Near(tt->maxMTOrderSize, 0.3) && Near(tt->maxTTOrderSize, 0.4),
                  "A12b max{MT,TT}OrderSize = 配置的 0.3 / 0.4");
            CHECK(tt->targetSpreadType == stra::TargetSpredPrice_NOW_MEAN,
                  "A12b targetSpreadType = 配置的 NOW_MEAN");
            CHECK(!tt->mtRebalanceSwitch && tt->ttRebalanceSwitch &&
                      !tt->mtRebalanceFlag && tt->ttRebalanceFlag,
                  "A12b rebalance 开关/标志 = 配置值（与默认相反）");
            CHECK(tt->mtPriceTrendProtectFlag && tt->ttPriceTrendProtectFlag,
                  "A12b 价格趋势保护 = 配置的 true");
            CHECK(tt->activePriceTickFlag && tt->activePriceTickNum == 3 &&
                      tt->passivePriceTickFlag && tt->passivePriceTickNum == 4,
                  "A12b 价格 tick 偏移 = 配置值");
            delete tt;
        }

        // MT 走的是 mtActiveOrderType，不是 ttActiveOrderType
        BaseAlgoOrder* mt = fx.ctx.BuildAlgoOrderJson(pi, "MT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(mt != nullptr, "A12b MT/OL 能建单");
        if (mt) {
            CHECK(mt->activeOrderType == OT_LIMIT, "A12b MT 主动腿 = 配置的 mtActiveOrderType(OT_LIMIT)");
            CHECK(mt->passiveOrderType == OT_POST_ONLY, "A12b MT 被动腿同样 = 配置值");
            delete mt;
        }
    }

    // A13 SubmitAlgoOrder 的副作用
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(fx.N() == 1, "A13 报单回调被调用一次");
        CHECK(pi.hasActiveAlgoOrder, "A13 hasActiveAlgoOrder = true");
        // algoOrderId 已从字符串（"PT_<n>"）改成 int64_t，两边直接比，
        // 不再有 to_string / stoll 的往返
        CHECK(fx.N() == 1 && fx.submits[0]->algoOrderId == pi.currentAlgoOrderId,
              "A13 currentAlgoOrderId == 算法单 algoOrderId（int64 直比）");
        CHECK(pi.satisfyTime > 0 && pi.algoModifyTime == pi.satisfyTime,
              "A13 satisfyTime / algoModifyTime 一起刷新且相等");
    }

    // A14 建单失败 -> 不占对子、不回调
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        pi.orderParams.ttOLSwitch = false;      // 建单会失败
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(fx.N() == 0, "A14 建单失败 -> 回调不触发");
        CHECK(!pi.hasActiveAlgoOrder, "A14 建单失败 -> 不占用对子");
    }

    // A15 无回调时安全返回
    {
        Fx fx; Boot(fx); GoTrading(fx);
        fx.ctx.SetAlgoCommandCallback(nullptr);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.SubmitAlgoOrder(pi, "TT", "OL", 0.0, AlgoRole::OPEN_SIGNAL);
        CHECK(fx.N() == 0 && !pi.hasActiveAlgoOrder, "A15 未注册回调 -> 直接返回，不占对子");
    }

    // A16 ID 生成
    // ⚠️ PairTradingContext::GenerateAlgoOrderId() 已经删掉了：算法单 id 现在由
    //    basic/Utility.h 的 GenerateStrategyAlgoPairId() 统一生成（BuildAlgoOrderJson:670
    //    与 AlgoContext.cpp:91/241 都调它），类型是 int64_t，不再是 "PT_<n>" 字符串。
    //    所以这条改成直接测那个函数。
    {
        int64_t a = GenerateStrategyAlgoPairId();
        int64_t b = GenerateStrategyAlgoPairId();
        CHECK(a > 0 && b > 0 && a != b, "A16 GenerateStrategyAlgoPairId 唯一且为正");
        CHECK(std::to_string(a).find_first_not_of("0123456789") == std::string::npos,
              "A16 ID 是纯数字（快照落盘 / 回传都用 int64，不需要再解析）");
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
            // 期望值 2026-10-01 随生产变（提交 4f3e2a5）：TT -> OT_LIMIT、MT -> OT_POST_ONLY
            CHECK(o->activeOrderType == (std::string(c.mode) == "TT" ? OT_LIMIT : OT_POST_ONLY),
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
            CHECK(o->role == AlgoRole::CLOSE_SIGNAL,
                  "B5 平仓优先：报出的是平仓单（role = CLOSE_SIGNAL）");
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
            // 2026-10-28：平仓单不再清开仓开关（它允许在开仓带回补），改由 role 决定
            // "平到 0 算结束"（AlgoContext:1376）。这里钉住的是"报出来的确实是平仓角色"。
            CHECK(fx.submits[0]->role == AlgoRole::CLOSE_SIGNAL,
                  (std::string("C ") + c.sw + " 报出的是平仓角色（role = CLOSE_SIGNAL）").c_str());
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
        // TT_CL 与 MT_CL 的主动腿类型不同，用这个区分走了哪条路。
        // 期望值 2026-10-01 随生产变（提交 4f3e2a5）：TT -> OT_LIMIT、MT -> OT_POST_ONLY。
        CHECK(fx.N() == 1 && fx.submits[0]->activeOrderType == OT_LIMIT,
              "C6 优先走 TT_CL（TT 主动腿 = OT_LIMIT，与 MT 的 OT_POST_ONLY 可区分）");
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
        PumpSignal(fx.ctx, t0);                             // T_signal 是异步线程，测试里显式驱动一轮
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
        pi.currentAlgoOrderId = 999999LL;
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

    // D10 id 生命周期（原「坏 id -> 不改参」）
    //
    // ⚠️ 原来的用例塞一个 "not-a-number" 字符串进 currentAlgoOrderId，验证
    //    ProcessModify 解析失败时不改参。**这个前提已经不存在了**：currentAlgoOrderId
    //    现在是 int64_t（PairInfo.h:281），没有任何解析步骤，也就没有"坏 id"这个状态。
    //    改成验证真正的契约：SetActiveAlgoOrder / ClearActiveAlgoOrder 必须把
    //    currentAlgoOrderId 与 hasActiveAlgoOrder 一起维护 —— 一旦脱钩，
    //    ProcessModify（只看 hasActiveAlgoOrder）就会拿着 id=0 去改单。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& pim = PairInfoManager::Instance();

        pim.SetActiveAlgoOrder(pi.pairInstrumentKey, 424242LL);
        CHECK(pi.hasActiveAlgoOrder && pi.currentAlgoOrderId == 424242LL,
              "D10 SetActiveAlgoOrder 同时置 id 与 hasActiveAlgoOrder");

        pi.algoModifyTime = 0;
        fx.ctx.ProcessModify(pi, 1000 * SEC);
        CHECK(fx.modifyCalls.size() == 1 && fx.modifyCalls[0].first == 424242LL,
              "D10 改参回调带的就是刚设进去的那个 id");

        pim.ClearActiveAlgoOrder(pi.pairInstrumentKey);
        CHECK(!pi.hasActiveAlgoOrder && pi.currentAlgoOrderId == 0,
              "D10 ClearActiveAlgoOrder 同时清 id 与 hasActiveAlgoOrder");

        fx.modifyCalls.clear();
        pi.algoModifyTime = 0;
        fx.ctx.ProcessModify(pi, 2000 * SEC);
        CHECK(fx.modifyCalls.empty(), "D10 清空后不再改参");
    }

    // D11 快照落盘周期（异步：OnTimer 只"发布"到队列，写文件在 T_signal 上）
    //
    // 改这个功能要守的不变量：
    //   ① 周期门控还是 csvSaveIntervalSec —— 断言 m_lastCsvSaveUs（交易线程上的确定性状态）。
    //   ② OnTimer 真的把行推进了队列：read_available() = "已发布、未被 T_signal 消费"的行数，
    //      这是唯一能直接观测"发布了没有"的量（没有计数器了）。
    //   ③ **发布出去的东西真的能变成文件**，而且内容与同步 SaveSnapshot 逐字节相同
    //      （两条路径共用 WritePairInfoRecordFile，一旦谁改了列序/精度，这条立刻红）。
    //   ④ 连续两批落盘，第二批不能混进哨兵 —— 哨兵只是"一批结束"的标记，不是数据行。
    {
        const std::string csv = "/tmp/ptsuite/out/timer_snapshot.csv";
        std::remove(csv.c_str());
        Fx fx; Boot(fx, csv); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        const int64_t t0 = 1000 * SEC;

        auto readAll = [](const std::string& p) {
            std::ifstream ifs(p);
            std::stringstream ss; ss << ifs.rdbuf();
            return ss.str();
        };

        // 落盘挂在 T_signal 上，而它是异步线程 —— 先停掉（join），之后每一步都是确定的，
        // stat()/读文件不会和后台那一轮 RunSignalOnce 赛跑。
        fx.ctx.StopSignalThread();

        fx.ctx.OnTimer(t0);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0, "D11 首轮到点 -> 发布一次快照");
        CHECK(fx.ctx.m_snapQ.read_available() == 2u, "D11 首轮发布 1 行 + 1 哨兵");

        // 手工推一轮 T_signal（生产由 SignalWork 按真实时间循环调，走的是同一段代码）。
        // lastStatsUpdateUs 传 nowUs：这一轮**不**进"到点重算分位数"分支，
        // 免得 Build/统计落盘混进来 —— 这里只验第 3 步。
        int64_t lastStats = t0;
        fx.ctx.RunSignalOnce(t0, lastStats);
        CHECK(fx.ctx.m_snapQ.read_available() == 0u, "D11 一轮就把队列排空");
        struct stat st{};
        CHECK(::stat(csv.c_str(), &st) == 0 && st.st_size > 0, "D11 快照文件已写出");

        // ③ 异步路径与同步路径的输出必须逐字节相同
        const std::string sync = "/tmp/ptsuite/out/timer_snapshot_sync.csv";
        std::remove(sync.c_str());
        PairInfoManager::Instance().SaveSnapshot(sync);
        const std::string a = readAll(csv);
        CHECK(!a.empty() && a == readAll(sync), "D11 异步落盘与 SaveSnapshot 逐字节相同");

        fx.ctx.OnTimer(t0 + 5 * SEC);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0, "D11 未到 csvSaveIntervalSec -> 不重复发布");
        CHECK(fx.ctx.m_snapQ.read_available() == 0u, "D11 未到点 -> 队列里没有新行");

        fx.ctx.OnTimer(t0 + 11 * SEC);
        CHECK(fx.ctx.m_lastCsvSaveUs == t0 + 11 * SEC, "D11 到点 -> 再发布一次");
        CHECK(fx.ctx.m_snapQ.read_available() == 2u, "D11 到点 -> 又发布 1 行 + 1 哨兵");

        // 第二批：状态没变 ⇒ 文件必须与第一轮逐字节一致。
        // ④ 就在这一条上：哨兵若被 push 进 m_snapWrite，第二批会多出一行空 key 的垃圾，
        //    这里立刻红（症状延迟一轮才出现，所以必须连推两批才抓得到）。
        fx.ctx.RunSignalOnce(t0 + 11 * SEC, lastStats);
        CHECK(readAll(csv) == a, "D11 第二轮内容与第一轮一致（哨兵没混进数据）");
    }

    // D12 m_pairKeys 里存在"查不到 PairInfo"的 key -> 快照不能多写空行
    //
    // 为什么会有这种 key：Init 里 key 不合法（没有 `|`）只 LOG_WARN + continue，
    // 它留在 m_pairKeys 里但不建 PairInfo（PairInfoManager.cpp:28-31）。
    // 于是 CollectSnapshotRows 收不满 total 行 —— 如果不按 k 截断，
    // SaveSnapshot 会把尾部那些"从没写过"的行也交给写文件函数（它按 v.size() 遍历）。
    {
        ResetWorld();
        Fx fx;
        fx.smc.Clear();
        fx.smc.Set(kActive, MakeInstrumentInfo());
        fx.smc.Set(kPassive, MakeInstrumentInfo());

        const std::string csv = "/tmp/ptsuite/out/badkey_snapshot.csv";
        std::remove(csv.c_str());

        PairTradingConfig cfg;
        cfg.pairKeys = {kPairKey, "bad-key-without-separator"};   // 第二个 key 没有 `|`
        cfg.activeAccountId = 10000;
        cfg.passiveAccountId = 10001;
        cfg.csvStatePath = csv;
        cfg.spreadStatsStatePath = "";     // 别往相对路径写统计（同 Boot 的理由）
        fx.csvPath = csv;
        fx.Wire();
        fx.ctx.Init(cfg, &fx.smc);

        std::vector<PairInfoRecord> rows;
        const int n = PairInfoManager::Instance().CollectSnapshotRows(rows);
        CHECK(n == 1, "D12 坏 key 不进快照（只收 1 行）");
        CHECK(rows.size() == 1, "D12 out 按实际行数截断（不是 m_pairKeys 的长度 2）");

        PairInfoManager::Instance().SaveSnapshot(csv);
        std::ifstream ifs(csv);
        std::string line;
        int lines = 0;
        while (std::getline(ifs, line)) {
            ++lines;
        }
        CHECK(lines == 2, "D12 文件 = 表头 + 1 行（坏 key 不写空行）");
    }

    // D13 全局持仓闸门：所有对子持仓市值之和 > maxPositionValue -> 只平不开
    //
    // 这条用例要守住的三件事：
    //   ① 判据是**跨对子求和**，不是单对子 —— 所以只能在 PairTradingContext 里算
    //      （RecalcOrderParams 是单对子签名，自己算不出全局和）；
    //   ② 落点是 4 个开仓开关，4 个平仓开关一个都不能动（"只平不开"的字面意思）；
    //   ③ 闸门在 RecalcOrderParams **内部**生效。它还有 Init / OnAlgoFinished / OnTimer 第 2 步
    //      （统计刷新）这些调用点，只在 OnTimer 里压开关的话，那些调用点会立刻把开关重新打开 ——
    //      闸门就只存在于两次调用之间的缝隙里，等于没有。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);   // 腿价 100、面值 1 => 市值 100
        auto& sg  = SignalGenerator::Instance();
        auto& pim = PairInfoManager::Instance();

        // ① 现算，不走缓存的 pi.positionValue（那个只在 UpdateOnAlgoOrder 里刷，两次成交之间会滞后）
        CHECK(Near(pim.TotalPositionValue(), 100.0),
              "D13 TotalPositionValue 现算 = |量 × 腿价 × 面值|");

        // ② 对照组：闸门关着 + 统计有效 -> 逻辑自己把 4 个开仓开关打开。
        //    没有这一步，下面"被压掉"就证明不了是闸门干的（也可能是逻辑本来就不想开）。
        MakeOpenFriendlyStats(pi);
        sg.SetGlobalPositionExceeded(false);
        sg.RecalcOrderParams(pi);
        CHECK(pi.orderParams.ttOLSwitch && pi.orderParams.ttOSSwitch &&
              pi.orderParams.mtOLSwitch && pi.orderParams.mtOSSwitch,
              "D13 对照组：闸门关 -> 逻辑把 4 个开仓开关都打开");

        // ③ 集成：OnTimer 第 0.5 步从 TotalPositionValue 推结论，第 1 步的 RecalcOrderParams 落闸
        fx.ctx.m_cfg.maxPositionValue = 50.0;               // 100 > 50
        fx.ctx.OnTimer(1000 * SEC);
        CHECK(sg.GetGlobalPositionExceeded(), "D13 总市值 100 > 50 -> OnTimer 置位闸门");
        CHECK(!pi.orderParams.ttOLSwitch && !pi.orderParams.ttOSSwitch &&
              !pi.orderParams.mtOLSwitch && !pi.orderParams.mtOSSwitch,
              "D13 闸门置位 -> 4 个开仓开关全压掉");
        CHECK(pi.orderParams.ttCLSwitch && pi.orderParams.ttCSSwitch &&
              pi.orderParams.mtCLSwitch && pi.orderParams.mtCSSwitch,
              "D13 闸门只管开仓，4 个平仓开关一个都没动");

        // ④ 单独调一次（模拟 OnAlgoFinished / Init 这些非 OnTimer 的调用点）：
        //    开关先被手工恢复成"想开仓"，调完仍必须是关的。
        pi.orderParams.ttOLSwitch = pi.orderParams.ttOSSwitch = true;
        pi.orderParams.mtOLSwitch = pi.orderParams.mtOSSwitch = true;
        sg.RecalcOrderParams(pi);
        CHECK(!pi.orderParams.ttOLSwitch && !pi.orderParams.ttOSSwitch &&
              !pi.orderParams.mtOLSwitch && !pi.orderParams.mtOSSwitch,
              "D13 闸门在 RecalcOrderParams 内部：非 OnTimer 的调用点也压得住");
    }

    // D14 低于上限 -> 不置位；已经置位的要能解除
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);   // 市值 100
        auto& sg = SignalGenerator::Instance();
        fx.ctx.m_cfg.maxPositionValue = 500.0;              // 100 <= 500

        fx.ctx.OnTimer(1000 * SEC);
        CHECK(!sg.GetGlobalPositionExceeded(), "D14 总市值 100 <= 500 -> 闸门不置位");

        // 减仓减到上限以下之后必须能解除 —— 否则闸门只进不出，越减越开不了新仓。
        sg.SetGlobalPositionExceeded(true);
        fx.ctx.OnTimer(1000 * SEC + 1 * SEC);
        CHECK(!sg.GetGlobalPositionExceeded(), "D14 已置位的闸门在市值回落后解除");

        // 统计无效时 RecalcOrderParams 早退、不动开关，所以这里看到的就是"闸门没干预"的结果
        CHECK(pi.orderParams.ttOLSwitch && pi.orderParams.mtOSSwitch,
              "D14 闸门不置位 -> 不干预开仓开关");
    }

    // D15 maxPositionValue <= 0 = 关闭：有持仓也不置位
    //
    // 量取 -2.0 而不是 -1000：这个分支在算 TotalPositionValue **之前**就跳过了，
    // 持仓多大都无关，取个小值只是别让 ProcessRisk 在同一个 tick 里被顺带触发。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -2.0);
        auto& sg = SignalGenerator::Instance();
        fx.ctx.m_cfg.maxPositionValue = 0.0;

        CHECK(PairInfoManager::Instance().TotalPositionValue() > 0.0, "D15 前置：确实有持仓");
        fx.ctx.OnTimer(1000 * SEC);
        CHECK(!sg.GetGlobalPositionExceeded(), "D15 maxPositionValue=0 -> 关闭，有持仓也不置位");
        CHECK(pi.orderParams.ttOLSwitch && pi.orderParams.mtOSSwitch, "D15 关闭态不干预开关");
    }

    // D16 统计无效的早退路径也必须过闸门
    //
    // 这条最容易漏：RecalcOrderParams 在 !largeStats.IsValid() 时直接 return，
    // 而那条路径上 4 个开仓开关**保持上一轮的值**。偏偏"刚重启、带着快照恢复的大仓位、
    // 分位数还没攒够"就是这条路径，也正是最该只平不开的时刻。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        auto& sg = SignalGenerator::Instance();
        pi.largeStats = SpreadStats{};                      // 回到"统计还没攒够"
        CHECK(!pi.largeStats.IsValid(), "D16 前置：统计无效");

        // 4 个平仓开关先压成 false：早退路径不会重写它们（重写发生在后面的完整路径上），
        // 所以"调完还是 false"才真正证明闸门只碰了开仓那 4 个 —— 若断言"是 true"，
        // MakeHoldable 本来就把它们设成 true，什么都证明不了。
        pi.orderParams.ttCLSwitch = pi.orderParams.ttCSSwitch = false;
        pi.orderParams.mtCLSwitch = pi.orderParams.mtCSSwitch = false;

        sg.SetGlobalPositionExceeded(true);
        sg.RecalcOrderParams(pi);
        CHECK(!pi.orderParams.ttOLSwitch && !pi.orderParams.ttOSSwitch &&
              !pi.orderParams.mtOLSwitch && !pi.orderParams.mtOSSwitch,
              "D16 统计无效的早退路径也压掉 4 个开仓开关");
        CHECK(!pi.orderParams.ttCLSwitch && !pi.orderParams.ttCSSwitch &&
              !pi.orderParams.mtCLSwitch && !pi.orderParams.mtCSSwitch,
              "D16 闸门只碰开仓：平仓开关原样保持 false");
    }

    // D17 开仓阈值的"绝对地板"（minSpreadTarget > 0；祖先 cc_pricespread_gb_ltp.py:781-782）
    //
    // 分位数只回答"历史上有多少价差"，回答不了"多少价差才够覆盖两边的手续费"。
    // 分位带宽只有 6bp 时，光靠 canOpen* 那道开关只是把开仓"关掉" —— 市场真冲到 20bp
    // 也不会报单；祖先的做法是把阈值钉在地板上，等市场来撞。
    //
    // 地板的量纲：它夹的是**含执行成本**的 StartSpread，所以毛价差要求 = 地板 + F。
    // 换句话说 0.002 的含义是"扣掉两边成本后净赚千 2"，不是"毛价差千 2"。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        auto& sg = SignalGenerator::Instance();

        // ⚠️ 单例状态跨用例泄漏：D16 把全局持仓闸门留在 true，这里不先复位的话
        //    下面 RecalcOrderParams 末尾的 ApplyNoOpenGate 会把 4 个开仓开关全压掉。
        sg.SetGlobalPositionExceeded(false);
        // main() 里写死的那套费率，末尾必须还原 —— 后面 E~K 组都按它算期望值
        const FeeSlippageConfig orig = sg.GetConfig();

        // 浅分位数：mtOL 阈值 = -3bp×0.8 + F_mt ≈ +0.7bp，比地板浅得多
        auto& s = pi.largeStats;
        s = SpreadStats{};
        s.bidAskUQ = s.bidAskDQ = -0.0003;
        s.bidBidUQ = s.askBidUQ =  0.0003;
        s.bidBidDQ = s.askBidDQ = -0.0003;
        s.askAskUQ =  0.0003;
        s.askAskDQ = -0.0003;
        s.count = 100;
        s.valid = true;

        FeeSlippageConfig fs = orig;
        fs.minSpreadTarget = 0.002;      // 千 2
        sg.SetConfig(fs);
        sg.RecalcOrderParams(pi);

        const double F_mt = sg.CalcExecCost(false);
        const double floorMT = fs.minSpreadTarget + 0.000005;
        const double floorTT = floorMT + fs.ttAddPercent;
        const auto& op = pi.orderParams;

        CHECK(Near(op.mtOLStartSpread, -floorMT), "D17 浅分位数 -> mtOL 抬到地板 -20.05bp");
        CHECK(Near(op.mtOSStartSpread,  floorMT), "D17 浅分位数 -> mtOS 抬到地板 +20.05bp");
        CHECK(Near(op.ttOLStartSpread, -floorTT), "D17 TT 地板再深一个 ttAddPercent");
        CHECK(Near(op.ttOSStartSpread,  floorTT), "D17 TT 地板再深一个 ttAddPercent");

        // End 必须跟着 Start 走：只夹 Start 会让 End 比 Start 更浅、阶梯反向，
        // AlgoPairOrder 里 (End-Start) 还可能变成 0（除零）。
        CHECK(Near(op.mtOLEndSpread - op.mtOLStartSpread, -0.000005), "D17 mtOL End 跟着走（-5e-6）");
        CHECK(Near(op.mtOSEndSpread - op.mtOSStartSpread,  0.000005), "D17 mtOS End 跟着走（+5e-6）");
        CHECK(Near(op.ttOLEndSpread - op.ttOLStartSpread, -0.000005), "D17 ttOL End 跟着走（-5e-6）");
        CHECK(Near(op.ttOSEndSpread - op.ttOSStartSpread,  0.000005), "D17 ttOS End 跟着走（+5e-6）");

        CHECK(op.mtOLSwitch && op.mtOSSwitch && op.ttOLSwitch && op.ttOSSwitch,
              "D17 夹逼后 4 个开仓开关都开着 —— 是'钉住等'，不是'关掉'");

        // 端到端：毛价差要过 地板 + F 才打（F 是含在 StartSpread 里的）
        pi.rtSpread.spreadAskAsk = -(floorMT + F_mt) + 1e-9;
        CHECK(!sg.CheckSignalForSatisfy(pi).mtOLSignal, "D17 毛价差差一点点 -> 不打");
        pi.rtSpread.spreadAskAsk = -(floorMT + F_mt) - 1e-9;
        CHECK(sg.CheckSignalForSatisfy(pi).mtOLSignal, "D17 毛价差过了地板+F -> 打");

        // 只抬高、不压低：分位数比地板深时原样保留
        s.askAskDQ = -0.02;              // -200bp
        s.bidBidUQ =  0.02;
        sg.RecalcOrderParams(pi);
        CHECK(op.mtOLStartSpread < -floorMT, "D17 分位数比地板深 -> 不压低");
        CHECK(Near(op.mtOLEndSpread - op.mtOLStartSpread, -0.000005),
              "D17 未夹逼时 5e-6 阶梯也不变");

        sg.SetConfig(orig);
    }

    // D18 费率只通过"地板"影响触发点（D17 那条恒等式的可执行证据）
    //
    // D17 证明地板把阈值钉住了；这一节证明**为什么平时改费率不影响报单**：
    //   CheckSignal 比的是 `裸价差 + F` 与 `StartSpread = 分位×0.8 + F`，
    //   F 在两边各出现一次 ⇒ 触发点（裸价差）恒等于 分位×0.8，与 F 无关。
    // 但地板夹的是含 F 的 StartSpread、夹逼值本身不含 F ⇒ 这一层约不掉：
    //   触发点（裸价差）= 地板 + F（随费率平移），而净额（毛 − F）恒等于地板。
    // 两个断言都钉住：以后谁把某一侧的 F 删了、或把符号写反，这里立刻红。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        auto& sg = SignalGenerator::Instance();

        // ⚠️ 同 D17：D16 把全局持仓闸门留在 true，不复位会被 ApplyNoOpenGate 压掉开仓开关
        sg.SetGlobalPositionExceeded(false);
        const FeeSlippageConfig orig = sg.GetConfig();

        // 分位取 ±20bp，这个数被两边同时约束住：
        //   · ② 要触发夹逼 ⇒ 分位必须比地板**浅**（20bp < 20.05bp）。分位比地板深时
        //     夹逼不生效（"只抬高不压低"），② 就退化成空断言 —— 第一版取 ±30bp 正是这么红的。
        //   · ① 要开关为真 ⇒ 分位必须深到连 11bp 的 F 也压不穿 0（需 > F/0.8 = 13.75bp）。
        auto& s = pi.largeStats;
        s = SpreadStats{};
        s.bidAskUQ = s.bidAskDQ = -0.002;
        s.bidBidUQ = s.askBidUQ =  0.002;
        s.bidBidDQ = s.askBidDQ = -0.002;
        s.askAskUQ =  0.002;
        s.askAskDQ = -0.002;
        s.count = 100;
        s.valid = true;

        // 两组费率，F_mt 正好差 10bp（F_mt = activeMaker + passiveTaker + 滑点）
        auto withFees = [&](double am, double at, double pt, double slip) {
            FeeSlippageConfig fs = orig;
            fs.activeMakerFeeRate = am;
            fs.activeTakerFeeRate = at;
            fs.passiveTakerFeeRate = pt;
            fs.basicSlippage = slip;
            return fs;
        };
        const FeeSlippageConfig cheap = withFees(-0.0002, 0.0002, 0.0002, 0.0001); // F_mt = 1bp
        const FeeSlippageConfig dear  = withFees( 0.0008, 0.0008, 0.0002, 0.0001); // F_mt = 11bp

        auto recalcWith = [&](const FeeSlippageConfig& fs) {
            sg.SetConfig(fs);
            sg.RecalcOrderParams(pi);
        };

        // ---- ① 地板关掉：触发点（裸价差）与费率无关 ----
        FeeSlippageConfig offCheap = cheap; offCheap.minSpreadTarget = 0.0;
        FeeSlippageConfig offDear  = dear;  offDear.minSpreadTarget  = 0.0;

        recalcWith(offCheap);
        const double rawCheap = pi.orderParams.mtOLStartSpread - sg.CalcExecCost(false);
        recalcWith(offDear);
        const double rawDear = pi.orderParams.mtOLStartSpread - sg.CalcExecCost(false);

        CHECK(Near(rawCheap, rawDear, 1e-12),
              "D18 地板关：费率差 10bp，触发点仍相同 —— F 在比较式两边抵消");
        CHECK(Near(rawCheap, s.askAskDQ * offCheap.spreadAdjPct, 1e-12),
              "D18 地板关：触发点 = 分位×spreadAdjPct，式子里没有 F");

        // 端到端：同一个裸价差，两组费率下报不报必须完全一致
        const FeeSlippageConfig both[2] = {offCheap, offDear};
        for (int i = 0; i < 2; ++i) {
            recalcWith(both[i]);
            pi.rtSpread.spreadAskAsk = rawCheap + 1e-9;
            CHECK(!sg.CheckSignalForSatisfy(pi).mtOLSignal, "D18 地板关：差一点，两组都不打");
            pi.rtSpread.spreadAskAsk = rawCheap - 1e-9;
            CHECK(sg.CheckSignalForSatisfy(pi).mtOLSignal, "D18 地板关：过线，两组都打");
        }

        // ---- ② 地板打开：毛门槛随 F 平移，净额恒定 ----
        const double target = 0.002;
        const double floorMT = target + 0.000005;

        FeeSlippageConfig onCheap = cheap; onCheap.minSpreadTarget = target;
        FeeSlippageConfig onDear  = dear;  onDear.minSpreadTarget  = target;

        recalcWith(onCheap);
        const double Fc = sg.CalcExecCost(false);
        const double rc = pi.orderParams.mtOLStartSpread - Fc;
        recalcWith(onDear);
        const double Fd = sg.CalcExecCost(false);
        const double rd = pi.orderParams.mtOLStartSpread - Fd;

        CHECK(Near(rc, -(floorMT + Fc), 1e-12), "D18 地板开：毛门槛 = 地板 + F（1bp 档）");
        CHECK(Near(rd, -(floorMT + Fd), 1e-12), "D18 地板开：毛门槛 = 地板 + F（11bp 档）");
        CHECK(Near(rc - rd, Fd - Fc, 1e-12),
              "D18 地板开：两组毛门槛之差 = 两组 F 之差（纯平移，贵的更深）");
        CHECK(Near(-rc - Fc, floorMT, 1e-12) && Near(-rd - Fd, floorMT, 1e-12),
              "D18 地板开：净额（毛 − F）两组都等于地板 —— 0.002 的含义是净 20bp");

        sg.SetConfig(orig);
    }

    // D19 开仓的"预期往返净利"门槛（minProfitTarget；祖先 min_profit_target / except_profit_flag）
    //
    // D17 只钉住了入场深度，而"入场深"不等于"赚钱"：出场线是 `0.8×对向分位 + profitPct`
    // （profitPct 只有 1bp），对向分位浅的时候出场线几乎贴在 0 上，于是入场再深，
    // 往返也只赚"地板 − F"。这一节把出场线也算进判据，钉住五件事：
    //   ① 门槛值是**净额**：在"刚好穿过入场阈值"的价格上恒等于 `平仓线 − 开仓线`。
    //      因为开仓判据是 `裸价差 + F < start`、平仓是 `裸价差 − F > start`，各含一次 F，
    //      相减时两次 F 都留下 —— 正好是往返成本，不用再额外加。
    //   ② 边界是 `>=`：等于门槛要放行，差 1e-12 要挡住。
    //   ③ 同一份行情、同一个阈值，只改 minProfitTarget 就能把"打"翻成"不打"。
    //   ④ 它**只管开仓**：门槛拉到 10.0（价差本身不可能到 1000bp）时，4 个平仓信号必须照旧成立。
    //   ⑤ 与地板组合：地板放行 ≠ 有利润。地板管入场深度，门槛管"入场到出场还剩多少"。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        auto& sg = SignalGenerator::Instance();

        // 同 D16/D17：单例状态跨用例泄漏，不复位会被 ApplyNoOpenGate 压掉开仓开关
        sg.SetGlobalPositionExceeded(false);
        const FeeSlippageConfig orig = sg.GetConfig();

        // 分位带宽 60bp（askAskDQ = −30bp / bidBidUQ = +30bp）。
        // 地板关掉，让阈值完全由分位数决定 —— 这一节验的是门槛，不是夹逼。
        auto& s = pi.largeStats;
        s = SpreadStats{};
        s.bidAskUQ = s.bidAskDQ = -0.003;
        s.bidBidUQ = s.askBidUQ =  0.003;
        s.bidBidDQ = s.askBidDQ = -0.003;
        s.askAskUQ =  0.003;
        s.askAskDQ = -0.003;
        s.count = 100;
        s.valid = true;

        FeeSlippageConfig fs = orig;
        fs.minSpreadTarget = 0.0;        // 地板关
        fs.minProfitTarget = 0.0;        // 门槛先关，把阈值算出来
        sg.SetConfig(fs);
        sg.RecalcOrderParams(pi);

        const auto& op = pi.orderParams;
        const double F_mt = sg.CalcExecCost(false);
        const double F_tt = sg.CalcExecCost(true);

        CHECK(op.mtOLSwitch && op.mtOSSwitch && op.ttOLSwitch && op.ttOSSwitch,
              "D19 前提：4 个开仓开关都开着（否则下面的'不打'是开关挡的，不是门槛）");

        // ① 刚好穿过入场阈值时的净利 = 平仓线 − 开仓线
        //   MT: 0.0024 − 0.0009 + 0.0001 − (−0.0024 + 0.0009) = 0.0031 = 31bp
        //   TT: 0.0024 − 0.0010 + 0.0001 − (−0.0024 + 0.0010) = 0.0029 = 29bp
        //       TT 反而窄 2bp：ttAddPercent(3bp) < taker−maker(4bp)，两腿都吃 taker 时
        //       3bp 的缓冲补不平 4bp 的费率差，入场、出场各差 1bp。
        const double pMT = op.mtCLStartSpread - op.mtOLStartSpread;
        const double pTT = op.ttCLStartSpread - op.ttOLStartSpread;
        CHECK(Near(pMT, 0.0031), "D19 MT 门槛值 = 平仓线 − 开仓线 = 31bp");
        CHECK(Near(pTT, 0.0029), "D19 TT 门槛值窄 2bp（ttAdd 3bp 补不平 taker−maker 4bp）");

        // 把某个方向的价差喂到"刚好穿过开仓阈值"，返回那一刻门槛算出来的净利。
        // 返回值与 CheckSignalForSatisfy 里的表达式**逐字相同**，所以下面可以拿它当边界值用。
        auto armOpen = [&](const char* which) -> double {
            OnlySwitch(pi, which);
            const std::string w(which);
            if (w == "mtOL") {
                pi.rtSpread.spreadAskAsk = op.mtOLStartSpread - F_mt - 1e-9;
                return op.mtCLStartSpread - pi.rtSpread.spreadAskAsk - F_mt;
            }
            if (w == "mtOS") {
                pi.rtSpread.spreadBidBid = op.mtOSStartSpread + F_mt + 1e-9;
                return pi.rtSpread.spreadBidBid - op.mtCSStartSpread - F_mt;
            }
            if (w == "ttOL") {
                pi.rtSpread.spreadBidAsk = op.ttOLStartSpread - F_tt - 1e-9;
                return op.ttCLStartSpread - pi.rtSpread.spreadBidAsk - F_tt;
            }
            pi.rtSpread.spreadAskBid = op.ttOSStartSpread + F_tt + 1e-9;
            return pi.rtSpread.spreadAskBid - op.ttCSStartSpread - F_tt;
        };

        // ② 四个方向都走一遍：净利 == 门槛 ⇒ 放行；门槛再高 1e-12 ⇒ 挡住。
        //    顺带把"对称方向净利相同"也钉住（OL 与 OS 若不等，说明 profitPct 的符号写反了）。
        {
            const char* dirs[4] = {"mtOL", "mtOS", "ttOL", "ttOS"};
            double got[4] = {0, 0, 0, 0};
            for (int i = 0; i < 4; ++i) {
                const double p = armOpen(dirs[i]);
                got[i] = p;
                CHECK(p > 0.0025, "D19 前提：阈值处净利为正（否则测的是'挡'，不是'放'）");

                FeeSlippageConfig on = fs;  on.minProfitTarget  = p;
                sg.SetConfig(on);
                CHECK(sg.CheckSignalForSatisfy(pi).hasSignal,
                      "D19 净利 == 门槛 ⇒ 放行（边界是 >=，不是 >）");

                FeeSlippageConfig off = fs; off.minProfitTarget = p + 1e-12;
                sg.SetConfig(off);
                CHECK(!sg.CheckSignalForSatisfy(pi).hasSignal,
                      "D19 净利比门槛差 1e-12 ⇒ 挡住");
            }
            CHECK(Near(got[0], got[1], 1e-12), "D19 MT 开多/开空 的预期净利对称");
            CHECK(Near(got[2], got[3], 1e-12), "D19 TT 开多/开空 的预期净利对称");
            // 喂价故意比阈值深 1e-9（否则不满足严格小于），净利也就比阈值处多 1e-9
            CHECK(Near(got[0], pMT + 1e-9, 1e-12) && Near(got[2], pTT + 1e-9, 1e-12),
                  "D19 阈值处净利与 ① 的两个值一致（喂价深 1e-9，净利也深 1e-9）");
        }

        // ③ 行情完全不动，只改门槛：31bp 的净利，门槛 10bp 打、50bp 不打。
        //    这条是"门槛独立于阈值"的证据 —— 阈值没变，信号却翻了。
        {
            const double p = armOpen("mtOL");   // 31bp
            FeeSlippageConfig lo = fs; lo.minProfitTarget = 0.0010;
            sg.SetConfig(lo);
            CHECK(sg.CheckSignalForSatisfy(pi).mtOLSignal, "D19 门槛 10bp < 净利 31bp ⇒ 打");
            FeeSlippageConfig hi = fs; hi.minProfitTarget = 0.0050;
            sg.SetConfig(hi);
            CHECK(!sg.CheckSignalForSatisfy(pi).mtOLSignal,
                  "D19 门槛 50bp > 净利 31bp ⇒ 不打（行情与阈值都没变）");
            CHECK(p > 0.0030, "D19 ③ 的净利仍是 31bp");
        }

        // ④ 只管开仓：门槛 10.0（价差本身到不了 1000bp）⇒ 开仓全灭、平仓全活。
        //    平仓要是也被这道门槛卡住，止损/风控平仓就永远发不出去 —— 这是最容易写错的一处。
        {
            FeeSlippageConfig huge = fs; huge.minProfitTarget = 10.0;
            sg.SetConfig(huge);

            // 开仓侧：4 条轴全部喂到"必穿"（±1.0 远深于阈值），仍然一个都不许打
            pi.pairTotalVolume = 0.0;
            pi.rtSpread.spreadAskAsk = -1.0;
            pi.rtSpread.spreadBidBid =  1.0;
            pi.rtSpread.spreadBidAsk = -1.0;
            pi.rtSpread.spreadAskBid =  1.0;
            const char* opens[4] = {"mtOL", "mtOS", "ttOL", "ttOS"};
            for (const char* w : opens) {
                OnlySwitch(pi, w);
                const std::string n(w);
                const auto r = sg.CheckSignalForSatisfy(pi);
                const bool fired = (n == "mtOL") ? r.mtOLSignal : (n == "mtOS") ? r.mtOSSignal
                                 : (n == "ttOL") ? r.ttOLSignal : r.ttOSSignal;
                CHECK(!fired, "D19 ④ 门槛 10.0 ⇒ 该开仓方向不报（价差已远超阈值）");
            }

            // 平仓侧：IsLong 要 pairTotalVolume < 0，IsShort 要 > 0
            pi.pairTotalVolume = -1.0;
            OnlySwitch(pi, "mtCL");
            pi.rtSpread.spreadBidBid = op.mtCLStartSpread + F_mt + 1e-9;
            CHECK(sg.CheckSignalForSatisfy(pi).mtCLSignal, "D19 ④ 门槛再高也不挡 mtCL（平仓不受约束）");
            OnlySwitch(pi, "ttCL");
            pi.rtSpread.spreadAskBid = op.ttCLStartSpread + F_tt + 1e-9;
            CHECK(sg.CheckSignalForSatisfy(pi).ttCLSignal, "D19 ④ 门槛再高也不挡 ttCL");

            pi.pairTotalVolume = 1.0;
            OnlySwitch(pi, "mtCS");
            pi.rtSpread.spreadAskAsk = op.mtCSStartSpread - F_mt - 1e-9;
            CHECK(sg.CheckSignalForSatisfy(pi).mtCSSignal, "D19 ④ 门槛再高也不挡 mtCS");
            OnlySwitch(pi, "ttCS");
            pi.rtSpread.spreadBidAsk = op.ttCSStartSpread - F_tt - 1e-9;
            CHECK(sg.CheckSignalForSatisfy(pi).ttCSSignal, "D19 ④ 门槛再高也不挡 ttCS");
        }

        // ⑤ 地板 + 门槛的组合：地板放行 ≠ 有利润
        //    当前配置的 minSpreadTarget = 0.0012，若对向分位 p92 = 0（出场线只剩 profitPct = +1bp）：
        //      入场裸价差 = −(0.001205 + F_mt) = −21.05bp   （F_mt = 9bp）
        //      出场裸价差 = mtCLStart + F_mt  = (−0.0008) + 0.0009 = +1bp
        //      净利 = 22.05 − 2×9 = 4.05bp
        //    ⇒ 门槛 5bp 挡住、门槛 4bp 放行。这就是"地板只管入场深度"留下的缺口：
        //       0.0012 在 F_mt = 9bp 下并不等于净赚 5bp，价差回 0 时只有 3.05bp。
        {
            auto& s2 = pi.largeStats;
            s2 = SpreadStats{};
            s2.bidAskUQ = s2.bidAskDQ = -0.0005;
            s2.bidBidUQ = s2.askBidUQ =  0.0;      // p92 = 0 ⇒ 出场线 = profitPct
            s2.bidBidDQ = s2.askBidDQ = -0.0005;
            s2.askAskUQ =  0.0;
            s2.askAskDQ = -0.0005;
            s2.count = 100;
            s2.valid = true;

            FeeSlippageConfig f2 = orig;
            f2.minSpreadTarget = 0.0012;
            f2.minProfitTarget = 0.0;
            sg.SetConfig(f2);
            pi.pairTotalVolume = 0.0;
            sg.RecalcOrderParams(pi);

            CHECK(op.mtOLSwitch, "D19 ⑤ 地板放行：mtOL 开关开着");
            CHECK(Near(op.mtOLStartSpread, -0.001205), "D19 ⑤ 地板把 mtOL 钉在 −20.05bp");

            const double p = op.mtCLStartSpread - op.mtOLStartSpread;
            CHECK(Near(p, 0.000405), "D19 ⑤ 阈值处净利只有 4.05bp（不是 5bp）");

            OnlySwitch(pi, "mtOL");
            pi.rtSpread.spreadAskAsk = op.mtOLStartSpread - F_mt - 1e-9;

            FeeSlippageConfig g5 = f2; g5.minProfitTarget = 0.0005;
            sg.SetConfig(g5);
            CHECK(!sg.CheckSignalForSatisfy(pi).mtOLSignal,
                  "D19 ⑤ 门槛 5bp > 净利 4.05bp ⇒ 不打（地板过了也没用）");

            FeeSlippageConfig g4 = f2; g4.minProfitTarget = 0.0004;
            sg.SetConfig(g4);
            CHECK(sg.CheckSignalForSatisfy(pi).mtOLSignal, "D19 ⑤ 门槛 4bp < 净利 4.05bp ⇒ 打");
        }

        sg.SetConfig(orig);
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
        pi.currentAlgoOrderId = 777LL;
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + 100 * TIMEOUT);
        CHECK(fx.modifyCalls.empty(), "E2 autoFlag = false（手动单）-> 永不撤单");
    }

    // E3 窗口内 -> 不撤
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.satisfyTime = t0;
        pi.currentAlgoOrderId = 777LL;
        fx.ctx.CheckAlgoOrderTimeout(pi, t0 + TIMEOUT - 1);
        CHECK(fx.modifyCalls.empty(), "E3 idle <= algoOrderTimeoutMs -> 不撤单");
    }

    // E4 超时 -> 撤单
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.satisfyTime = t0;
        pi.currentAlgoOrderId = 777LL;
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
        pi.currentAlgoOrderId = 777LL;
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
        pi.currentAlgoOrderId = 888LL;
        fx.ctx.SetAlgoOrderModifyCallback(nullptr);
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.empty(), "E6 未注册回调 -> 安全返回");
    }

    // E7 撤单回调原样传出 id（原「坏 id -> 不回调」）
    //
    // ⚠️ 原用例的前提（id 是字符串、解析失败就静默不发撤单）随着
    //    currentAlgoOrderId 改成 int64_t 一起消失了：RequestCancelAlgoOrder 现在
    //    只判 hasActiveAlgoOrder（:326），id 是直接透传的。
    //    改成钉住这个新契约：回调里的 id 必须与 pi.currentAlgoOrderId 逐位相等，
    //    包括 0（"有活跃单但 id 还是 0" 这种不一致状态也要原样透传，
    //    因为把它悄悄改成别的值会让执行端撤错单）。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.hasActiveAlgoOrder = true;
        pi.currentAlgoOrderId = 0;
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.size() == 1 && fx.modifyCalls[0].first == 0 &&
              fx.modifyCalls[0].second == stra::CommandType_CANCEL,
              "E7 撤单回调原样透传 id（int64 直传，不再有解析失败路径）");
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
        pi.currentAlgoOrderId = 555LL;
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
        pi.currentAlgoOrderId = 444LL;
        fx.ctx.RequestCancelAlgoOrder(pi);
        CHECK(fx.modifyCalls.size() == 1, "E12 撤单请求已发出");
        CHECK(pi.hasActiveAlgoOrder, "E12 撤单请求不释放对子（要等终态回传）");
        CHECK(pi.currentAlgoOrderId == 444LL, "E12 currentAlgoOrderId 保持不动");
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
    //
    // ⚠️ 阈值是 RiskManager 的**当前默认值 5**（RiskManager.h:48 `tinyCloseThresholdUsdt{5}`，
    //    etc/config.json 也是 "5"）。旧版本这个默认是 25，注释里那句"< 25 USDT"和原来用的
    //    -0.1（= 10 USDT）都是阈值还是 25 时留下的 —— 阈值改成 5 之后 10 USDT 已经不满足
    //    "碎单"了，用例没跟着改，所以一直在红。
    //    这里按当前阈值造场景：0.04 * 100 = 4 USDT < 5。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -0.04);
        pi.rtSpread.activePriceTema = 100.0;      // 市值 = 0.04 * 100 = 4 USDT < 5
        RiskCheckResult r = RiskManager::Instance().CheckRisk(pi, t0);
        CHECK(r.hasRisk && r.isTinyClose && r.needForceClose, "F5 持仓市值 < 5 USDT -> 碎单强平");
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
        pi.currentAlgoOrderId = 111LL;
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
        pi.currentAlgoOrderId = 333LL;
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
            pi.currentAlgoOrderId = 123456789LL;
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
            pi.currentAlgoOrderId = 123456789LL;
            PairInfoManager::Instance().SaveSnapshot(csv);
        }
        ResetWorld();
        Fx fx;
        fx.smc.Clear();
        fx.smc.Set(kActive, MakeInstrumentInfo());
        fx.smc.Set(kPassive, MakeInstrumentInfo());
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

    // G17 分位数快照：重启免预热
    //
    // 改这个功能要守的不变量：重启后**一个样本都不喂**，largeStats 就得是有效的，
    // 且 orderParams 得已经按它重算过 —— 只恢复 largeStats 不重算，阈值仍是默认值，
    // 等于没恢复（CanOpen 看的是 orderParams，不是 largeStats）。
    {
        const std::string stats = "/tmp/ptsuite/out/spread_stats.csv";
        std::remove(stats.c_str());

        double ttOLStart = 0.0, bidBidUQ = 0.0, askAskDQ = 0.0;
        int64_t calcTs = 0;
        int count = 0;

        // 第一轮：喂样本 -> 建统计 -> OnTimer 顺手落盘
        {
            Fx fx; Boot(fx, "", stats); GoTrading(fx);
            PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);

            // ts 必须用**真实墙钟**：calcTs 取自样本时间，而恢复侧的过期判据是
            // `nowUs - calcTs <= maxStaleSec`。用合成时间戳（如 1000*SEC）写出来的
            // 文件会被判成 1970 年的统计，永远恢复不了。
            const int64_t now = crypto::getCurrentTime();
            dbp::DbpTopic topic = MakeTopic(kPairKey);
            for (int i = 0; i < 5; ++i) {
                dbp::DbpData d = MakeData(0.001 * i, 0.002 * i, 0.003 * i, -0.001 * i,
                                          now - (4 - i) * SEC);
                fx.ctx.OnSpread(&topic, &d);
            }
            PumpSignal(fx.ctx, now + SEC);              // 显式驱动 T_signal 一轮（见 PumpSignal 注释）
            fx.ctx.OnTimer(now + SEC);

            CHECK(pi.largeStats.IsValid(), "G17 第一轮：样本足够 -> largeStats 建立");
            CHECK(std::ifstream(stats).good(), "G17 第一轮：T_signal 一轮顺手把统计落盘");

            ttOLStart = pi.orderParams.ttOLStartSpread;
            bidBidUQ  = pi.largeStats.bidBidUQ;
            askAskDQ  = pi.largeStats.askAskDQ;
            calcTs    = pi.largeStats.calcTs;
            count     = pi.largeStats.count;
        }

        // 第二轮：重启，一个样本都不喂
        {
            Fx fx; Boot(fx, "", stats);
            PairInfo& pi = *fx.pi();
            CHECK(pi.largeStats.IsValid(), "G17 重启后未喂任何样本 -> largeStats 已恢复（免预热）");
            CHECK(Near(pi.largeStats.bidBidUQ, bidBidUQ) && Near(pi.largeStats.askAskDQ, askAskDQ),
                  "G17 分位数往返无损");
            CHECK(pi.largeStats.calcTs == calcTs, "G17 calcTs（新鲜度基准）往返无损");
            CHECK(pi.largeStats.count == count, "G17 样本数往返无损");
            CHECK(Near(pi.orderParams.ttOLStartSpread, ttOLStart),
                  "G17 恢复后立刻重算 orderParams（只恢复统计不重算 = 阈值仍是默认值）");
        }
    }

    // G18 过期的统计不恢复 -> 回退到正常预热
    {
        const std::string stats = "/tmp/ptsuite/out/stale_stats.csv";
        std::remove(stats.c_str());
        {
            // 手写一份"两小时前算出来"的统计（默认 maxStaleSec = 3600）
            const int64_t old = crypto::getCurrentTime() - 2 * HOUR;
            std::ofstream ofs(stats);
            ofs << "pairInstrumentKey,bidAskUQ,bidAskDQ,bidBidUQ,bidBidDQ,"
                   "askBidUQ,askBidDQ,askAskUQ,askAskDQ,count,avgDepthVolume,calcTs\n";
            ofs << kPairKey << ",0.001,0.002,0.003,0.004,0.005,0.006,0.007,0.008,"
                << "100,10.5," << old << "\n";
        }
        Fx fx; Boot(fx, "", stats);
        PairInfo& pi = *fx.pi();
        CHECK(!pi.largeStats.IsValid(), "G18 过期统计（2h > maxStaleSec 1h）-> 不恢复，正常预热");
        CHECK(pi.largeStats.count == 0, "G18 不恢复时不留半截数据");
    }

    // G19 一个有效统计都没有时不覆盖磁盘上已有的那份
    {
        const std::string stats = "/tmp/ptsuite/out/keep_good.csv";
        std::remove(stats.c_str());
        std::string before;
        {
            const std::string good = "/tmp/ptsuite/out/keep_good_src.csv";
            std::remove(good.c_str());
            Fx fx; Boot(fx, "", good); GoTrading(fx);
            PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
            const int64_t now = crypto::getCurrentTime();
            dbp::DbpTopic topic = MakeTopic(kPairKey);
            for (int i = 0; i < 5; ++i) {
                dbp::DbpData d = MakeData(0.001 * i, 0.002 * i, 0.003 * i, -0.001 * i,
                                          now - (4 - i) * SEC);
                fx.ctx.OnSpread(&topic, &d);
            }
            PumpSignal(fx.ctx, now + SEC);              // 显式驱动 T_signal 一轮（见 PumpSignal 注释）
            fx.ctx.OnTimer(now + SEC);
            std::ifstream ifs(good);
            std::stringstream ss; ss << ifs.rdbuf();
            before = ss.str();
            CHECK(!before.empty(), "G19 前置：先拿到一份好文件");
            // 把它当成"磁盘上已有的那份"
            std::ofstream ofs(stats); ofs << before;
        }

        // 全新世界、统计快照关掉 -> largeStats 必然无效
        Fx fx; Boot(fx);
        CHECK(!fx.pi()->largeStats.IsValid(), "G19 前置：新世界没有有效统计");
        // 直接验落盘函数本身：空列表必须返回 false 且**一个字节都不动**。
        // （落盘已从 PairInfoManager 的方法变成自由函数 WriteSpreadStatsFile，
        //   它只认"有效统计的列表"，不再自己去 largeStats 里挑 —— 挑的活归 T_signal。）
        const std::vector<pt::SpreadStatsMsg> none;
        const bool wrote = pt::WriteSpreadStatsFile(stats, none);
        CHECK(!wrote, "G19 无有效统计 -> WriteSpreadStatsFile 返回 false");

        std::ifstream ifs(stats);
        std::stringstream ss; ss << ifs.rdbuf();
        CHECK(ss.str() == before, "G19 上一份好值被完整保留（预热期不会把好文件刷成空表头）");
    }

    // G20 恢复之后**真的能报单**，而不是"orderParams 里有几个值"
    //
    // 为什么非要有这一条：orderParams 有两个**互相独立**的输入 ——
    //   · StartSpread/EndSpread  <- largeStats（分位数）
    //   · *EndVolume/*StartVolume <- pi.maxVolume（由腿价算出）
    // G17 只断言了前者。只恢复分位数、不在 maxVolume 到位后重算，
    // *EndVolume 就还是 0 -> 开仓判据 `vol > ttOLEndVolume + 1e-9` 变成 `0 > 1e-9`
    // -> **开仓照样发不出去**，等于预热期一天没省。
    // 所以这里断言的是"单真的发出去了"（fx.N() > 0），不是"某个字段有值"。
    {
        const std::string stats = "/tmp/ptsuite/out/restore_trade.csv";
        std::remove(stats.c_str());

        // 样本：bidAsk/askAsk 走低、bidBid/askBid 走高 ——
        // 既要让 canOpenLong 成立（mtOLStartSpread < 0），
        // 又要让 spreadSpan = bidBidUQ - askAskDQ 够宽（否则 RecalcOrderParams 直接关掉全部开仓开关）。
        const double kLo = -0.02, kHi = 0.02;

        // 第一轮：喂样本建统计并落盘
        {
            Fx fx; Boot(fx, "", stats); GoTrading(fx);
            PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
            const int64_t now = crypto::getCurrentTime();
            dbp::DbpTopic topic = MakeTopic(kPairKey);
            for (int i = 0; i < 5; ++i) {
                dbp::DbpData d = MakeData(kLo, kHi, kHi, kLo, now - (4 - i) * SEC);
                fx.ctx.OnSpread(&topic, &d);
            }
            PumpSignal(fx.ctx, now + SEC);              // 显式驱动 T_signal 一轮（见 PumpSignal 注释）
            fx.ctx.OnTimer(now + SEC);
            CHECK(pi.largeStats.IsValid(), "G20 前置：第一轮建立统计");
        }

        // 第二轮：重启，只喂**一条**行情，就该能报单
        {
            Fx fx; Boot(fx, "", stats); GoTrading(fx);
            PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);

            const int64_t now = crypto::getCurrentTime();
            dbp::DbpTopic topic = MakeTopic(kPairKey);
            // 远低于下分位的价差 -> 深度足够触发 ttOL
            dbp::DbpData d = MakeData(-0.05, kHi, kHi, kLo, now);
            fx.ctx.OnSpread(&topic, &d);
            fx.ctx.OnTimer(now + SEC);

            CHECK(pi.maxVolume > 0, "G20 前置：maxVolume 已由腿价算出");
            CHECK(pi.orderParams.ttOLEndVolume != 0.0,
                  "G20 orderParams 的量字段也跟着 maxVolume 重算了（否则开仓判据恒不成立）");
            CHECK(SignalGenerator::Instance().CheckSignalForSatisfy(pi).ttOLSignal,
                  "G20 恢复后第一 tick 就有 ttOL 信号");

            fx.ctx.ProcessPairSignal(pi);
            CHECK(fx.N() > 0, "G20 恢复后第一 tick 就真的把单报出去了（无需重新预热）");
        }
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
// K. 运行时配置热更新（op.pairTrading.closeOnly / stopAllAlgo）
//
// 这条链路分三段，本组逐段验：
//   解析段     ReadRuntimeConfigFile：真 JSON 文件 -> RuntimeConfig（K0）
//   T_signal段 stat 配置文件 -> 指纹（inode+mtime+size）变了才读 -> 解析成功就 push 进 m_cfgQ
//   交易线程段 OnTimer 第 0.6 步抽干队列 -> 搬进 SignalGenerator / m_cfg -> 压 4 个开仓开关
//              + 让在跑的单本轮改参；stopAllAlgo 另有三处闸门 + 第 0.7 步每 tick 撤单
//
// 为什么用**真 JSON 文件**而不是注入一个假读取器：解析就在
// quant_library/basic/RuntimeConfig.cpp 里，本套件能直接编译它。于是"运维手写的配置
// 到底认不认得出来"也一并验了 —— 这段代码决定急停有没有生效，不能只靠肉眼审。
// ===========================================================================
static void WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream ofs(path, std::ios::trunc);
    ofs << text;
}

static void TestK_RuntimeConfig() {
    Section("K. 运行时配置热更新 (closeOnly / stopAllAlgo)");

    const std::string cfgPath = "/tmp/ptsuite/out/runtime_cfg.json";
    auto& sg = SignalGenerator::Instance();

    // K0 解析器本身
    //    ⚠️ 这里刻意**不写**带引号的布尔：`s["x"].GetBool()` 对字符串会 RAPIDJSON_ASSERT，
    //    本套件是 Debug 口径（assert 生效）-> 直接 abort。配置里必须写裸 true/false。
    {
        RuntimeConfig rc;

        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":true,"stopAllAlgo":true}}})");
        CHECK(ReadRuntimeConfigFile(cfgPath, rc) && rc.closeOnly && rc.stopAllAlgo,
              "K0 裸 true：两个字段都读出来");

        rc = RuntimeConfig{};
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":false,"stopAllAlgo":true}}})");
        CHECK(ReadRuntimeConfigFile(cfgPath, rc) && !rc.closeOnly && rc.stopAllAlgo,
              "K0 两个字段互不影响");

        rc = RuntimeConfig{};
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{}}})");
        CHECK(ReadRuntimeConfigFile(cfgPath, rc) && !rc.closeOnly && !rc.stopAllAlgo,
              "K0 段在、key 全缺 -> 解析成功，值取默认 false");

        rc = RuntimeConfig{};
        WriteTextFile(cfgPath, "{ this is not json");
        CHECK(!ReadRuntimeConfigFile(cfgPath, rc), "K0 语法错（半截文件）-> false，调用方重试");

        rc = RuntimeConfig{};
        WriteTextFile(cfgPath, R"({"op":{}})");
        CHECK(!ReadRuntimeConfigFile(cfgPath, rc), "K0 没有 pairTrading 段 -> false");

        rc = RuntimeConfig{};
        WriteTextFile(cfgPath, "");
        CHECK(!ReadRuntimeConfigFile(cfgPath, rc), "K0 空文件 -> false（编辑器 truncate 的瞬间）");
    }

    // K1~K5 共用一份夹具
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        MakeOpenFriendlyStats(pi);          // 统计有效 -> RecalcOrderParams 走完整路径

        fx.ctx.m_cfg.configPath = cfgPath;

        // T_signal 是异步线程，先停掉，之后每一步都是确定的
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        // 对照组：闸门关着时，同一份统计会让 4 个开仓开关都是开的。
        // 没有这一步，下面"被压掉"就证明不了是 closeOnly 干的。
        sg.SetCloseOnly(false);
        sg.RecalcOrderParams(pi);
        CHECK(pi.orderParams.ttOLSwitch && pi.orderParams.ttOSSwitch &&
              pi.orderParams.mtOLSwitch && pi.orderParams.mtOSSwitch,
              "K1 对照组：closeOnly=false -> 逻辑把 4 个开仓开关都打开");

        // K1 配置改成 true -> 投递到队列 -> OnTimer 生效
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":true}}})");
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        CHECK(fx.ctx.m_cfgStamp.valid, "K1 首次读到 -> 记下文件指纹");
        CHECK(fx.ctx.m_cfgQ.read_available() == 1, "K1 解析成功 -> push 了一条配置");

        fx.ctx.OnTimer(1000 * SEC);
        CHECK(fx.ctx.m_cfgQ.read_available() == 0, "K1 OnTimer 把队列抽干");
        CHECK(sg.GetCloseOnly(), "K1 OnTimer 把队列里的配置搬进 SignalGenerator");
        CHECK(!pi.orderParams.ttOLSwitch && !pi.orderParams.ttOSSwitch &&
              !pi.orderParams.mtOLSwitch && !pi.orderParams.mtOSSwitch,
              "K1 只平不开 -> 4 个开仓开关全压掉");
        CHECK(pi.orderParams.ttCLSwitch && pi.orderParams.ttCSSwitch &&
              pi.orderParams.mtCLSwitch && pi.orderParams.mtCSSwitch,
              "K1 平仓开关一个都没动");

        // K2 文件没变 -> 不重读。
        //    "少读一次"没法从外部直接观测（生产代码里没有读计数器，也不该为一个测试加）。
        //    这里钉住的是**指纹记得对不对**：记错的表现是"每次都重读"或"永远不重读"，
        //    前者会让 K4 的断言变红，后者会让 K3 变红。
        struct stat st{};
        CHECK(::stat(cfgPath.c_str(), &st) == 0 &&
              fx.ctx.m_cfgStamp.size == static_cast<int64_t>(st.st_size) &&
              fx.ctx.m_cfgStamp.ino == static_cast<uint64_t>(st.st_ino),
              "K2 指纹记的是文件当前状态（inode + size）");

        // K3 改回 false -> 恢复开仓
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":false}}})");
        fx.ctx.RunSignalOnce(1000 * SEC + 1 * SEC, lastStats);
        CHECK(fx.ctx.m_cfgQ.read_available() == 1, "K3 又 push 了一条");
        fx.ctx.OnTimer(1000 * SEC + 1 * SEC);
        CHECK(!sg.GetCloseOnly(), "K3 OnTimer 解除只平不开");
        CHECK(pi.orderParams.ttOLSwitch && pi.orderParams.mtOSSwitch,
              "K3 恢复后开仓开关重新打开");

        // K4 解析失败（编辑器写到一半 / 语法错）-> 保持原值，且**不更新指纹**（下一轮重试）
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":true}}})");
        fx.ctx.RunSignalOnce(1000 * SEC + 2 * SEC, lastStats);
        fx.ctx.OnTimer(1000 * SEC + 2 * SEC);
        CHECK(sg.GetCloseOnly(), "K4 前置：已进入只平不开");

        const auto stampBefore = fx.ctx.m_cfgStamp;
        WriteTextFile(cfgPath, "{ broken json !!!");
        fx.ctx.RunSignalOnce(1000 * SEC + 3 * SEC, lastStats);
        CHECK(sg.GetCloseOnly(), "K4 解析失败 -> 保持原值");
        CHECK(fx.ctx.m_cfgQ.read_available() == 0, "K4 解析失败 -> 不 push（直接跳过）");
        CHECK(fx.ctx.m_cfgStamp.size == stampBefore.size &&
              fx.ctx.m_cfgStamp.mtimeNs == stampBefore.mtimeNs,
              "K4 解析失败不更新指纹 -> 下一轮还会重试（不会卡死）");

        // K5 文件里没有这个 key -> 读成 false
        //    ⚠️ 这是当前语义：**删掉 key 等于放开开仓**。所以配置模板里必须一直保留它，
        //    只改值，不要整行删掉。
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"maxAmount":"1000"}}})");
        fx.ctx.RunSignalOnce(1000 * SEC + 4 * SEC, lastStats);
        CHECK(fx.ctx.m_cfgQ.read_available() == 1, "K5 解析成功 -> 照常 push");
        fx.ctx.OnTimer(1000 * SEC + 4 * SEC);
        CHECK(!sg.GetCloseOnly(),
              "K5 文件里没有 closeOnly -> 读成 false（删 key 等于放开开仓）");
    }

    // K6 configPath 为空 -> 完全不监视（没配路径就不该有任何文件操作）
    {
        Fx fx; Boot(fx);
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        CHECK(!fx.ctx.m_cfgStamp.valid, "K6 configPath 为空 -> 一次 stat 都不做");
    }

    // K7 CanOpen 的理由必须点明是配置关的（否则现场只看到"没信号"，查不出是人按的）
    {
        Fx fx; Boot(fx);
        PairInfo& pi = *fx.pi();
        std::string reason;
        sg.SetCloseOnly(true);
        CHECK(!sg.CanOpen(pi, reason), "K7 closeOnly=true -> CanOpen 返回 false");
        CHECK(reason.find("closeOnly") != std::string::npos, "K7 理由里带 closeOnly");
        sg.SetCloseOnly(false);
    }

    // K8 启动时配置里就是 true -> Init 那一刻就只平不开，不必等 T_signal 读到文件。
    //    这一小段窗口其实被启动闸门（Reconciling）挡着，但"配置里写了 true 却先放行两秒"
    //    是那种只有真出事才会被发现的问题，一行 SetCloseOnly 就能消掉。
    {
        ResetWorld();
        Fx fx;
        fx.smc.Clear();
        fx.smc.Set(kActive, MakeInstrumentInfo());
        fx.smc.Set(kPassive, MakeInstrumentInfo());

        PairTradingConfig cfg;
        cfg.pairKeys = {kPairKey};
        cfg.activeAccountId = 10000;
        cfg.passiveAccountId = 10001;
        cfg.closeOnly = true;
        cfg.csvStatePath = "";              // 不读快照，保持用例干净
        cfg.spreadStatsStatePath = "";      // 不读分位数快照（否则会被上一轮的落盘污染）
        fx.Wire();
        fx.ctx.Init(cfg, &fx.smc);

        CHECK(sg.GetCloseOnly(), "K8 Init 就把配置里的 closeOnly 装进 SignalGenerator");
        CHECK(fx.ctx.m_cfgQ.read_available() == 0,
              "K8 队列是空的（Init 直接落地，不该再投一条）");
    }

    // K9 一个 tick 里来了两条 -> 必须抽干，最后落的是后一条。
    //    "只 pop 一条"的症状很隐蔽：结果最终也对，只是要等下一轮 —— 若中途再没有 tick
    //    （比如刚好在退出前），那就永远差一条。所以钉住"队列必须空"。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        fx.ctx.m_cfg.configPath = cfgPath;
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":true}}})");
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":false}}})");
        fx.ctx.RunSignalOnce(1000 * SEC + 1 * SEC, lastStats);
        CHECK(fx.ctx.m_cfgQ.read_available() == 2, "K9 两次改动 -> 队列里两条");

        fx.ctx.OnTimer(1000 * SEC + 1 * SEC);
        CHECK(fx.ctx.m_cfgQ.read_available() == 0, "K9 OnTimer 一个 tick 把两条都吃掉");
        CHECK(!sg.GetCloseOnly(), "K9 后一条覆盖前一条（最终是 false）");
    }

    // K10 停掉所有算法单：在跑的算法单被撤
    //     satisfyTime / algoModifyTime 都摆到"刚好到期之前"，否则同一轮里
    //     CheckAlgoOrderTimeout 和 ProcessModify 也会各发一条，分不清是谁发的。
    //     ⚠️ 但这一轮配置**会变**：步骤 0.6 落地新配置时会把 algoModifyTime 归零
    //        （为了让 closeOnly 立刻作用到在跑的单）。所以这里同时验证了
    //        "stopAllAlgo 打开时 ProcessModify 直接跳过" —— 否则 modifyCalls 会是
    //        [CANCEL, MODIFY] 两条，size() == 1 这一条就会红。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        fx.ctx.m_cfg.configPath = cfgPath;
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        pi.hasActiveAlgoOrder = true;
        pi.currentAlgoOrderId = 555LL;
        pi.satisfyTime    = 1000 * SEC;
        pi.algoModifyTime = 1000 * SEC;

        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"stopAllAlgo":true}}})");
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        CHECK(fx.modifyCalls.empty(), "K10 T_signal 只投递、不在这条线程动单");

        fx.ctx.OnTimer(1000 * SEC);
        CHECK(fx.ctx.m_cfg.stopAllAlgo, "K10 OnTimer 把 stopAllAlgo 落地");
        CHECK(fx.modifyCalls.size() == 1 && fx.LastCmd() == stra::CommandType_CANCEL &&
              fx.modifyCalls[0].first == 555LL,
              "K10 在跑的算法单被撤（CANCEL + 按 id 指回）");

        // 撤单是异步两段式（CANCELLING 要等被动腿成交才终结），所以每 tick 都会再发一次。
        // 注意这里只断言"继续请求"，不断言"终结" —— 被动腿不成交时它会一直停在这儿。
        fx.modifyCalls.clear();
        pi.satisfyTime    = 1000 * SEC + 1 * SEC;
        pi.algoModifyTime = 1000 * SEC + 1 * SEC;
        fx.ctx.OnTimer(1000 * SEC + 1 * SEC);
        CHECK(fx.modifyCalls.size() == 1 && fx.LastCmd() == stra::CommandType_CANCEL,
              "K10 还没终结 -> 下一 tick 继续请求撤单");
    }

    // K11 stopAllAlgo 打开时不创建新算法单；关掉后恢复
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, "ttOL", 0.0);              // 与 B 组同一个"必定开仓"的场景

        fx.ctx.m_cfg.stopAllAlgo = true;
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "K11 stopAllAlgo=true -> 不创建新算法单");

        fx.ctx.m_cfg.stopAllAlgo = false;
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 1, "K11 关掉后同一个场景照常报单（证明上一条不是信号本身不成立）");
    }

    // K12 停掉所有算法单时连风控强平也不发 —— 强平单也是算法单，否则"不再创建新的"就是空话
    //     时间基准照抄 F 组：取一个远离 0 的固定值，避免和 CheckRisk 内部的计时起点（初始 0）混淆。
    const int64_t tRisk = 1000 * DAY;
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.rtSpread.passiveFundingRate = 0.4;   // 与 F6 同一个"资金费率异常 -> 强平"场景

        fx.ctx.m_cfg.stopAllAlgo = true;
        fx.ctx.ProcessRisk(pi, tRisk);
        CHECK(fx.N() == 0, "K12 stopAllAlgo=true -> 风控强平也不发单");
    }
    {
        // 对照组：同样的场景、开关关着 -> 会发
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        pi.rtSpread.passiveFundingRate = 0.4;

        fx.ctx.ProcessRisk(pi, tRisk);
        CHECK(fx.N() == 1, "K12 对照组：stopAllAlgo=false -> 强平照常发");
    }

    // K13 启动时配置里就写着 stopAllAlgo=true -> 从第一秒起就不发单
    //     这条钉的是 Init 那条路径：`m_cfg = cfg` 一旦被改成逐字段赋值而漏掉新字段，
    //     表现是"配置写了 true、重启后照样报单"，而运行期热改（K10/K11）完全测不出来。
    //     顺带钉住"初值不走队列" —— Init 直接落地，不该再投一条。
    {
        ResetWorld();
        Fx fx;
        fx.smc.Clear();
        fx.smc.Set(kActive, MakeInstrumentInfo());
        fx.smc.Set(kPassive, MakeInstrumentInfo());

        PairTradingConfig cfg;
        cfg.pairKeys = {kPairKey};
        cfg.activeAccountId = 10000;
        cfg.passiveAccountId = 10001;
        cfg.stopAllAlgo = true;
        cfg.csvStatePath = "";
        cfg.spreadStatsStatePath = "";
        fx.Wire();
        fx.ctx.Init(cfg, &fx.smc);

        CHECK(fx.ctx.m_cfg.stopAllAlgo, "K13 Init 把配置里的 stopAllAlgo 装进 m_cfg");
        CHECK(fx.ctx.m_cfgQ.read_available() == 0, "K13 初值不走队列");

        GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, 0.0);
        ArmOpen(pi, "ttOL", 0.0);          // 与 K11 同一个"必定开仓"的场景
        fx.ctx.ProcessPairSignal(pi);
        CHECK(fx.N() == 0, "K13 启动即 stopAllAlgo -> 同一个场景也不发单");
    }

    // K14 closeOnly **不撤**在跑的开仓单，只推一条 MODIFY 把 4 个开仓开关压掉
    //     这条钉的是"只平不开"的边界：它是关闸门，不是拉手刹。
    //     执行侧真正让这张单结束的是另一条路径（4 个开仓开关全 false + 无在途 pairOrder
    //     -> AlgoContext 自己把算法单终结成 ALGO_OS_FILLED），那条在 tests/algo_exec 里。
    //     这里只验到"我们这一层发出去的是什么"。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        MakeOpenFriendlyStats(pi);         // 与 K1 同一份夹具：4 个开仓开关本来是开的
        fx.ctx.m_cfg.configPath = cfgPath;
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        pi.hasActiveAlgoOrder = true;
        pi.currentAlgoOrderId = 777LL;
        pi.satisfyTime    = 1000 * SEC;    // 摆到"刚好到期之前"，别让超时撤单混进来
        pi.algoModifyTime = 1000 * SEC;

        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":true}}})");
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        fx.ctx.OnTimer(1000 * SEC);

        CHECK(fx.modifyCalls.size() == 1 && fx.LastCmd() == stra::CommandType_MODIFY,
              "K14 closeOnly -> 在跑的单只收到 MODIFY（不是 CANCEL）");
        CHECK(!fx.modifyPayloads.empty(), "K14 MODIFY 带参数体");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads.back();
            CHECK(!m.ttOLSwitch && !m.ttOSSwitch && !m.mtOLSwitch && !m.mtOSSwitch,
                  "K14 推过去的 4 个开仓开关全 false");
            CHECK(m.ttCLSwitch && m.ttCSSwitch && m.mtCLSwitch && m.mtCSSwitch,
                  "K14 平仓开关一个都没动（只平不开 = 平仓照常）");
        }
        CHECK(pi.hasActiveAlgoOrder, "K14 对子仍被这张单占着 —— closeOnly 不撤单");
    }

    // K15 改参时的开关压制：**只有风控强平**压开仓开关，普通平仓单照搬（可回补）
    //     2026-10-28 起"平仓单不许开仓"这条约束被 AlgoRole 取代：
    //       · 普通平仓单（CLOSE_SIGNAL）允许在开仓带回补 —— 压掉就砍掉了回补能力，
    //         而且第一次改参要等到 modifyTimespanSec(60s) 之后，那 60s 是纯机会损失；
    //       · 强平单（RISK_CLOSE）必须一直压着 —— 逆着强平方向加仓是最坏的失败模式。
    //     "平到 0 算不算结束"由算法单自己的 role 判（AlgoContext:1376），与这里无关。
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        MakeOpenFriendlyStats(pi);         // orderParams 的 4 个开仓开关是开的
        fx.ctx.m_cfg.configPath = cfgPath;
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        pi.hasActiveAlgoOrder = true;
        pi.currentAlgoOrderId = 780LL;
        pi.satisfyTime    = 1000 * SEC;
        pi.algoModifyTime = 1000 * SEC;

        // ⚠️ cfgPath 是**同一个文件**，K14 往里面写过 closeOnly:true。不覆盖的话
        //    RecalcOrderParams 末尾的 ApplyNoOpenGate 会把 4 个开仓开关压掉，
        //    于是下面"照搬 orderParams"验的就成了 closeOnly 的效果，不是角色的效果。
        WriteTextFile(cfgPath, R"({"op":{"pairTrading":{"closeOnly":false}}})");

        // ① 普通平仓单
        pi.activeAlgoIsRiskClose = false;
        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        fx.ctx.OnTimer(1000 * SEC);
        CHECK(!fx.modifyPayloads.empty(), "K15 普通平仓单 MODIFY 带参数体");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads.back();
            CHECK(m.mtOSSwitch && m.mtOLSwitch,
                  "K15 普通平仓单：开仓开关照搬 orderParams（可在开仓带回补）");
            CHECK(m.ttCLSwitch && m.ttCSSwitch && m.mtCLSwitch && m.mtCSSwitch,
                  "K15 普通平仓单：平仓开关照常开");
        }

        // ② 风控强平单
        //    第一次 MODIFY 是 RunSignalOnce 里"配置变了"把 algoModifyTime 归零触发的；
        //    这一轮没有新配置，所以要把时间推过 modifyTimespanSec 才会再推一次。
        fx.modifyCalls.clear();
        fx.modifyPayloads.clear();
        pi.activeAlgoIsRiskClose = true;
        pi.algoModifyTime = 1000 * SEC;
        fx.ctx.OnTimer(1000 * SEC + 61 * SEC);
        CHECK(!fx.modifyPayloads.empty(), "K15 强平单 MODIFY 带参数体");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads.back();
            CHECK(!m.ttOLSwitch && !m.ttOSSwitch && !m.mtOLSwitch && !m.mtOSSwitch,
                  "K15 强平单：4 个开仓开关全压掉（永不开仓）");
            CHECK(m.ttCLSwitch && m.ttCSSwitch && m.mtCLSwitch && m.mtCSSwitch,
                  "K15 强平单：平仓开关一个都没动");
        }
    }

    // K16 开仓单不受压制：同一份夹具、只把标记清掉，4 个开仓开关必须原样带过去
    {
        Fx fx; Boot(fx); GoTrading(fx);
        PairInfo& pi = *fx.pi(); MakeHoldable(pi, -1.0);
        MakeOpenFriendlyStats(pi);
        fx.ctx.m_cfg.configPath = cfgPath;
        fx.ctx.StopSignalThread();
        int64_t lastStats = 0;

        pi.hasActiveAlgoOrder = true;
        pi.currentAlgoOrderId = 781LL;
        pi.activeAlgoIsRiskClose = false;
        pi.satisfyTime    = 1000 * SEC;
        pi.algoModifyTime = 1000 * SEC;

        fx.ctx.RunSignalOnce(1000 * SEC, lastStats);
        fx.ctx.OnTimer(1000 * SEC);

        CHECK(!fx.modifyPayloads.empty(), "K16 MODIFY 带参数体");
        if (!fx.modifyPayloads.empty()) {
            const auto& m = fx.modifyPayloads.back();
            const auto& op = pi.orderParams;
            CHECK(m.ttOLSwitch == op.ttOLSwitch && m.ttOSSwitch == op.ttOSSwitch &&
                  m.mtOLSwitch == op.mtOLSwitch && m.mtOSSwitch == op.mtOSSwitch,
                  "K16 开仓单：4 个开仓开关原样照搬 orderParams（没被误压）");
        }
    }
}

// ===========================================================================
int main() {
    // 固定配置，让成本口径可预测。
    //
    // ⚠️ 不要图省事用 `FeeSlippageConfig{}` 的默认值：那些默认值现在是**生产兜底**，
    //    会随调参改动，测试就会跟着漂。2026-10-01 就踩过 —— 费率被置 0 之后
    //    A4 的「成本口径 = 0.0013 / 0.0009」直接失败。
    //    这里显式写死一套，A4 才有确定的期望值（= 上面 F_TT / F_MT 两个常量）。
    //    注意 CheckSignalForSatisfy 里那 10 行逐 tick 调试打印是**无条件**的
    //    （生产代码的既有形态），所以本套件的输出里会混着 ~230 行 `F_tt:` 噪音 ——
    //    那是被测代码打的，不是用例打的。
    {
        FeeSlippageConfig fs;
        fs.activeMakerFeeRate  = 0.0002;
        fs.activeTakerFeeRate  = 0.0006;
        fs.passiveMakerFeeRate = 0.0002;
        fs.passiveTakerFeeRate = 0.0006;
        fs.basicSlippage       = 0.0001;
        // 预期往返净利门槛：显式关掉，让 A~K 组的开仓用例只受阈值本身约束。
        // 它的行为由 D19 单独覆盖 —— 否则这个门槛会悄悄挡掉一批"只喂了开仓阈值"的老用例。
        fs.minProfitTarget     = 0.0;
        SignalGenerator::Instance().SetConfig(fs);
    }
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
    TestK_RuntimeConfig();

    std::printf("\n=================================================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
