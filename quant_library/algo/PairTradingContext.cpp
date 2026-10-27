/********
 * 核心流程：
 * OnSpread:
 * 1. 更新 pi.rtSpread
 * 2. AccumulateSpreadSample --》按 spreadSampleIntervalSec 降频后投给 T_signal
 *    （样本窗口与分位数计算都在 T_signal 上，本线程不碰）
 * 3. 机会条件快照 --》UpdateSatisfyTime（对每个对子都算，与有没有在跑的单无关）
 * 4. CheckSignal --》有信号 --》SubmitAlgoOrder
 * 
 * OnTimer:
 * 1. 周期性 RecalcVolumeParams
 * 2. 消费 T_signal 算好的价差统计 --》UpdateLargeStats --》立即 RecalcOrderParams
 * 3. 周期性 CheckRisk --> 有风险 --》SubmitAlgoOrder (强制平仓)
 *    强平单在途期间 ProcessRisk 不撤它（pi.riskCloseOrderInFlight），否则会被自己的撤单逻辑撤掉
 * 4. 周期性 撤单触发检查 --》CheckAlgoOrderTimeout / CheckExposureAbnormal --》RequestCancelAlgoOrder
 *    CheckAlgoOrderTimeout 对强平单同样生效：长时间不成交就撤掉它、下一轮按新档位重报。
 *    撤单 -> 终结 -> OnAlgoFinished -> tierNTimes++ 正是档位升级的唯一驱动
 * 5. 周期性 改参检查 --》ProcessModify --》SubmitAlgoOrder(CommandType_MODIFY)
 * 6. 周期性 SaveSnapshot（原子写，默认 10s）
 * 
 * 撤单是异步的两段式：RequestCancelAlgoOrder 只把算法单置成 ALGO_OS_CANCELLING，
 * 之后由执行端的 CancelOrderOnSpread 撤主动腿、等被动腿成交，子单清零后
 * OnAlgoOrderUpdate 收到终态才释放对子。调用方不能假设它同步生效。
 * 
 * OnAlgoOrderUpdate (算法单回传，执行端同线程直接调用，不走消息队列)
 * 1. 量/价直接覆盖 PairInfo (执行端是唯一数据源，策略侧不做加权)
 * 2. 场外->场内 的那一次事件记录开仓快照 (openSmallSpread*)
 * 3. 非终态到此为止；终态才: OnAlgoFinished -> 重算 orderParams -> 释放对子
 * 
 * ******/

#include "PairTradingContext.h"
#include "basic/DataStruct.h"
#include "basic/Utility.h"
#include "basic/AlgoPairOrder.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <sys/stat.h>
#include <thread>


