/********
 * 核心流程：
 * OnSpread:
 * 1. 更新 pi.rtSpread
 * 2. AccumulateSpreadSample --》喂一条价差样本进 24h 滚动窗口（降频）
 * 3. 机会条件快照 --》UpdateSatisfyTime（对每个对子都算，与有没有在跑的单无关）
 * 4. CheckSignal --》有信号 --》SubmitAlgoOrder
 * 
 * OnTimer:
 * 1. 周期性 RecalcVolumeParams
 * 2. 周期性 刷新价差统计 Prune/Build --》UpdateLargeStats --》立即 RecalcOrderParams
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
#include <cmath>


namespace pt {

PairTradingContext* PairTradingContext::s_instance = nullptr;

PairTradingContext::PairTradingContext() = default;

PairTradingContext::~PairTradingContext() {
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

    auto& pim = PairInfoManager::Instance();
    pim.Init(cfg.pairKeys, cfg.activeAccountId, cfg.passiveAccountId, smc);

    // 恢复上一轮的"策略自己那本账"。原实现把成功/失败打反了（成功打 WARN、失败打 INFO），
    // 且 LoadFromCSV 是个一行都不写的坏桩 —— 这里一并修正。
    if (!cfg.csvStatePath.empty()) {
        const int restored = pim.LoadSnapshot(cfg.csvStatePath);
        if (restored < 0) {
            LOG_WARN("LoadSnapshot: {} 不可用（首次启动 / 格式不兼容）-> 按无持仓启动", cfg.csvStatePath);
        } else {
            LOG_INFO("LoadSnapshot: {} pair(s) restored from {}", restored, cfg.csvStatePath);
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

    LOG_WARN("启动闸门: Reconciling —— 交易冻结，等两腿持仓推送到位后对账（pairs:{}）", cfg.pairKeys.size());
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
        LOG_ERROR("孤儿单告警: pairKey:{} 上一轮算法单未终结 algoOrderId:{} —— "
                  "交易所侧可能残留子单，账本(pairTotalVolume:{})不可信，请人工确认",
                  pi->pairInstrumentKey, pi->currentAlgoOrderId, pi->pairTotalVolume);

        if (m_cfg.freezeOnOrphanAlgoOrder) {
            // 复用 errorFlag 的"判死、停自动、留人工处理"语义（它现在有清除路径：PairCmd_RESUME）
            pi->errorFlag = true;
            LOG_ERROR("孤儿单冻结: pairKey:{} errorFlag = true（运维确认交易所已清干净后发 PairCmd_RESUME 复活）",
                      pi->pairInstrumentKey);
        }
    }
}

void PairTradingContext::OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    std::string pairKey(topic->__name);
    auto& pim = PairInfoManager::Instance();
    pim.UpdateRtSpread(pairKey, pdata);

    // 喂一条价差样本进 24h 滚动窗口（内部按 spreadSampleIntervalMs 降频）
    // 注意：这里只负责“收集”，不负责算分位数；分位数在 OnTimer 里按周期重算
    AccumulateSpreadSample(pairKey, pdata);

    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    ProcessPairSignal(*pi);
}

void PairTradingContext::AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata) {
    // generateTs 是价差生成时间，单位 us，
    const int64_t ts = pdata->generateTs;

    auto it = m_spreadWindows.find(pairKey);
    if (it == m_spreadWindows.end()) {
        SpreadWindow win;
        win.builder.SetWindowUs(static_cast<int64_t>(m_cfg.spreadStatsWindowSec) * 1000000LL);
        it = m_spreadWindows.emplace(pairKey, std::move(win)).first;
    }
    SpreadWindow& win = it->second;

    if (m_cfg.spreadSampleIntervalMs > 0 && win.lastSampleTs > 0) {
        const int64_t minGapUs = static_cast<int64_t>(m_cfg.spreadSampleIntervalMs) * 1000LL;
        if (ts - win.lastSampleTs < minGapUs) {
            return;
        }
    }

    SpreadSample s;
    s.ts = ts;
    s.sba = static_cast<float>(pdata->spreadBidAsk);
    s.sbb = static_cast<float>(pdata->spreadBidBid);
    s.sab = static_cast<float>(pdata->spreadAskBid);
    s.saa = static_cast<float>(pdata->spreadAskAsk);
    s.abv = static_cast<float>(pdata->activeBidVolume[0]);
    s.aav = static_cast<float>(pdata->activeAskVolume[0]);

    win.builder.Add(s);
    win.lastSampleTs = ts;
}

void PairTradingContext::ProcessPairSignal(PairInfo& pi) {
    // 启动闸门：对账完成前不下任何单。
    // 价差推送通常早于持仓推送，此时 pairTotalVolume 还是快照值（或 0），照常跑信号会：
    //   账本 = 0 而交易所有仓  -> 在孤儿仓上再开一笔（CanOpen 没有持仓门槛，CanClose 又平不掉）
    //   账本 ≠ 0 而交易所为空  -> 平仓单实际是反向开仓
    if (!IsTradingReady()) {
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

    // 这里不打日志：ProcessRisk 每个 tick 都会走到这里，直到算法单终结、对子被释放为止，
    // 打日志会刷屏。真正发生状态跃迁的地方（AlgoContext::SubmitAlgoOrder 的 CANCEL 分支）
    // 已经有一条日志。
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
    // 祖先的三个 satisfy_time 写入点：
    //   open_satisfy_index（:922）  = 一大串开仓前置条件同时成立
    //   manual_index（:930）        = status==READY & auto_flag==False & !stop_flag
    //   close_satisfy_index（:936） = (except_close_long|except_close_short) & 价差合格 & !stop_flag & 敞口合格
    // C++ 侧用 CanOpen/CanClose 承担那串前置条件，用 CheckSignal 承担"价差已穿越入口阈值"。
    const bool openSatisfied = canOpen && HasOpenSignal(sig);
    const bool closeSatisfied = canClose && HasCloseSignal(sig);

    // 手动对子（autoFlag == false）没有信号也要续命，否则 CheckAlgoOrderTimeout 会撤它的单。
    // 祖先 :930 就是干这个的；虽然 :1018-1020 已经跳过手动单，这里是双保险，保持与祖先一致。
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

    LOG_INFO("CheckAlgoOrderTimeout: pairKey:{} algoOrderId:{} idleSec:{} > {}s -> cancel",
             pi.pairInstrumentKey, pi.currentAlgoOrderId,
             static_cast<double>(idleUs) / 1000000.0,
             static_cast<double>(m_cfg.algoOrderTimeoutMs) / 1000.0);
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
    const double netExposure = std::abs(pi.activeRealPosition * activeMultiple +
                                        pi.passiveRealPosition * pi.passiveParam.multiple) / activeMultiple;
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
    // C++ 侧只有 stopFlag（停止开仓）和 closeFlag（强制平仓）这两个等价输入；
    // 祖先那三个 flag（just_close_flag / funding_time_qualified / unqualified_spread_flag）
    // 目前没有对应物，属于待补项。
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
    //    除下面显式赋值的字段外，其余参数先取默认值，后续统一改为从 PairTradingConfig 读取
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

    // ---- 腿属性（默认值，后续从 PairTradingConfig 来）----
    pAlgoOrder->activeDriveType = stra::DriveType_ACTIVE;
    pAlgoOrder->passiveDriveType = stra::DriveType_PASSIVE;
    // TT 主动腿吃单 -> MARKET；MT 主动腿挂单 -> LIMIT
    pAlgoOrder->activeOrderType = isTT ? OT_MARKET : OT_LIMIT;
    // 被动腿永远是吃单腿
    pAlgoOrder->passiveOrderType = OT_MARKET;

    pAlgoOrder->activeDepthMakerCheck = false;
    pAlgoOrder->activeDepthTakerCheck = false;
    pAlgoOrder->passiveDepthMakerCheck = false;
    pAlgoOrder->passiveDepthTakerCheck = false;

    pAlgoOrder->activePriceTakerPct = 0.0;
    pAlgoOrder->activePriceMakerPct = 0.0;
    pAlgoOrder->passivePriceTakerPct = 0.0;
    pAlgoOrder->passivePriceMakerPct = 0.0;
    pAlgoOrder->passiveVolumePct = 0.5;

    // ---- 撤单参数（默认值，后续从 PairTradingConfig 来）----
    pAlgoOrder->activeMakerCancelOrderTime = 5LL * 1000 * 1000;
    pAlgoOrder->activeTakerCancelOrderTime = 5LL * 1000 * 1000;
    pAlgoOrder->passiveMakerCancelOrderTime = 5LL * 1000 * 1000;
    pAlgoOrder->passiveTakerCancelOrderTime = 5LL * 1000 * 1000;
    pAlgoOrder->activePassiveCancelOrderPct = 0.001;
    pAlgoOrder->activeMakerCancelOrderPct = 0.001;
    pAlgoOrder->activeTakerCancelOrderPct = 0.001;
    pAlgoOrder->passiveMakerCancelOrderPct = 0.001;
    pAlgoOrder->passiveTakerCancelOrderPct = 0.001;

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
    pAlgoOrder->maxMTOrderSize = 1.0;   // 默认值，后续从 PairTradingConfig 来
    pAlgoOrder->maxTTOrderSize = 1.0;   // 默认值，后续从 PairTradingConfig 来

    // ---- 开关 / 策略参数（默认值，后续从 PairTradingConfig 来）----
    pAlgoOrder->targetSpreadType = stra::TargetSpredPrice_NOW;
    pAlgoOrder->activeVolumeCalcualteType = stra::ActiveVolumeCalcualteType_PassiveVolumePct;
    pAlgoOrder->profitSwitch = pi.profitSwitch;
    pAlgoOrder->profitPct = pi.profitPct;
    pAlgoOrder->mtRebalanceSwitch = true;
    pAlgoOrder->ttRebalanceSwitch = true;
    pAlgoOrder->mtRebalanceFlag = true;
    pAlgoOrder->ttRebalanceFlag = true;
    pAlgoOrder->mtPriceTrendProtectFlag = false;
    pAlgoOrder->ttPriceTrendProtectFlag = false;
    pAlgoOrder->activePriceTickFlag = false;
    pAlgoOrder->activePriceTickNum = 0;
    pAlgoOrder->passivePriceTickFlag = false;
    pAlgoOrder->passivePriceTickNum = 0;
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
    // 开仓开关只看价差和 closeFlag（SignalGenerator.cpp:120-126），不判持仓，
    // 所以持仓期间完全可能是 true，尤其在强平触发时（持仓正亏着，价差朝不利方向走）。
    //
    // 只清开仓开关，**不动平仓开关**：C++ 有独立的 TT 平仓路径
    // （ProcessPairSignal 按 sig.ttCLSignal 派发 "TT","CL"），祖先那套"平仓只走 MT"
    // （ttCL/ttCS=false、mtCL/mtCS=true）在这里会把 TT 平仓打断，属于另一个设计问题。
    //
    // 附带效果：CreatePairOrder 里 -minVolume < expectVolume < minVolume 的中间带分支
    // （:665-674 / :702-711）只试开仓开关、从不试平仓。清掉之后这里返回空单 ->
    // 不再挂新子单 -> allPairOrders 归零 -> 正好触发 FILLED，平仓的收尾也就顺了。
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

// ⚠️ 已废弃（2026-09-29）：本函数现在**没有任何读取方**。
//   "价差不回归"风控已改用 SignalGenerator::CloseSpreadReached（与执行端平仓阈值同源同轴），
//   不再依赖 openSmallSpread* / smallStats。而 smallStats 全工程本来就没有生产者
//   （PairInfoManager::UpdateSmallStats 零调用者），所以本函数的 IsValid() 守卫从来
//   没通过过 —— 两个字段恒为 NaN。保留函数 + 快照两列只是为了不改动 CSV 列布局，
//   待快照格式统一迁移时连同 smallStats / UpdateSmallStats 一起删除。
//
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
    //    （ProcessRisk / CheckAlgoOrderTimeout / CheckExposureAbnormal / ProcessModify
    //     四个入口都只从这里调用，所以在这一处挡住就够了）。
    //    注意价差统计窗口（下面第 2 步）也一起跳过 —— 但它在 OnSpread 里继续积累样本，
    //    所以放行后第一轮 Build 就能拿到足够样本，不需要重新等 24h。
    if (!IsTradingReady()) {
        if (!TryReconcile(nowUs)) {
            return;
        }
        LOG_WARN("启动闸门: Reconciling -> Trading, 交易放行(耗时 {}ms)", (nowUs - m_startupTimeUs) / 1000);
    }

    // 1. 重算报单量参数（每分钟）
    if (nowUs - m_lastVolumeRecalcUs > m_cfg.volumeRecalcIntervalSec * 1000000LL) {
        pim.RecalcVolumeParams(m_cfg.maxAmount, m_cfg.targetAmount, m_cfg.exposureMaxLimit, m_cfg.exposureMaxLimitCoff);
        m_lastVolumeRecalcUs = nowUs;
    }

    // 2. 刷新价差统计（每 spreadStatsUpdateIntervalSec 秒，默认60s）
    //    统计一更新就立刻重算 orderParams，保证用到的分位数不是陈旧的
    if (nowUs - m_lastSpreadStatsUpdateUs > static_cast<int64_t>(m_cfg.spreadStatsUpdateIntervalSec) * 1000000LL) {
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            const std::string pairKey(pi->pairInstrumentKey);

            auto it = m_spreadWindows.find(pairKey);
            if (it == m_spreadWindows.end()) {
                continue;
            }

            SpreadStatsBuilder& builder = it->second.builder;
            builder.Prune(nowUs); // 先按 24h 窗口淘汰过期样本

            const SpreadStats stats = builder.Build(m_cfg.quantileUp, m_cfg.quantileDn, static_cast<size_t>(m_cfg.spreadStatsMinSamples));
            pim.UpdateLargeStats(pairKey, stats);

            sg.RecalcOrderParams(*pi);
        }
        m_lastSpreadStatsUpdateUs = nowUs;
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

    // 6. 快照落盘 (原子写)。周期已从 300s 收紧到 10s —— 只有几十行，代价可忽略，
    //    换来崩溃时最多丢 10s 的账（见 docs/restart_recovery_design.md §5.1）。
    if (nowUs - m_lastCsvSaveUs > static_cast<int64_t>(m_cfg.csvSaveIntervalSec) * 1000000LL) {
        if (!m_cfg.csvStatePath.empty()) {
            pim.SaveSnapshot(m_cfg.csvStatePath);
        }
        m_lastCsvSaveUs = nowUs;
    }
}

// ---------------------------------------------------------------------------
// 启动对账（docs/restart_recovery_design.md §5.2 / §5.3）
//
// 前提：pairTotalVolume 就是主动腿持仓（带符号，负 = 多），所以对账就是与
// activeRealPosition 直接相等比较，不需要任何换算。被动腿不参与主判据。
// ---------------------------------------------------------------------------

// 对账单个对子（调用前保证两腿持仓信息都已知）。返回 true 表示该对子已一致。
// 注意：activeRealPosition 也可能是"账户批次里根本没有这条腿"推出来的默认 0 ——
// 那正是我们要的语义（该腿空仓）。
bool PairTradingContext::ReconcilePair(PairInfo& pi) {
    const double real   = pi.activeRealPosition;  // 主动腿实时持仓（带符号；未推送 = 默认 0 = 空仓）
    const double ledger = pi.pairTotalVolume;     // 策略自己的账本（算法单回调累积）

    const double tol = std::max(1e-9, std::abs(real) * m_cfg.reconcileTolRatio);

    // 判据：量在容差内 且 符号相同（量级相同但多空翻转必须算不一致）
    const bool sameSign = (ledger >= 0.0) == (real >= 0.0);
    if (std::abs(ledger - real) <= tol && sameSign) {
        LOG_INFO("Reconcile ok: pairKey:{} pairTotalVolume:{} activeRealPosition:{}",
                 pi.pairInstrumentKey, ledger, real);
        return true;
    }

    LOG_ERROR("Reconcile MISMATCH: pairKey:{} 账本 pairTotalVolume:{} vs 实时 activeRealPosition:{} "
              "(tol:{}) -> 以系统推送为准", pi.pairInstrumentKey, ledger, real, tol);

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