namespace pt {

namespace {
int64_t FileMtimeNs(const struct stat& st) {
    // Linux 是 st_mtim，macOS 是 st_mtimespec（字段同名、结构体名不同）。
    // 目标环境是 Ubuntu，但本机测试套件也要能编，所以保留这个分支。
#if defined(__APPLE__)
    return static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL + static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
    return static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL + static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
}
} // namespace

PairTradingContext* PairTradingContext::s_instance = nullptr;

PairTradingContext::PairTradingContext() = default;

PairTradingContext::~PairTradingContext() {
    StopSignalThread();

    if (s_instance == this) {
        s_instance = nullptr;
    }
}

PairTradingContext* PairTradingContext::Instance() {
    return s_instance;
}

void PairTradingContext::NotifyAlgoOrderUpdate(BaseAlgoOrder* order) {
    if (s_instance == nullptr) {
        return;
    }

    s_instance->OnAlgoOrderUpdate(order);
}

void PairTradingContext::Init(const PairTradingConfig& cfg, sm::SecurityManager* s) {
    m_cfg = cfg;
    smc = s;
    s_instance = this;

    // 只平不开的初值：必须在下面那些 RecalcOrderParams 之前就位。
    SignalGenerator::Instance().SetCloseOnly(cfg.closeOnly);

    // 启动时就已经处于急停状态
    if (cfg.closeOnly || cfg.stopAllAlgo) {
        LOG_WARN("启动即处于急停状态: closeOnly={} stopAllAlgo={}（stopAllAlgo=true 时不创建任何算法单，需人工接管）", cfg.closeOnly, cfg.stopAllAlgo);
    }

    auto& pim = PairInfoManager::Instance();
    pim.Init(cfg.pairKeys, cfg.activeAccountId, cfg.passiveAccountId, smc);

    if (!cfg.csvStatePath.empty()) {
        const int restored = pim.LoadSnapshot(cfg.csvStatePath);
        if (restored < 0) {
            LOG_WARN("LoadSnapshot: {} 不可用（首次启动 / 格式不兼容）-> 按无持仓启动", cfg.csvStatePath);
        } else {
            LOG_INFO("LoadSnapshot: {} pair(s) restored from {}", restored, cfg.csvStatePath);
        }
    }

    // 恢复价差统计（**重启免预热**） 重算一次 orderParams
    if (!cfg.spreadStatsStatePath.empty() && cfg.spreadStatsMaxStaleSec > 0) {
        const int restoredStats = pim.LoadSpreadStats(cfg.spreadStatsStatePath, cfg.spreadStatsMaxStaleSec);
        if (restoredStats > 0) {
            auto& sg = SignalGenerator::Instance();
            for (PairInfo* pi : pim.GetAllPairInfos()) {
                if (!pi->largeStats.IsValid()) {
                    continue; // 这一对子没恢复成功 -> 让它走正常预热
                }
                sg.RecalcOrderParams(*pi);
                LOG_INFO("重启免预热: pairKey:{} 已恢复价差统计（样本数:{} 统计时刻:{}）-> 无需等待预热即可报单", pi->pairInstrumentKey, pi->largeStats.count, pi->largeStats.calcTs);
            }
        }
    }

    // ---- 启动闸门：先冻结，等对账 ----
    // 快照最多是 10s 前的，且 pre_stop 不撤单 —— 直接开跑等于拿一本可能过期的账去下单。
    // 价差推送通常早于持仓推送，所以这里必须挡住，等 OnPosition 把两腿填上再放行。
    m_phase = StartupPhase::Reconciling;
    m_startupTimeUs = crypto::getCurrentTime();
    m_lastReconcileWarnUs = m_startupTimeUs;

    // 上一轮有算法单未终结 -> 交易所侧可能仍有子单在成交，账本不可信（§5.4）
    HandleOrphanAlgoOrders();

    // 启动分位数计算线程（T_signal）
    StartSignalThread();

    LOG_WARN("启动闸门: Reconciling —— 交易冻结，等两腿持仓推送到位后对账（pairs:{}）", cfg.pairKeys.size());
}

// ---------------------------------------------------------------------------
// T_signal：分位数计算线程
// 它唯一的职责是把 SpreadStatsBuilder::Prune + Build（885 µs/对子）从行情线程搬走。
// 只吃 m_sampleQ 的样本、吐 m_statsQ 的统计。
void PairTradingContext::StartSignalThread() {
    if (m_signalRun.load(std::memory_order_relaxed)) {
        return;
    }

    m_signalRun.store(true, std::memory_order_relaxed);
    m_signalThread = std::thread(&PairTradingContext::SignalWork, this);
}

void PairTradingContext::StopSignalThread() {
    if (!m_signalRun.load(std::memory_order_relaxed)) {
        return;
    }

    m_signalRun.store(false, std::memory_order_relaxed);

    if (m_signalThread.joinable()) {
        m_signalThread.join();
    }
}

void PairTradingContext::SignalWork() {
    // 建统计的周期从本线程启动时刻起算。
    // ⚠️ 不能从 0 起算 —— 那样第一轮就会立刻 Build，而此时窗口里只有启动闸门期间的
    int64_t lastStatsUpdateUs = crypto::getCurrentTime();

    while (m_signalRun.load(std::memory_order_relaxed)) {
        RunSignalOnce(crypto::getCurrentTime(), lastStatsUpdateUs);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// T_signal 的一轮。生产由 SignalWork() 循环调；测试停掉线程后手工推（见头文件注释）。
void PairTradingContext::RunSignalOnce(int64_t nowUs, int64_t& lastStatsUpdateUs) {
    // 0. 运行时配置
    struct stat st{};
    if (::stat(m_cfg.configPath.c_str(), &st) == 0) {
        const int64_t mtimeNs = FileMtimeNs(st);
        const int64_t size = static_cast<int64_t>(st.st_size);
        const uint64_t ino = static_cast<uint64_t>(st.st_ino);

        bool fileSame = m_cfgStamp.valid && m_cfgStamp.ino == ino && m_cfgStamp.mtimeNs == mtimeNs && m_cfgStamp.size == size;
        if (!fileSame) {
            RuntimeConfig rc;
            if (ReadRuntimeConfigFile(m_cfg.configPath, rc)) {
                m_cfgStamp = CfgStamp{ino, mtimeNs, size, true};
                m_cfgQ.push(rc);
            }
            else {
                LOG_WARN("运行时配置: {} 读取/解析失败 -> 本次忽略，下一轮重试（指纹不更新）", m_cfg.configPath);
            }
        }
    }

    // 1. 吃掉所有新样本
    SpreadSampleMsg sm;
    while (m_sampleQ.pop(sm)) {
        auto it = m_spreadWindows.find(sm.pairKey);
        if (it == m_spreadWindows.end()) {
            SpreadStatsBuilder builder;
            builder.SetWindowUs(static_cast<int64_t>(m_cfg.spreadStatsWindowSec) * 1000000LL);
            it = m_spreadWindows.emplace(sm.pairKey, std::move(builder)).first;
        }
        it->second.Add(sm.s);
    }

    // 2. 到点就重算分位数，把结果发布回行情线程；**顺手把这一轮的结果落盘**。
    if (nowUs - lastStatsUpdateUs > static_cast<int64_t>(m_cfg.spreadStatsUpdateIntervalSec) * 1000000LL) {
        std::vector<SpreadStatsMsg> rows;

        for (auto& kv : m_spreadWindows) {
            SpreadStatsBuilder& builder = kv.second;
            builder.Prune(nowUs); // 先按 24h 窗口淘汰过期样本

            const SpreadStats stats = builder.Build(m_cfg.quantileUp, m_cfg.quantileDn, static_cast<size_t>(m_cfg.spreadStatsMinSamples));

            SpreadStatsMsg out{};
            std::strncpy(out.pairKey, kv.first.c_str(), stra::INST_KEY_LEN);
            out.st = stats;
            m_statsQ.push(out);

            // 与 SaveSpreadStats 同一条规则：无效统计不写。
            // 否则预热期每轮都会把磁盘上的 last-known-good 刷成空表头。
            if (stats.IsValid()) {
                rows.push_back(out);
            }
        }

        if (!rows.empty()) {
            try {
                WriteSpreadStatsFile(m_cfg.spreadStatsStatePath, rows);
            } 
            catch (const std::exception& e) {
                LOG_ERROR("写价差统计失败 path:{} rows:{} err:{}", m_cfg.spreadStatsStatePath, rows.size(), e.what());
            }
        }

        lastStatsUpdateUs = nowUs;
    }

    // 3. 顺带把快照队列里"攒够的一批"落盘。一次取到队列空为止；没遇到哨兵说明这批还没攒齐，
    //    留在 m_snapWrite 里下一轮接着攒（跨轮攒批是这条路径唯一的"状态"）。
    PairInfoRecord r;
    while (m_snapQ.pop(r)) {
        // 哨兵 = 一批结束：把攒到的行整个文件重写一次（原子替换，不是追加），然后清空接着攒。
        // ⚠️ 哨兵**本身不是数据行**，必须 continue 掉，绝不能落到下面那个 push_back：
        if (r.pairKey[0] == '\0') {
            if (!m_snapWrite.empty()) {
                try {
                    WritePairInfoRecordFile(m_cfg.csvStatePath, m_snapWrite);
                } 
                catch (const std::exception& e) {
                    LOG_ERROR("写快照失败 path:{} rows:{} err:{}", m_cfg.csvStatePath, m_snapWrite.size(), e.what());
                }
            }
            m_snapWrite.clear(); // 只清元素，capacity 留着 —— 下一批零分配
            continue;
        }
        m_snapWrite.push_back(r);
    }
}

// 快照里 hasActiveAlgoOrder == true 的启动处理。
// ⚠️ 必须在这里（Init）做：PairTradingStrategy::ScanFinishedAlgoOrders 第一次跑就会
//    因为"算法单在 AlgoContext 里找不到"而把 hasActiveAlgoOrder 清掉，之后再也看不出痕迹。
void PairTradingContext::HandleOrphanAlgoOrders() {
    auto& pim = PairInfoManager::Instance();

    for (PairInfo* pi : pim.GetAllPairInfos()) {
        if (!pi->hasActiveAlgoOrder) {
            continue;
        }

        // 交易所侧可能残留上一轮的子单，而我们没有"列在途单/全撤"的能力（TradeClient 只有
        // query_account / add_new_order / cancel_order / query_order）-> 只能告警 + 人工确认。
        std::string msg = fmt::format("孤儿单告警: pairKey:{} 上一轮算法单未终结 algoOrderId:{} —— 交易所侧可能残留子单，账本(pairTotalVolume:{})不可信，请人工确认", pi->pairInstrumentKey, pi->currentAlgoOrderId, pi->pairTotalVolume);
        LOG_ERROR(msg);
        LarkRebot::GetInstance().SendMsg(msg);

        if (m_cfg.freezeOnOrphanAlgoOrder) {
            // 复用 errorFlag 的"判死、停自动、留人工处理"语义（它现在有清除路径：PairCmd_RESUME）
            pi->errorFlag = true;
            LOG_ERROR("孤儿单冻结: pairKey:{} errorFlag = true（运维确认交易所已清干净后发 PairCmd_RESUME 复活）", pi->pairInstrumentKey);
        }
    }
}

void PairTradingContext::OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    std::string pairKey(topic->__name);
    auto& pim = PairInfoManager::Instance();
    pim.UpdateRtSpread(pairKey, pdata);

    // 喂一条价差样本进 24h 滚动窗口（内部按 spreadSampleIntervalSec 降频）
    AccumulateSpreadSample(pairKey, pdata);

    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    ProcessPairSignal(*pi);
}

void PairTradingContext::AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata) {
    const int64_t ts = pdata->generateTs;
    int64_t& lastTs = m_lastSampleTs[pairKey];
    if (m_cfg.spreadSampleIntervalSec > 0 && lastTs > 0) {
        const int64_t minGapUs = static_cast<int64_t>(m_cfg.spreadSampleIntervalSec) * 1000000LL;
        if (ts - lastTs < minGapUs) {
            return;
        }
    }
    lastTs = ts;

    SpreadSampleMsg msg{};
    std::strncpy(msg.pairKey, pairKey.c_str(), stra::INST_KEY_LEN - 1);
    msg.s.ts  = ts;
    msg.s.sba = pdata->spreadBidAsk;
    msg.s.sbb = pdata->spreadBidBid;
    msg.s.sab = pdata->spreadAskBid;
    msg.s.saa = pdata->spreadAskAsk;
    msg.s.abv = pdata->activeBidVolume[0];
    msg.s.aav = pdata->activeAskVolume[0];

    m_sampleQ.push(msg);
}

void PairTradingContext::ProcessPairSignal(PairInfo& pi) {
    // 启动闸门：对账完成前不下任何单。
    // 价差推送通常早于持仓推送，此时 pairTotalVolume 还是快照值（或 0），照常跑信号会：
    //   账本 = 0 而交易所有仓  -> 在孤儿仓上再开一笔（CanOpen 没有持仓门槛，CanClose 又平不掉）
    //   账本 ≠ 0 而交易所为空  -> 平仓单实际是反向开仓
    if (!IsTradingReady()) {
        return;
    }

    // 停掉所有算法单：不创建新单。
    // 放在信号计算之前 —— 与上面的启动闸门同一个位置，语义都是"这个对子本轮什么都不做"。
    // 代价：satisfyTime 不再刷新，CheckAlgoOrderTimeout 会把在跑的单按超时撤掉；
    // 而"停掉所有算法单"本来就要撤，方向一致，不用额外处理。
    if (m_cfg.stopAllAlgo) {
        return;
    }

    auto& sg = SignalGenerator::Instance();

    std::string reason = "";
    const bool canOpen = sg.CanOpen(pi, reason);
    const bool canClose = sg.CanClose(pi, reason);

    // ⚠️ 这里必须用 CheckSignalForSatisfy（不看 hasActiveAlgoOrder 的那版），
    // 不能用 CheckSignal：后者对"已有算法单在跑"的对子直接返回空信号，于是
    // UpdateSatisfyTime 永远刷不到 satisfyTime，CheckAlgoOrderTimeout 会把每一个
    // 算法单都在 150s 后撤掉（包括刚发出去的风控强平单）。
    // 祖先是在整个 pair_info 上整列算 open_satisfy_index / close_satisfy_index
    // （:900-919/:922/:930/:936），与有没有在跑的单无关，所以这里也不能早退。
    // 派发侧不受影响：下面 hasActiveAlgoOrder 的早退保证走到派发时它必然为 false，
    // 此时 CheckSignalForSatisfy 与 CheckSignal 完全等价。
    const SignalResult sig = sg.CheckSignalForSatisfy(pi);

    UpdateSatisfyTime(pi, sig, canOpen, canClose);

    if (pi.hasActiveAlgoOrder) {
        return;
    }

    // 敞口异常（CheckExposureAbnormal 打的 errorFlag）：祖先把该对子 status 打成 ERROR 之后
    // 既不开也不平，留人工处理。CanOpen 内部已经挡了 errorFlag，这里补上平仓侧。
    if (pi.errorFlag) {
        return;
    }

    if (!sig.hasSignal) {
        return;
    }

    LOG_INFO("信号触发，准备派发算法单: pairInstrumentKey:{} canOpen:{} canClose:{} reason:{} ttCL:{} ttCS:{} ttOL:{} ttOS:{} mtCL:{} mtCS:{} mtOL:{} mtOS:{}",
             pi.pairInstrumentKey, canOpen, canClose, reason,
             sig.ttCLSignal, sig.ttCSSignal, sig.ttOLSignal, sig.ttOSSignal,
             sig.mtCLSignal, sig.mtCSSignal, sig.mtOLSignal, sig.mtOSSignal);

    // 优先级 平仓 > 开仓
    if (sig.ttCLSignal && canClose) {
        SubmitAlgoOrder(pi, "TT", "CL");
        return;
    }

    if (sig.ttCSSignal && canClose) {
        SubmitAlgoOrder(pi, "TT", "CS");
        return;
    }

    if (sig.mtCLSignal && canClose) {
        SubmitAlgoOrder(pi, "MT", "CL");
        return;
    }

    if (sig.mtCSSignal && canClose) {
        SubmitAlgoOrder(pi, "MT", "CS");
        return;
    }


    if (sig.ttOLSignal && canOpen) {
        SubmitAlgoOrder(pi, "TT", "OL");
        return;
    }

    if (sig.ttOSSignal && canOpen) {
        SubmitAlgoOrder(pi, "TT", "OS");
        return;
    }

    if (sig.mtOLSignal && canOpen) {
        SubmitAlgoOrder(pi, "MT", "OL");
        return;
    }

    if (sig.mtOSSignal && canOpen) {
        SubmitAlgoOrder(pi, "MT", "OS");
        return;
    }
}


void PairTradingContext::ProcessRisk(PairInfo& pi, int64_t nowUs) {
    // 这里刻意不判 HasPosition：风控要对每个对子每 tick 都跑一遍，因为
    // CheckADLRisk / CheckSpreadNoRegression 内部依赖这个时机清理自己的计时起点
    // （无持仓时把 positionExceedThresholdStartTime / spreadNoRegressionStartTime 归零）。
    // 一旦加了早退，这两个归零就永远跑不到，下次开仓会带着上一轮的陈旧时间戳。
    auto& rm = RiskManager::Instance();
    RiskCheckResult risk = rm.CheckRisk(pi, nowUs);

    if (!risk.needForceClose) {
        return;
    }

    // 停掉所有算法单：连风控强平也不发 —— 强平单也是算法单，与"不再创建新的"冲突。
    // 注意上面那行 CheckRisk 必须照常调：它内部在"无持仓"时要把几个计时起点归零，
    // 跳过它等于把陈旧时间戳带进恢复之后（下一轮恢复就会立刻按旧档位强平）。
    if (m_cfg.stopAllAlgo) {
        return;
    }

    // ⚠️ 占着对子的就是上一轮自己发出去的风控强平单：这里不撤，让它跑完。
    // 强平期间 needForceClose 一直为 true（档位要靠算法单终结后的 OnAlgoFinished 才推进），
    // 所以下面那个 RequestCancelAlgoOrder 每一轮都会命中，把刚报出去的强平单撤掉 ——
    // 表现为"报单 -> 下个 tick 撤单 -> 再报 -> 再撤"，强平永远发不出去。
    // 注意这里只是**不提前撤**，不是给强平单豁免：它跑满 algoOrderTimeoutMs 之后
    // 照样由 CheckAlgoOrderTimeout 撤掉，再按新档位重报（见该函数注释）。
    if (pi.hasActiveAlgoOrder && pi.riskCloseOrderInFlight) {
        return;
    }

    // 对子被（非强平的）算法单占着时，先请求撤掉它。撤单是异步的：要等它终结、回传、释放对子之后，
    // 下一轮才会走到下面的 SubmitAlgoOrder。这里必须 return，不能继续报单 ——
    // 否则 SetActiveAlgoOrder 会把 currentAlgoOrderId 换成新单，旧单的后续回传会因为
    // id 对不上被 OnAlgoOrderUpdate 丢掉，两个算法单的状态彻底错位。
    //
    // 注意 CANCELLING 的语义是"撤掉主动腿的报价、等被动腿成交把敞口平掉"，
    // 不是"立刻中止、宁可留敞口"（2026-09-23 确认过的刻意设计）：
    //   - OnSpread 里"只有 NEW/PARTFILLED 才继续报单"那道判断拦住报新的 pairOrder（不再开新仓）；
    //   - CancelOrderOnSpread 的 CANCELLING 分支只撤主动腿，门槛 1000*10 微秒（=10ms，
    //     crypto::getCurrentTime 是微秒，所以基本是下一个 tick 就撤）；
    //     被动腿留着等成交 —— 被动腿挂着就说明主动腿已成交、场上存在单边敞口；
    //   - OnTimer 仍会每 tick 调 PairOrderTrade 给"尚未对冲完"的 pairOrder 续报被动腿；
    //   - pairOrder 全部删掉后 allPairOrders.size() 归零 -> ALGO_OS_CANCELED -> 回传释放对子。
    // 已知残余风险（未兜底）：被动腿长期不成交时算法单会一直停在 CANCELLING，
    // 见 memory 2026-09-23「唯一残余风险：这个"等待"没有上界」。
    if (pi.hasActiveAlgoOrder) {
        LOG_INFO("ProcessRisk RequestCancelAlgoOrder pairKey:{} algoOrderId:{}", pi.pairInstrumentKey, pi.currentAlgoOrderId);

        RequestCancelAlgoOrder(pi);
        return;
    }

    // satisfyTime / algoModifyTime 由 SubmitAlgoOrder 在报单成功时一起归零，
    // 所以这里不需要再显式刷新（风控强平没有信号路径，靠 SubmitAlgoOrder 兜住）。
    if (pi.IsLong()) {
        std::string mode = pi.autoFlag ? "TT" : "MT";
        SubmitAlgoOrder(pi, mode, "CL", risk.forgoProfit);
    }
    else if (pi.IsShort()) {
        std::string mode = pi.autoFlag ? "TT" : "MT";
        SubmitAlgoOrder(pi, mode, "CS", risk.forgoProfit);    
    }

    // SubmitAlgoOrder 只在真正报出单时才把 hasActiveAlgoOrder 置 true
    // （BuildAlgoOrderJson 返回 nullptr 时不会置），所以报完再判一次即可。
    if (pi.hasActiveAlgoOrder) {
        pi.riskCloseOrderInFlight = true;
    }
}

void PairTradingContext::RequestCancelAlgoOrder(const PairInfo& pi) const {
    if (!m_algoOrderModifyCb) {
        return;
    }

    if (!pi.hasActiveAlgoOrder) {
        return;
    }

    m_algoOrderModifyCb(pi.currentAlgoOrderId, stra::CommandType_CANCEL, nullptr);
}

// ---- 撤单触发条件（对齐祖先 cc_pricespread_gb_ltp.py:1015-1092）----

namespace {
    bool HasOpenSignal(const SignalResult& sig) {
        return sig.ttOLSignal || sig.ttOSSignal || sig.mtOLSignal || sig.mtOSSignal;
    }

    bool HasCloseSignal(const SignalResult& sig) {
        return sig.ttCLSignal || sig.ttCSSignal || sig.mtCLSignal || sig.mtCSSignal;
    }
}

void PairTradingContext::UpdateSatisfyTime(PairInfo& pi, const SignalResult& sig, bool canOpen, bool canClose) const {
    const bool openSatisfied = canOpen && HasOpenSignal(sig);
    const bool closeSatisfied = canClose && HasCloseSignal(sig);

    // 手动对子（autoFlag == false）没有信号也要续命，否则 CheckAlgoOrderTimeout 会撤它的单。
    const bool manualReady = !pi.autoFlag && !pi.stopFlag;

    if (openSatisfied || closeSatisfied || manualReady) {
        pi.satisfyTime = crypto::getCurrentTime();
    }
}

void PairTradingContext::CheckAlgoOrderTimeout(const PairInfo& pi, int64_t nowUs) const {
    if (!pi.hasActiveAlgoOrder) {
        return;
    }

    // 风控强平单**不豁免**（2026-09-27 确认）。
    // 曾经在这里加过 `if (pi.riskCloseOrderInFlight) return;`，理由是"强平是在平仓条件
    // 不成立的时候才发的，satisfyTime 天然刷不动，不豁免的话每次只有 150s 窗口"。这个理由
    // 两头都站不住：
    //   ① 算法单自身的 active*CancelOrderTime = 5s 只撤**子单**（BaseAlgoOrder::
    //      CancelOrderOnSpread 的 else 分支），算法单会换个价继续追，不会被终结 ——
    //      所以既不存在"不豁免就会被撤掉"，也不存在"豁免了就会自然结束"；
    //   ② 撤单恰恰是**档位升级的唯一驱动**：撤单 -> CANCELED -> OnAlgoFinished ->
    //      tierNTimes++ -> GetCurrentTier 才可能升到下一档。豁免之后强平单永不终结，
    //      tier1Times 恒为 0，档位永远停在 tier1 用最温和的让利追价，升不上去。
    // 所以强平单和普通单走同一套规则：长时间不成交就撤掉、下一轮按新档位重报。
    // 注意 ProcessRisk 里仍保留 riskCloseOrderInFlight 的早退 —— 那是为了不让 ProcessRisk
    // 在报单后的下一个 tick 就把强平单撤掉（needForceClose 全程为 true），
    // 好让它完整跑完这里的 150s 窗口。

    // 手动单不撤（祖先 :1018-1020：auto_flag == False 直接 continue）
    if (!pi.autoFlag) {
        return;
    }

    // satisfyTime 默认 0，语义与祖先把 satisfy_time 初始化成 1 天前等价（都是"很久以前"）。
    // 正常路径下它每次机会成立都会被刷新，而算法单只可能在机会成立之后才报出，
    // 所以这里不会误撤；真走到 0 说明 satisfyTime 没被写过，撤单是安全侧的选择。
    const int64_t idleUs = nowUs - pi.satisfyTime;
    if (idleUs <= m_cfg.algoOrderTimeoutMs * 1000LL) {
        return;
    }

    LOG_INFO("CheckAlgoOrderTimeout: pairKey:{} algoOrderId:{} idleSec:{} > {}s -> cancel", pi.pairInstrumentKey, pi.currentAlgoOrderId, static_cast<double>(idleUs) / 1000000.0, static_cast<double>(m_cfg.algoOrderTimeoutMs) / 1000.0);

    RequestCancelAlgoOrder(pi);
}

void PairTradingContext::CheckExposureAbnormal(PairInfo& pi) const {
    // 祖先 :1081 的 status != ERROR 门槛：已经打过标记的对子不再重复处理
    if (pi.errorFlag) {
        return;
    }

    // 两条腿都必须有实盘持仓，且都有实盘均价 —— 只有单边有仓属于正常的开仓中间态
    if (std::abs(pi.activeRealPosition) <= 1e-9 || std::abs(pi.passiveRealPosition) <= 1e-9) {
        return;
    }
    if (pi.activeAvgPrice <= 0 || pi.passiveAvgPrice <= 0) {
        return;
    }

    const double activeMultiple = pi.activeParam.multiple;
    if (activeMultiple <= 0) {
        return;
    }
    // 阈值本身要有效，否则 exposureCancelTimes * 0 = 0 会让任何一点敞口都判成异常
    if (std::isnan(pi.ttTargetVolume) || pi.ttTargetVolume <= 0) {
        return;
    }

    // 祖先 :1081：(activeRealPosition*active_multiple + passiveRealPosition*passive_multiple).abs()
    //              / active_multiple > 4 * ttTargetVolume
    const double netExposure = std::abs(pi.activeRealPosition * activeMultiple + pi.passiveRealPosition * pi.passiveParam.multiple) / activeMultiple;
    const double threshold = m_cfg.exposureCancelTimes * pi.ttTargetVolume;
    if (netExposure <= threshold) {
        return;
    }

    // 敞口失控：置 errorFlag（= 祖先的 status = ERROR），该对子此后既不开也不平，留人工处理。
    // 祖先 :1083 只打标记、:1090 才撤单；这里先停单再撤，避免中间再报出新单。
    pi.errorFlag = true;
    LOG_ERROR("CheckExposureAbnormal: pairKey:{} netExposure:{} > {} (ttTargetVolume:{} activePos:{} passivePos:{}) -> errorFlag + cancel",
              pi.pairInstrumentKey, netExposure, threshold, pi.ttTargetVolume,
              pi.activeRealPosition, pi.passiveRealPosition);

    if (pi.hasActiveAlgoOrder) {
        RequestCancelAlgoOrder(pi);
    }
}

// ---- 改参（祖先 cc_pricespread_gb_ltp.py:1059-1078）----

stra::AlgoOrderModify PairTradingContext::BuildNormalModify(const PairInfo& pi) {
    const auto& op = pi.orderParams;
    stra::AlgoOrderModify m;

    m.profitSwitch = pi.profitSwitch;
    m.profitPct = pi.profitPct;

    // 8 个开关必须全部带上。祖先的 create_modify_dict 只带 6 个（不带 ttCL/ttCS/mtCL/mtCS），
    // 因为 AEC 侧是"缺字段保持原值"；C++ 是整份覆盖，漏掉就会把平仓开关清零。见头文件注释。
    m.ttOLSwitch = op.ttOLSwitch;
    m.ttOSSwitch = op.ttOSSwitch;
    m.ttCLSwitch = op.ttCLSwitch;
    m.ttCSSwitch = op.ttCSSwitch;
    m.mtOLSwitch = op.mtOLSwitch;
    m.mtOSSwitch = op.mtOSSwitch;
    m.mtCLSwitch = op.mtCLSwitch;
    m.mtCSSwitch = op.mtCSSwitch;

    // 32 个起止价差 / 起止量：整份照搬 pi.orderParams
    m.ttOLStartSpread = op.ttOLStartSpread;
    m.ttOLEndSpread = op.ttOLEndSpread;
    m.ttOLStartVolume = op.ttOLStartVolume;
    m.ttOLEndVolume = op.ttOLEndVolume;
    m.ttCLStartSpread = op.ttCLStartSpread;
    m.ttCLEndSpread = op.ttCLEndSpread;
    m.ttCLStartVolume = op.ttCLStartVolume;
    m.ttCLEndVolume = op.ttCLEndVolume;
    m.ttOSStartSpread = op.ttOSStartSpread;
    m.ttOSEndSpread = op.ttOSEndSpread;
    m.ttOSStartVolume = op.ttOSStartVolume;
    m.ttOSEndVolume = op.ttOSEndVolume;
    m.ttCSStartSpread = op.ttCSStartSpread;
    m.ttCSEndSpread = op.ttCSEndSpread;
    m.ttCSStartVolume = op.ttCSStartVolume;
    m.ttCSEndVolume = op.ttCSEndVolume;
    m.mtOLStartSpread = op.mtOLStartSpread;
    m.mtOLEndSpread = op.mtOLEndSpread;
    m.mtOLStartVolume = op.mtOLStartVolume;
    m.mtOLEndVolume = op.mtOLEndVolume;
    m.mtCLStartSpread = op.mtCLStartSpread;
    m.mtCLEndSpread = op.mtCLEndSpread;
    m.mtCLStartVolume = op.mtCLStartVolume;
    m.mtCLEndVolume = op.mtCLEndVolume;
    m.mtOSStartSpread = op.mtOSStartSpread;
    m.mtOSEndSpread = op.mtOSEndSpread;
    m.mtOSStartVolume = op.mtOSStartVolume;
    m.mtOSEndVolume = op.mtOSEndVolume;
    m.mtCSStartSpread = op.mtCSStartSpread;
    m.mtCSEndSpread = op.mtCSEndSpread;
    m.mtCSStartVolume = op.mtCSStartVolume;
    m.mtCSEndVolume = op.mtCSEndVolume;

    return m;
}

stra::AlgoOrderModify PairTradingContext::BuildCloseModify(const PairInfo& pi, double shiftPct) {
    // 先取整份快照，再按"更激进的平仓"覆盖少数几个字段
    stra::AlgoOrderModify m = BuildNormalModify(pi);

    m.profitSwitch = false;
    m.profitPct = 0.0;

    // 开仓四个开关全关
    m.ttOLSwitch = false;
    m.ttOSSwitch = false;
    m.mtOLSwitch = false;
    m.mtOSSwitch = false;

    // 祖先把 tt 的平仓开关也关掉，只留 mt 的平仓开关（:260-265）。
    // 注意它仍然对 ttCL/ttCS 的价差做了让利（:270-271 / :278-279），这里照做 ——
    // 将来若把 tt 平仓开关打开，行为才与祖先一致。
    m.ttCLSwitch = false;
    m.ttCSSwitch = false;
    m.mtCLSwitch = true;
    m.mtCSSwitch = true;

    // 平仓让利：CL 的起止价差各减 shiftPct，CS 的各加 shiftPct，让平仓更容易成交。
    // 注意这里的基准是 pi.orderParams（不是刚才那份快照），与祖先读 pair_info 一致。
    m.ttCLStartSpread = pi.orderParams.ttCLStartSpread - shiftPct;
    m.ttCLEndSpread = pi.orderParams.ttCLEndSpread - shiftPct;
    m.ttCSStartSpread = pi.orderParams.ttCSStartSpread + shiftPct;
    m.ttCSEndSpread = pi.orderParams.ttCSEndSpread + shiftPct;
    m.mtCLStartSpread = pi.orderParams.mtCLStartSpread - shiftPct;
    m.mtCLEndSpread = pi.orderParams.mtCLEndSpread - shiftPct;
    m.mtCSStartSpread = pi.orderParams.mtCSStartSpread + shiftPct;
    m.mtCSEndSpread = pi.orderParams.mtCSEndSpread + shiftPct;

    return m;
}

void PairTradingContext::ProcessModify(PairInfo& pi, int64_t nowUs) const {
    if (!m_algoOrderModifyCb) {
        return;
    }

    // 祖先 :1059 只遍历在跑的算法单
    if (!pi.hasActiveAlgoOrder) {
        return;
    }

    // 停掉所有算法单时不给在跑的单推参数：单子正在被撤（OnTimer 步骤 0.7 每 tick 撤一次），
    // 推过去要么被执行端跳过（算法单已在撤单流程里），要么在撤单延迟期间把刚压掉的
    // 开仓开关又推回去 —— 两个结果都不是"停掉所有算法单"想要的。
    // 注意放在 algoModifyTime 判断**之前**：这一步不能动 algoModifyTime，
    // 否则恢复时会把 60s 的计时起点挪走（这里根本没发消息，不该记账）。
    if (m_cfg.stopAllAlgo) {
        return;
    }

    // 祖先 :1060：按 modify_time 计时。
    // 用 algoModifyTime 而不是 PairInfo::modifyTime —— 后者被 PairInfoManager 在
    // UpdateRtSpread 等七处刷新，等于每个价差 tick 刷一次，用它这个判断永远不成立。
    if (nowUs - pi.algoModifyTime < m_cfg.modifyTimespanSec * 1000000LL) {
        return;
    }

    // 祖先 :1063-1068：流动性危险时改用更激进的平仓参数，否则按最新行情重算
    const bool aggressiveClose = (pi.activeLiquidStatus == 2 || pi.passiveLiquidStatus == 2);
    stra::AlgoOrderModify mod = aggressiveClose ? BuildCloseModify(pi, m_cfg.modifyShiftPct)
                                                : BuildNormalModify(pi);

    // 祖先 :1069-1073：满足 just_close_flag / 资金费不合格 / 价差不合格时，把开仓开关压掉。
    // C++ 侧现在有三个等价输入：
    //   ① stopFlag / closeFlag —— 运维意图，直接读（就是下面这个 if）；
    //   ② just_close_flag —— 全局持仓闸门（SignalGenerator::m_globalPosExceeded）。
    //      **这里故意不再判它**：闸门写在 pi.orderParams 的 4 个开仓开关上
    //      （RecalcOrderParams 末尾，而 RecalcOrderParams 每个 OnTimer 都跑），
    //      上面 BuildNormalModify 整份拷 orderParams，所以超限时推给在跑的单的本来就是 false。
    //      在这里再判一次只是重复，还会多一条"两个地方各压一次、改一个忘一个"的隐患。
    // 仍未补：funding_time_qualified / unqualified_spread_flag —— 祖先这两条是"资格"状态，
    // 每轮重算后决定要不要开仓，C++ 侧没有对应字段（资金费只做了单 tick 的阈值判断）。
    if (pi.stopFlag || pi.closeFlag) {
        mod.ttOLSwitch = false;
        mod.ttOSSwitch = false;
        mod.mtOLSwitch = false;
        mod.mtOSSwitch = false;
    }

    // 先归零计时再回调：即使执行端因为算法单已在撤单流程里而跳过本次改参，
    // 也不要在 60s 内反复重试（祖先 :1077 同样是无条件重置）
    pi.algoModifyTime = nowUs;

    LOG_INFO("ProcessModify: pairKey:{} algoOrderId:{} aggressiveClose:{} stopFlag:{} closeFlag:{} -> CommandType_MODIFY",
             pi.pairInstrumentKey, pi.currentAlgoOrderId, aggressiveClose, pi.stopFlag, pi.closeFlag);
    m_algoOrderModifyCb(pi.currentAlgoOrderId, stra::CommandType_MODIFY, &mod);
}

void PairTradingContext::SubmitAlgoOrder(PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const {
    if (!m_algoCommandCb) {
        return;
    }

    // 直接创建算法单对象（不再拼 JSON 字符串），创建失败返回 nullptr
    BaseAlgoOrder* pAlgoOrder = BuildAlgoOrderJson(pi, algoMode, direction, forgoProfit);
    if (pAlgoOrder == nullptr) {
        LOG_WARN("算法单创建失败: BuildAlgoOrderJson 返回 nullptr -> 本次不报单 pairKey:{} algoMode:{} direction:{} forgoProfit:{}",
                 pi.pairInstrumentKey, algoMode, direction, forgoProfit);
        return;
    }

    auto& pim = PairInfoManager::Instance();
    pim.SetActiveAlgoOrder(pi.pairInstrumentKey, pAlgoOrder->algoOrderId);

    // 报单即"参数已经推给算法单了"，把两个计时起点一起归零（详见头文件注释）：
    //   satisfyTime    供 CheckAlgoOrderTimeout 算"机会连续不成立"的时长
    //   algoModifyTime 供 ProcessModify 算"距上次推参数多久"
    pi.satisfyTime = crypto::getCurrentTime();
    pi.algoModifyTime = pi.satisfyTime;

    // 交给 AlgoContext 注册：Init / 插入 algoOrderManager / 落库 / 订阅价差
    m_algoCommandCb(pAlgoOrder);
}

BaseAlgoOrder* PairTradingContext::BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const {
    const auto& op = pi.orderParams;
    const bool isTT = (algoMode == "TT");
    const bool isClose = (direction == "CL" || direction == "CS");

    // 1. 定位本次触发的是哪一组参数（TT/MT x OL/OS/CL/CS）
    const bool* pSw = nullptr;
    if (algoMode == "TT" && direction == "OL") {
        pSw = &op.ttOLSwitch;
    }
    else if (algoMode == "TT" && direction == "OS") {
        pSw = &op.ttOSSwitch;
    }
    else if (algoMode == "TT" && direction == "CL") {
        pSw = &op.ttCLSwitch;
    }
    else if (algoMode == "TT" && direction == "CS") {
        pSw = &op.ttCSSwitch;
    }
    else if (algoMode == "MT" && direction == "OL") {
        pSw = &op.mtOLSwitch;
    }
    else if (algoMode == "MT" && direction == "OS") {
        pSw = &op.mtOSSwitch;
    }
    else if (algoMode == "MT" && direction == "CL") {
        pSw = &op.mtCLSwitch;
    }
    else if (algoMode == "MT" && direction == "CS") {
        pSw = &op.mtCSSwitch;
    }
    else {
        LOG_ERROR("invalid algoMode:{} direction:{}", algoMode, direction);
        return nullptr;
    }

    const bool sw = *pSw;

    // 2. 开关校验：正常开仓必须开关打开；平仓（含风控强平）不受开关限制
    if (!sw && !isClose && forgoProfit == 0.0) {
        LOG_INFO("算法单创建失败: 开仓开关关闭且非平仓且无让利 -> 不报单 pairKey:{} algoMode:{} direction:{} switch:{} forgoProfit:{}",
                 pi.pairInstrumentKey, algoMode, direction, sw, forgoProfit);
        return nullptr;
    }

    // 3. 报单量有效性校验
    double targetVolume = isTT ? pi.ttTargetVolume : pi.mtTargetVolume;
    if (std::isnan(targetVolume) || targetVolume <= 0.0) {
        LOG_WARN("invalid targetVolume:{} pairKey:{} algoMode:{} direction:{}", targetVolume, pi.pairInstrumentKey, algoMode, direction);
        return nullptr;
    }

    // 4. 创建算法单对象
    //    可调参数**全部**来自 m_cfg（= etc/config.json 的 op 段），本函数不再有硬编码的
    //    调参值；剩下的常量是协议/身份类的（algoStrategyName、baseAsset、isManual 等）。
    //
    //    ⚠️ algoOrderId 必须在这里就分配好，且后续**不能再改**：
    //       SubmitAlgoOrder:598 会立刻把它写进 pi.currentAlgoOrderId，
    //       而 AlgoContext::SubmitAlgoOrder 里的重新分配会让两边脱钩
    //       （那是个已修的线上级 bug，见 AlgoContext.cpp:89 的注释）。
    const char* algoStrategyName = "pair_trading";

    AlgoPairOrder* pAlgoOrder = new AlgoPairOrder();
    pAlgoOrder->algoType = stra::AlgoType_PairTrading;

    // ---- 身份 / 币对 ----
    pAlgoOrder->algoOrderId = GenerateStrategyAlgoPairId();
    pAlgoOrder->commandType = stra::CommandType_NEW;
    pAlgoOrder->algoOrderStatus = stra::ALGO_OS_NEW;
    pAlgoOrder->insertTime = crypto::getCurrentTime();
    pAlgoOrder->updateTime = pAlgoOrder->insertTime;
    strncpy(pAlgoOrder->algoStrategyName, algoStrategyName, sizeof(pAlgoOrder->algoStrategyName) - 1);
    strncpy(pAlgoOrder->pairInstrumentKey, pi.pairInstrumentKey, sizeof(pAlgoOrder->pairInstrumentKey) - 1);
    strncpy(pAlgoOrder->activeInstrumentKey, pi.activeInstrumentKey, sizeof(pAlgoOrder->activeInstrumentKey) - 1);
    strncpy(pAlgoOrder->passiveInstrumentKey, pi.passiveInstrumentKey, sizeof(pAlgoOrder->passiveInstrumentKey) - 1);
    strncpy(pAlgoOrder->baseAsset, baseAsset.c_str(), sizeof(pAlgoOrder->baseAsset) - 1);

    // ---- 账户 ----
    pAlgoOrder->activeAccountId = pi.activeAccountId;
    pAlgoOrder->passiveAccountId = pi.passiveAccountId;

    // ---- 算法单参数：全部来自 PairTradingConfig（etc/config.json 的 op.pairTrading 段）----
    // 这里以前是一大段硬编码，改一个值要重编译整个工程；现在改配置即可。
    // 各字段的含义、单位、以及"2026-10-01 改过哪几个"见 PairTradingContext.h 的
    // PairTradingConfig 定义。
    const auto& aoc = m_cfg;

    // ---- 腿属性 ----
    pAlgoOrder->activeDriveType = stra::DriveType_ACTIVE;
    pAlgoOrder->passiveDriveType = stra::DriveType_PASSIVE;

    pAlgoOrder->tradingTypeOrder = isTT ? stra::TAKER_TAKER : stra::MAKER_TAKER;

    // 报单类型。判据最终落在**子单**的 orderType 上（BaseAlgoOrder.cpp:457
    // 用它决定走 Maker 还是 Taker 撤单分支），子单类型直接取自这里
    // （AlgoPairOrder.cpp:321-322 / 388-389 / 439-440 / 503-504）。
    pAlgoOrder->activeOrderType  = isTT ? aoc.ttActiveOrderType : aoc.mtActiveOrderType;
    pAlgoOrder->passiveOrderType = aoc.passiveOrderType;

    pAlgoOrder->activeDepthMakerCheck  = aoc.activeDepthMakerCheck;
    pAlgoOrder->activeDepthTakerCheck  = aoc.activeDepthTakerCheck;
    pAlgoOrder->passiveDepthMakerCheck = aoc.passiveDepthMakerCheck;
    pAlgoOrder->passiveDepthTakerCheck = aoc.passiveDepthTakerCheck;

    pAlgoOrder->activePriceTakerPct  = aoc.activePriceTakerPct;
    pAlgoOrder->activePriceMakerPct  = aoc.activePriceMakerPct;
    pAlgoOrder->passivePriceTakerPct = aoc.passivePriceTakerPct;
    pAlgoOrder->passivePriceMakerPct = aoc.passivePriceMakerPct;
    pAlgoOrder->passiveVolumePct     = aoc.passiveVolumePct;

    // ---- 撤单参数 ----
    // 配置里是**毫秒**（*CancelOrderTimeMs），BaseAlgoOrder 里是微秒，
    // 换算只在这一处做 —— 这样"差了 1000 倍"这类事故只有一个可能的位置。
    pAlgoOrder->activeMakerCancelOrderTime  = aoc.activeMakerCancelOrderTimeMs  * 1000LL;
    pAlgoOrder->activeTakerCancelOrderTime  = aoc.activeTakerCancelOrderTimeMs  * 1000LL;
    pAlgoOrder->passiveMakerCancelOrderTime = aoc.passiveMakerCancelOrderTimeMs * 1000LL;
    pAlgoOrder->passiveTakerCancelOrderTime = aoc.passiveTakerCancelOrderTimeMs * 1000LL;
    pAlgoOrder->activePassiveCancelOrderPct = aoc.activePassiveCancelOrderPct;
    pAlgoOrder->activeMakerCancelOrderPct   = aoc.activeMakerCancelOrderPct;
    pAlgoOrder->activeTakerCancelOrderPct   = aoc.activeTakerCancelOrderPct;
    pAlgoOrder->passiveMakerCancelOrderPct  = aoc.passiveMakerCancelOrderPct;
    pAlgoOrder->passiveTakerCancelOrderPct  = aoc.passiveTakerCancelOrderPct;

    // ---- 费率 / 滑点：复用信号侧的同一套配置，保证两边口径一致 ----
    // takerTakerFs / makerTakerFs 会直接参与 GetTargetPairOrder 的目标价差计算，不能留 0
    const auto& fs = SignalGenerator::Instance().GetConfig();
    pAlgoOrder->activeMakerFeeRate = fs.activeMakerFeeRate;
    pAlgoOrder->activeTakerFeeRate = fs.activeTakerFeeRate;
    pAlgoOrder->passiveMakerFeeRate = fs.passiveMakerFeeRate;
    pAlgoOrder->passiveTakerFeeRate = fs.passiveTakerFeeRate;
    // 信号侧与算法单侧只计一次滑点，放在主动腿
    pAlgoOrder->activeMakerSlippage = fs.basicSlippage;
    pAlgoOrder->activeTakerSlippage = fs.basicSlippage;
    pAlgoOrder->passiveMakerSlippage = 0.0;
    pAlgoOrder->passiveTakerSlippage = 0.0;
    // 必须与 SignalGenerator::CalcExecCost 完全同源：
    // CheckSignal 与 AlgoPairOrder 阶梯都用 (实时价差 ∓ 这里的 Fs) 去比同一个 StartSpread，
    // 两侧不同源就会错开一个 F，形成「信号触发但报不出单」的死区
    const auto& sg = SignalGenerator::Instance();
    pAlgoOrder->takerTakerFs = sg.CalcExecCost(true);
    pAlgoOrder->makerTakerFs = sg.CalcExecCost(false);

    // ---- 持仓 / 报单量 ----
    pAlgoOrder->pairTotalVolume = pi.pairTotalVolume;
    pAlgoOrder->pairActiveTotalPrice = pi.pairActiveTotalPrice;
    pAlgoOrder->pairPassiveTotalPrice = pi.pairPassiveTotalPrice;
    pAlgoOrder->pairPassiveTotalVolume = pi.pairPassiveTotalVolume;
    pAlgoOrder->ttTargetVolume = pi.ttTargetVolume;
    pAlgoOrder->mtTargetVolume = pi.mtTargetVolume;
    pAlgoOrder->minVolume = pi.minVolume;
    pAlgoOrder->maxMTOrderSize = aoc.maxMTOrderSize;
    pAlgoOrder->maxTTOrderSize = aoc.maxTTOrderSize;

    // ---- 开关 / 策略参数 ----
    pAlgoOrder->targetSpreadType = aoc.targetSpreadType;
    pAlgoOrder->activeVolumeCalcualteType = aoc.activeVolumeCalcualteType;
    pAlgoOrder->profitSwitch = pi.profitSwitch;
    pAlgoOrder->profitPct = pi.profitPct;
    // rebalance：MT 与 TT 分开配（2026-10-01 起 TT 两个都是 false，MT 两个都是 true）
    pAlgoOrder->mtRebalanceSwitch = aoc.mtRebalanceSwitch;
    pAlgoOrder->ttRebalanceSwitch = aoc.ttRebalanceSwitch;
    pAlgoOrder->mtRebalanceFlag = aoc.mtRebalanceFlag;
    pAlgoOrder->ttRebalanceFlag = aoc.ttRebalanceFlag;
    pAlgoOrder->mtPriceTrendProtectFlag = aoc.mtPriceTrendProtectFlag;
    pAlgoOrder->ttPriceTrendProtectFlag = aoc.ttPriceTrendProtectFlag;
    pAlgoOrder->activePriceTickFlag = aoc.activePriceTickFlag;
    pAlgoOrder->activePriceTickNum = aoc.activePriceTickNum;
    pAlgoOrder->passivePriceTickFlag = aoc.passivePriceTickFlag;
    pAlgoOrder->passivePriceTickNum = aoc.passivePriceTickNum;
    pAlgoOrder->isManual = pi.manualFlag;

    // ---- 32 个开平仓触发参数整体拷贝 ----
    pAlgoOrder->ttOLStartSpread = op.ttOLStartSpread;
    pAlgoOrder->ttOLEndSpread = op.ttOLEndSpread;
    pAlgoOrder->ttOLStartVolume = op.ttOLStartVolume;
    pAlgoOrder->ttOLEndVolume = op.ttOLEndVolume;
    pAlgoOrder->ttOLSwitch = op.ttOLSwitch;

    pAlgoOrder->ttCLStartSpread = op.ttCLStartSpread;
    pAlgoOrder->ttCLEndSpread = op.ttCLEndSpread;
    pAlgoOrder->ttCLStartVolume = op.ttCLStartVolume;
    pAlgoOrder->ttCLEndVolume = op.ttCLEndVolume;
    pAlgoOrder->ttCLSwitch = op.ttCLSwitch;

    pAlgoOrder->ttOSStartSpread = op.ttOSStartSpread;
    pAlgoOrder->ttOSEndSpread = op.ttOSEndSpread;
    pAlgoOrder->ttOSStartVolume = op.ttOSStartVolume;
    pAlgoOrder->ttOSEndVolume = op.ttOSEndVolume;
    pAlgoOrder->ttOSSwitch = op.ttOSSwitch;

    pAlgoOrder->ttCSStartSpread = op.ttCSStartSpread;
    pAlgoOrder->ttCSEndSpread = op.ttCSEndSpread;
    pAlgoOrder->ttCSStartVolume = op.ttCSStartVolume;
    pAlgoOrder->ttCSEndVolume = op.ttCSEndVolume;
    pAlgoOrder->ttCSSwitch = op.ttCSSwitch;

    pAlgoOrder->mtOLStartSpread = op.mtOLStartSpread;
    pAlgoOrder->mtOLEndSpread = op.mtOLEndSpread;
    pAlgoOrder->mtOLStartVolume = op.mtOLStartVolume;
    pAlgoOrder->mtOLEndVolume = op.mtOLEndVolume;
    pAlgoOrder->mtOLSwitch = op.mtOLSwitch;

    pAlgoOrder->mtCLStartSpread = op.mtCLStartSpread;
    pAlgoOrder->mtCLEndSpread = op.mtCLEndSpread;
    pAlgoOrder->mtCLStartVolume = op.mtCLStartVolume;
    pAlgoOrder->mtCLEndVolume = op.mtCLEndVolume;
    pAlgoOrder->mtCLSwitch = op.mtCLSwitch;

    pAlgoOrder->mtOSStartSpread = op.mtOSStartSpread;
    pAlgoOrder->mtOSEndSpread = op.mtOSEndSpread;
    pAlgoOrder->mtOSStartVolume = op.mtOSStartVolume;
    pAlgoOrder->mtOSEndVolume = op.mtOSEndVolume;
    pAlgoOrder->mtOSSwitch = op.mtOSSwitch;

    pAlgoOrder->mtCSStartSpread = op.mtCSStartSpread;
    pAlgoOrder->mtCSEndSpread = op.mtCSEndSpread;
    pAlgoOrder->mtCSStartVolume = op.mtCSStartVolume;
    pAlgoOrder->mtCSEndVolume = op.mtCSEndVolume;
    pAlgoOrder->mtCSSwitch = op.mtCSSwitch;

    // 平仓单必须清掉四个开仓开关（2026-09-27）。
    // 祖先在平仓路径上就是这么做的（create_close_modify_dict :258-263；建单处 :984-988）。
    // 不清的两个后果：
    //   ① AlgoContext.cpp:2106 的 FILLED 门槛要求四个开仓开关全 false —— 只要有一个是 true，
    //      平仓单在持仓归零之后也无法终结，只能靠显式撤单收场；
    //   ② AlgoPairOrder::CreatePairOrder 在同一侧两个开关都打开时**优先试开仓**
    //      （:653-661 / :640-648，MT 同构），于是"平仓单"会先挂出 OPEN 子单 ——
    //      轻则平不掉，重则加仓。
    if (isClose) {
        pAlgoOrder->ttOLSwitch = false;
        pAlgoOrder->ttOSSwitch = false;
        pAlgoOrder->mtOLSwitch = false;
        pAlgoOrder->mtOSSwitch = false;
    }

    // 5. 本次触发的模式+方向：套用风控价差修正，并确保开关打开
    double* pStartSpread = nullptr;
    double* pEndSpread = nullptr;
    bool*   pSwitch = nullptr;
    if (algoMode == "TT" && direction == "OL") {
        pStartSpread = &pAlgoOrder->ttOLStartSpread;
        pEndSpread = &pAlgoOrder->ttOLEndSpread;
        pSwitch = &pAlgoOrder->ttOLSwitch;
    }
    else if (algoMode == "TT" && direction == "OS") {
        pStartSpread = &pAlgoOrder->ttOSStartSpread;
        pEndSpread = &pAlgoOrder->ttOSEndSpread;
        pSwitch = &pAlgoOrder->ttOSSwitch;
    }
    else if (algoMode == "TT" && direction == "CL") {
        pStartSpread = &pAlgoOrder->ttCLStartSpread;
        pEndSpread = &pAlgoOrder->ttCLEndSpread;
        pSwitch = &pAlgoOrder->ttCLSwitch;
    }
    else if (algoMode == "TT" && direction == "CS") {
        pStartSpread = &pAlgoOrder->ttCSStartSpread;
        pEndSpread = &pAlgoOrder->ttCSEndSpread;
        pSwitch = &pAlgoOrder->ttCSSwitch;
    }
    else if (algoMode == "MT" && direction == "OL") {
        pStartSpread = &pAlgoOrder->mtOLStartSpread;
        pEndSpread = &pAlgoOrder->mtOLEndSpread;
        pSwitch = &pAlgoOrder->mtOLSwitch;
    }
    else if (algoMode == "MT" && direction == "OS") {
        pStartSpread = &pAlgoOrder->mtOSStartSpread;
        pEndSpread = &pAlgoOrder->mtOSEndSpread;
        pSwitch = &pAlgoOrder->mtOSSwitch;
    }
    else if (algoMode == "MT" && direction == "CL") {
        pStartSpread = &pAlgoOrder->mtCLStartSpread;
        pEndSpread = &pAlgoOrder->mtCLEndSpread;
        pSwitch = &pAlgoOrder->mtCLSwitch;
    }
    else {
        pStartSpread = &pAlgoOrder->mtCSStartSpread;
        pEndSpread = &pAlgoOrder->mtCSEndSpread;
        pSwitch = &pAlgoOrder->mtCSSwitch;
    }

    // 风控强平：放弃一部分利润，把触发价差往更容易成交的方向挪
    if (isClose && forgoProfit > 0.0) {
        if (direction == "CL") {
            *pStartSpread -= forgoProfit;
            *pEndSpread -= forgoProfit;
        }
        else {
            *pStartSpread += forgoProfit;
            *pEndSpread += forgoProfit;
        }
    }

    // 本次已经决定报单，开关必须是打开的（风控强平时对应开关可能是关的）
    *pSwitch = true;

    LOG_INFO("create algo order pairKey:{} algoMode:{} direction:{} startSpread:{} endSpread:{} forgoProfit:{}",
             pi.pairInstrumentKey, algoMode, direction, *pStartSpread, *pEndSpread, forgoProfit);

    return pAlgoOrder;
}

void PairTradingContext::OnPosition(const pubsub::Position& position) {
    // 记下"该账户的持仓批次已完整到达"（isLast = 批次尾）。
    // 启动对账靠它区分"该腿确实没持仓"和"该腿推送还没到"——
    // 交易所通常不会为从未持有过的腿推零仓，只等 push 会永远等不到。
    // Init 之前不记（m_startupTimeUs == 0 时 BatchDoneAfterStart 的语义不成立）。
    if (position.isLast && m_startupTimeUs > 0) {
        m_positionBatchDoneUs[position.accountId] = crypto::getCurrentTime();;
    }

    PairInfoManager::Instance().UpdateOnPosition(position);
    // PairInfoManager::Instance().UpdateLiquidStatus(position);
}

void PairTradingContext::OnBalance(const pubsub::Balance& balance) {
    PairInfoManager::Instance().UpdateOnBalance(balance, baseAsset);
}

void PairTradingContext::OnTotalAccount(const pubsub::TotalAccount& totalAccount) {
    PairInfoManager::Instance().UpdateOnTotalAccount(totalAccount);
}

// 建仓瞬间的小周期分位数快照（价差不回归风控的基准，祖先的 *_q_open）。
// 只取一次：字段从 NAN 变成有效值之后不再覆盖；完全平仓时由
// RiskManager::OnAlgoFinished 复位成 NAN，下一轮建仓重新取。
// 统计尚未建立（smallStats 无效）时不写，留 NAN 给后续事件补 ——
// 不能用"写入 NaN 也算写过"的写法，否则哨兵会失效。
static void CaptureOpenSpreadSnapshot(PairInfo& pi) {
    if (!pi.HasPosition() || !pi.smallStats.IsValid()) {
        return;
    }

    if (std::isnan(pi.openSmallSpreadBidBidUQ)) {
        pi.openSmallSpreadBidBidUQ = pi.smallStats.bidBidUQ;
    }

    if (std::isnan(pi.openSmallSpreadAskAskDQ)) {
        pi.openSmallSpreadAskAskDQ = pi.smallStats.askAskDQ;
    }
}

void PairTradingContext::OnAlgoOrderUpdate(BaseAlgoOrder* order) {
    if (order == nullptr) {
        return;
    }

    auto& pim = PairInfoManager::Instance();
    const std::string pairKey(order->pairInstrumentKey);
    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    // 只接受该对子"当前算法单"的回传。算法单终结后仍可能有子单回报走到
    // OnOrder -> PairOrderTrade；若不校验 ID，既会把已释放的对子重新结算一遍
    // （OnAlgoFinished 重复调用 -> 风控档位多加），也可能误释放后来新建的算法单。
    if (order->algoOrderId != pi->currentAlgoOrderId) {
        return;
    }

    // 量 / 价：执行端是唯一数据源，直接覆盖，不在策略侧做加权混合
    const bool wasFlat = !pi->HasPosition();
    pim.UpdateOnAlgoOrder(pairKey, order->pairTotalVolume, order->pairActiveTotalPrice, order->pairPassiveTotalPrice);

    // 建仓瞬间的小周期统计快照：只在"场外 -> 场内"的那一次事件上取，
    // 这样基准就是刚建仓时的分位数，而不是建仓之后任意时刻的。
    // 必须放在下面 terminal 早退**之前** —— 量的变化在非终态事件里就发生了，
    // 等到终态事件时 wasFlat 已经是 false，就永远取不到建仓瞬间。
    if (wasFlat && pi->HasPosition()) {
        CaptureOpenSpreadSnapshot(*pi);
    }

    // 状态：非终态只同步量价，不释放对子、不做结算
    const bool terminal = (order->algoOrderStatus == stra::ALGO_OS_FILLED ||
                           order->algoOrderStatus == stra::ALGO_OS_CANCELED ||
                           order->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED);
    if (!terminal) {
        return;
    }

    // 兜底：建仓那一刻统计还没建立（smallStats 无效）时上面会跳过，终态事件上再补一次。
    // 必须在 OnAlgoFinished 之前取，因为完全平仓时 OnAlgoFinished 会把 openSmallSpread* 复位成 NAN
    if (pi->HasPosition()) {
        CaptureOpenSpreadSnapshot(*pi);
    }

    // 风控：fullyFlat 必须在量已覆盖之后计算，不能提前取快照
    RiskManager::Instance().OnAlgoFinished(*pi, !pi->HasPosition());

    // 重算 orderParams
    SignalGenerator::Instance().RecalcOrderParams(*pi);

    // 释放对子，允许下一单
    pim.ClearActiveAlgoOrder(pairKey);
}

void PairTradingContext::OnTimer(int64_t nowUs) {
    auto& pim = PairInfoManager::Instance();
    auto& sg = SignalGenerator::Instance();

    // 0. 启动闸门：对账完成前，OnTimer 的一切动作全部跳过
    if (!IsTradingReady()) {
        if (!TryReconcile(nowUs)) {
            return;
        }
        LOG_WARN("启动闸门: Reconciling -> Trading, 交易放行(耗时 {}ms)", (nowUs - m_startupTimeUs) / 1000);
    }

    // 0.5 全局持仓闸门（祖先 cc_pricespread_gb_ltp.py:832 的 just_close_flag）
    //     所有对子持仓市值之和 > maxPositionValue ⇒ 只平不开。
    //
    //     位置必须在步骤 1/2 之前：那两步都会调 RecalcOrderParams，闸门要在那一刻已经就位。
    //     闸门本体在 SignalGenerator::RecalcOrderParams 末尾（它有 4 个生产调用点：Init /
    //     OnAlgoFinished / OnTimer 第 1、2 步 —— 逐个去压开关必漏），
    //     这里只负责"算出结论 + 状态翻转时打一行日志"。
    //
    //     ⚠️ 不写 pi.closeFlag：closeFlag 是**运维意图**，会进快照、会被 RESUME 清掉，
    //        拿它当计算出来的闸门用会跟运维操作互相覆盖（见 docs/PairInfo-flag-审计.md）。
    //     maxPositionValue <= 0 = 关闭，此时连 TotalPositionValue() 都不算。
    //
    //     ⚠️ 只搬了祖先判据的第一项（position_value_all > max_position_value）。祖先 :832 那一行
    //        还有两个杠杆分支（position_value_both_max / active_total_pnl > leverage，被动腿同理），
    //        它们要"账户权益"当分母，而 C++ 侧这个口径没接通 —— UpdateOnBalance 拼的 key 匹配不上
    //        （两腿推的 currency 都是 USDT），那两行是空转。所以**没搬**，不是漏了，是缺输入。
    const double cap = m_cfg.maxPositionValue;
    const double totalPv = (cap > 0.0) ? pim.TotalPositionValue() : 0.0;
    const bool exceeded = (cap > 0.0) && (totalPv > cap);
    if (exceeded != sg.GetGlobalPositionExceeded()) {
        sg.SetGlobalPositionExceeded(exceeded);
        if (exceeded) {
            LOG_WARN("全局持仓闸门: 所有对子持仓市值之和 {} > maxPositionValue {} -> 只平不开（已有持仓照常平，新开仓全部禁止）", totalPv, cap);
        }
        else {
            LOG_INFO("全局持仓闸门解除: 持仓市值之和 {} <= maxPositionValue {} -> 恢复开仓", totalPv, cap);
        }
    }
    
    // 0.6 运行时配置：把 T_signal 投递过来的配置落地
    RuntimeConfig rc;
    bool configChanged = false;
    while (m_cfgQ.pop(rc)) {
        configChanged = true;
    }

    if (configChanged) {
        sg.SetCloseOnly(rc.closeOnly);
        m_cfg.closeOnly = rc.closeOnly;
        m_cfg.stopAllAlgo = rc.stopAllAlgo;

        // 立刻重算 orderParams 并把 algoModifyTime 归零。
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            sg.RecalcOrderParams(*pi);
            pi->algoModifyTime = 0;   // 0 = 下一轮 ProcessModify 立即推（见它的计时判断）
        }

        // 一行覆盖两个字段：运维要能从日志确认自己那次保存到底生效成了什么。
        LOG_WARN("运行时配置: closeOnly={} stopAllAlgo={}", rc.closeOnly, rc.stopAllAlgo);
    }

    // 0.7 停掉所有算法单：每 tick 撤一遍
    //     为什么不是"只在翻转时撤一次"：撤单是异步两段式的（CANCELLING 要等被动腿成交才终结），
    //     而且翻转那一 tick 之前可能刚有单被报出去。每 tick 撤一次才能维持
    //     "只要 stopAllAlgo 为 true，就每 tick 都在请求撤单"。
    //     对已经在撤单流程里的单重复发 CANCEL 是安全的：CheckAlgoOrderTimeout 到点后
    //     也是每 tick 发一次（见该函数的注释）。
    //
    //     ⚠️ 别把这条写成"保证场上没有活单"：CANCELLING 只撤主动腿，被动腿留着等成交，
    //        而那个等待**没有上界**（见 ProcessRisk 里 464-473 行的说明）。所以打开本开关后
    //        仍可能有一张单长期挂在撤单中，对子也一直被它占着。这是既有的已知残余风险，
    //        不是本开关引入的，但运维必须知道 —— "打开 stopAllAlgo" ≠ "场上立刻干净"。
    if (m_cfg.stopAllAlgo) {
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            if (pi->hasActiveAlgoOrder) {
                RequestCancelAlgoOrder(*pi);
            }
        }
    }
    

    // 1. 重算报单量参数（每分钟）
    //    量一变就得重算 orderParams —— orderParams 的 16 个 *StartVolume/*EndVolume
    //    直接取自 pi.maxVolume，量变了不重算，它们就是上一轮的旧值。
    //    冷启动时这条尤其关键：Init 里那一次 RecalcOrderParams 拿到的 maxVolume 还是 0
    //    （腿价要等第一条行情才有效），于是 ttOLEndVolume = 0 ->
    //    开仓判据 `vol > ttOLEndVolume + 1e-9` 退化成 `0 > 1e-9` -> **一单都开不出去**，
    //    而且下一次重算要等到统计刷新（默认 60s），统计无效时还会被早退掉。
    if (nowUs - m_lastVolumeRecalcUs > m_cfg.volumeRecalcIntervalSec * 1000000LL) {
        pim.RecalcVolumeParams(m_cfg.maxAmount, m_cfg.targetAmount, m_cfg.exposureMaxLimit, m_cfg.exposureMaxLimitCoff);
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            sg.RecalcOrderParams(*pi);
        }
        m_lastVolumeRecalcUs = nowUs;
    }

    // 2. 应用 T_signal 算好的价差统计（每 spreadStatsUpdateIntervalSec 秒一轮，默认60s）
    SpreadStatsMsg msg;
    while (m_statsQ.pop(msg)) {
        PairInfo* pi = pim.GetPairInfo(msg.pairKey);
        if (!pi) {
            continue;
        }

        pim.UpdateLargeStats(msg.pairKey, msg.st);
        sg.RecalcOrderParams(*pi);
    }
    
    // 3. 风控检查 (每次定时器触发)
    for (PairInfo* pi : pim.GetAllPairInfos()) {
        ProcessRisk(*pi, nowUs);
    }

    // 4. 撤单触发检查 (每次定时器触发)
    //    祖先 on_timer 的五条撤单触发里已落地的两条：
    //      ① 机会超时（:1021-1026）—— satisfyTime 连续 algoOrderTimeoutMs 不成立
    //      ⑤ 敞口异常（:1081-1092）—— 净敞口超阈值，置 errorFlag 并撤单
    //    敞口检查不判 autoFlag（祖先 :1081 那一段没有 auto_flag 门槛）；
    //    超时检查自带 autoFlag 门槛（祖先 :1018-1020）。
    for (PairInfo* pi : pim.GetAllPairInfos()) {
        CheckAlgoOrderTimeout(*pi, nowUs);
        CheckExposureAbnormal(*pi);
    }

    // 5. 改参检查 (每次定时器触发)
    //    祖先 :1059-1078：每 modifyTimespanSec 秒把 pair_info 的最新参数推给在跑的算法单。
    //    顺序上放在撤单之后（祖先也是先撤后改），且 AlgoContext 的 MODIFY 分支会跳过
    //    已在撤单流程里的算法单，所以不会出现"刚请求撤单又去改它"的竞争。
    for (PairInfo* pi : pim.GetAllPairInfos()) {
        ProcessModify(*pi, nowUs);
    }

    // 6. 快照落盘
    if (nowUs - m_lastCsvSaveUs > static_cast<int64_t>(m_cfg.csvSaveIntervalSec) * 1000000LL) {
        const int n = pim.CollectSnapshotRows(m_snapScratch);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                m_snapQ.push(m_snapScratch[i]);
            }
            m_snapQ.push(PairInfoRecord{});   // 哨兵：pairKey 全零
        }

        m_lastCsvSaveUs = nowUs;
    }
}

// 启动对账
// 前提：pairTotalVolume 就是主动腿持仓（带符号，负 = 多），所以对账就是与
// activeRealPosition 直接相等比较，不需要任何换算。被动腿不参与主判据。
bool PairTradingContext::ReconcilePair(PairInfo& pi) {
    const double real = pi.activeRealPosition;  // 主动腿实时持仓（带符号；未推送 = 默认 0 = 空仓）
    const double ledger = pi.pairTotalVolume;     // 策略自己的账本（算法单回调累积）

    const double tol = std::max(1e-9, std::abs(real) * m_cfg.reconcileTolRatio);

    // 判据：量在容差内 且 符号相同（量级相同但多空翻转必须算不一致）
    const bool sameSign = (ledger >= 0.0) == (real >= 0.0);
    if (std::abs(ledger - real) <= tol && sameSign) {
        LOG_INFO("Reconcile ok: pairKey:{} pairTotalVolume:{} activeRealPosition:{}", pi.pairInstrumentKey, ledger, real);
        return true;
    }

    LOG_ERROR("Reconcile MISMATCH: pairKey:{} 账本 pairTotalVolume:{} vs 实时 activeRealPosition:{} (tol:{}) -> 以系统推送为准", pi.pairInstrumentKey, ledger, real, tol);

    if (std::abs(real) <= 1e-9) {
        // 交易所是空仓 -> 账本作废，并把"持仓期间"的状态一起归零。
        // 必须和 RiskManager::OnAlgoFinished(fullyFlat == true) 保持同一套不变量
        // （空仓 ⇒ 风控档位 / 计时起点 / 建仓基准全部复位），否则下一轮建仓会
        // 从上一轮的 tier 和旧基准接着算。
        pi.pairTotalVolume       = 0.0;
        pi.pairActiveTotalPrice  = -1.0;   // -1.0 = "从未建仓"哨兵
        pi.pairPassiveTotalPrice = -1.0;
        pi.openSmallSpreadBidBidUQ = std::nan("");
        pi.openSmallSpreadAskAskDQ = std::nan("");
        pi.adlClose           = AbnormalCloseState();
        pi.spreadNoRegression = AbnormalCloseState();
        pi.fundingAbnormal    = AbnormalCloseState();
        pi.positionExceedThresholdStartTime = 0;
        pi.spreadNoRegressionStartTime      = 0;
        pi.positionStartTime                = 0;
        LOG_WARN("Reconcile: pairKey:{} 交易所已空仓 -> 账本清零 + 风控档位/建仓基准复位",
                 pi.pairInstrumentKey);
    } else {
        // 交易所仍持仓 -> 只把量改成实时值。
        // 记账均价（pairActiveTotalPrice/pairPassiveTotalPrice）与风控档位**保留**：
        //   均价是建仓基准，交易所的 avgPrice 在部分平仓后不变，快照值仍然是有效的建仓价；
        //   档位是跨轮次累积的风控耐心，重启不该重置。
        // ⚠️ 但如果进程宕机期间这个对子被平掉又重开，快照里的均价就是陈旧的
        //    —— 这是上面这条 LOG_ERROR 要人工看一眼的原因。
        pi.pairTotalVolume = real;
    }

    return true;
}

// 周期尝试对账。返回 true = 可以放行。
// 策略：**一直等 + 告警，不设超时放行** —— 按"无持仓"放行的后果是在孤儿仓上重复开仓。
bool PairTradingContext::TryReconcile(int64_t nowUs) {
    auto& pim = PairInfoManager::Instance();

    for (PairInfo* pi : pim.GetAllPairInfos()) {
        // 该腿"已知"有两个来源，满足其一即可：
        //   ① 收到过该腿的持仓推送（activePushArrived / passivePushArrived）；
        //   ② 该腿所在账户的持仓批次已完整到达过（isLast）—— 批次里没有它 = 它是空仓，
        //      此时 activeRealPosition / passiveRealPosition 保持默认 0，对账照样成立。
        // 只等 ① 是不够的：交易所一般不会为"从未持有过"的腿推零仓，会永远等不到。
        const bool activeKnown  = pi->activePushArrived  || BatchDoneAfterStart(pi->activeAccountId);
        const bool passiveKnown = pi->passivePushArrived || BatchDoneAfterStart(pi->passiveAccountId);

        if (!activeKnown || !passiveKnown) {
            // 还没到。超时只告警，**不放行** —— 按"无持仓"放行会在孤儿仓上重复开仓。
            if (nowUs - m_lastReconcileWarnUs > static_cast<int64_t>(m_cfg.reconcileWarnIntervalSec) * 1000000LL) {
                LOG_ERROR("启动闸门等待中: pairKey:{} 持仓信息未到位 "
                          "(activePush:{} activeBatch:{} passivePush:{} passiveBatch:{}) —— "
                          "已等 {}s，交易保持冻结，请检查持仓订阅",
                          pi->pairInstrumentKey,
                          pi->activePushArrived, BatchDoneAfterStart(pi->activeAccountId),
                          pi->passivePushArrived, BatchDoneAfterStart(pi->passiveAccountId),
                          (nowUs - m_startupTimeUs) / 1000000);
                m_lastReconcileWarnUs = nowUs;
            }
            return false;
        }
    }

    // 所有对子的持仓信息都已知 -> 逐一对账（不一致的当场改成实时值）
    for (PairInfo* pi : pim.GetAllPairInfos()) {
        ReconcilePair(*pi);
    }

    m_phase = StartupPhase::Trading;
    return true;
}

}