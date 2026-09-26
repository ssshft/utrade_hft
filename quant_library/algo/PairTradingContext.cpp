/********
 * 核心流程：
 * OnSpread:
 * 1. 更新 pi.rtSpread
 * 2. CheckSignal --》有信号 --》SubmitAlgoOrder
 * 3. 调用RecalcOrderParams
 * 
 * OnTimer:
 * 1. 周期性 RecalcVolumeParams
 * 2. 周期性 CheckRisk --> 有风险 --》SubmitAlgoOrder (强制平仓)
 * 3. 周期性 SaveToCSV
 * 4. 周期性 调用RecalcOrderParams(全量刷新)
 * 
 * OnAlgoOrderUpdate (成交回报)
 * 1. 更新持仓均价 (PairInfoManager)
 * 2. 重算 orderParams (信号参数变化)
 * 3. 记录开仓时小周期统计快照 (用于价差不回归判断)
 * 
 * ******/

#include "PairTradingContext.h"
#include "basic/DataStruct.h"
#include "basic/AlgoPairOrder.h"


namespace pt {

int64_t PairTradingContext::NowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t PairTradingContext::GenerateAlgoOrderId() {
    static std::atomic<int64_t> seq{0};
    int64_t id = NowUs() * 1000 + (seq.fetch_add(1) % 1000);
    return id;
}

PairTradingContext::PairTradingContext() = default;

void PairTradingContext::Init(const PairTradingConfig& cfg, sm::SecurityManager* s) {
    m_cfg = cfg;
    smc = s;

    auto& pim = PairInfoManager::Instance();
    pim.Init(cfg.pairKeys, cfg.activeAccountId, cfg.passiveAccountId, smc);

    if (!cfg.csvStatePath.empty()) {
        if (pim.LoadFromCSV(cfg.csvStatePath)) {
            LOG_WARN("");
        }
    }

    LOG_INFO("size: {}", cfg.pairKeys.size());
}

void PairTradingContext::OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    std::string pairKey(topic->__name);

    auto& pim = PairInfoManager::Instance();
    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pim.UpdateRtSpread(pairKey, pdata);

    ProcessPairSignal(*pi);
}

void PairTradingContext::ProcessPairSignal(PairInfo& pi) {
    if (pi.hasActiveAlgoOrder) {
        return;
    }


    auto& sg = SignalGenerator::Instance();

    std::string reason = "";
    bool canOpen = sg.CanOpen(pi, reason);
    bool canClose = sg.CanClose(pi, reason);

    SignalResult sig = sg.CheckSignal(pi);
    if (!sig.hasSignal) {
        return;
    }

    LOG_INFO("");

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
    if (!pi.HasPosition()) {
        return;
    }

    if (pi.hasActiveAlgoOrder) {
        return;
    }

    auto& rm = RiskManager::Instance();
    RiskCheckResult risk = rm.CheckRisk(pi, nowUs);

    if (!risk.needForceClose) {
        return;
    }

    LOG_INFO("reson: {}");


    if (pi.IsLong()) {
        std::string mode = pi.autoFlag ? "TT" : "MT";
        SubmitAlgoOrder(pi, mode, "CL", risk.forgoProfit);
    }
    else if (pi.IsShort()) {
        std::string mode = pi.autoFlag ? "TT" : "MT";
        SubmitAlgoOrder(pi, mode, "CS", risk.forgoProfit);    
    }
}

void PairTradingContext::SubmitAlgoOrder(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const {
    if (!m_algoCommandCb) {
        return;
    }

    // 直接创建算法单对象（不再拼 JSON 字符串），创建失败返回 nullptr
    BaseAlgoOrder* pAlgoOrder = BuildAlgoOrderJson(pi, algoMode, direction, forgoProfit);
    if (pAlgoOrder == nullptr) {
        return;
    }

    // 先占住这个对子，避免算法单未结束时同一对子重复触发。
    // 注意：这里存的 id 必须和算法单对象的 algoOrderId 一致（ScanFinishedAlgoOrders
    //      会用 stoll(currentAlgoOrderId) 去 AlgoContext 里查这个算法单）
    auto& pim = PairInfoManager::Instance();
    pim.SetActiveAlgoOrder(pi.pairInstrumentKey, std::to_string(pAlgoOrder->algoOrderId).c_str());

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
    pAlgoOrder->algoOrderId = GenerateAlgoOrderId();
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
    // 信号侧 totalCost 只计一次滑点，这里放在主动腿，避免 Fs 中重复计入
    pAlgoOrder->activeMakerSlippage = fs.basicSlippage;
    pAlgoOrder->activeTakerSlippage = fs.basicSlippage;
    pAlgoOrder->passiveMakerSlippage = 0.0;
    pAlgoOrder->passiveTakerSlippage = 0.0;
    pAlgoOrder->takerTakerFs = pAlgoOrder->activeTakerFeeRate + pAlgoOrder->passiveTakerFeeRate
                             + pAlgoOrder->activeTakerSlippage + pAlgoOrder->passiveTakerSlippage;
    pAlgoOrder->makerTakerFs = pAlgoOrder->activeMakerFeeRate + pAlgoOrder->passiveTakerFeeRate
                             + pAlgoOrder->activeMakerSlippage + pAlgoOrder->passiveTakerSlippage;

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
    PairInfoManager::Instance().UpdateOnPosition(position);
    PairInfoManager::Instance().UpdateLiquidStatus(position);
}

void PairTradingContext::OnBalance(const pubsub::Balance& balance) {
    PairInfoManager::Instance().UpdateOnBalance(balance, "baseAsset");
}

void PairTradingContext::OnTotalAccount(const pubsub::TotalAccount& totalAccount) {
    PairInfoManager::Instance().UpdateOnTotalAccount(totalAccount);
}

void PairTradingContext::OnAlgoOrderUpdate(const std::string& pairKey, const std::string& algoOrderId, double volumeFilled, double activePriceFilled, double passivePriceFilled, bool isFinished, bool isFullyFlat) {
    auto& pim = PairInfoManager::Instance();
    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pim.ClearActiveAlgoOrder(pairKey);

    if (isFinished && std::abs(volumeFilled) > 1e-9) {
        pim.UpdateOnAlgoOrderFinished(pairKey, activePriceFilled, volumeFilled, passivePriceFilled);

        if (pi->HasPosition()) {
            if (std::isnan(pi->openSmallSpreadBidBidUQ)) {
                pi->openSmallSpreadBidBidUQ = pi->smallStats.bidBidUQ;
            }

            if (std::isnan(pi->openSmallSpreadAskAskDQ)) {
                pi->openSmallSpreadAskAskDQ = pi->smallStats.askAskDQ;
            }
        }

        // 风控状态更新
        RiskManager::Instance().OnAlgoFinished(*pi, isFullyFlat);

        // 重算 orderParams
        SignalGenerator::Instance().RecalcOrderParams(*pi);
    }
}

void PairTradingContext::OnTimer(int64_t nowUs) {
    auto& pim = PairInfoManager::Instance();
    auto& sg = SignalGenerator::Instance();

    // 1. 重算报单量参数（每分钟）
    if (nowUs - m_lastVolumeRecalcUs > m_cfg.volumeRecalcIntervalSec * 1000000LL) {
        pim.RecalcVolumeParams(m_cfg.maxAmount, m_cfg.targetAmount, m_cfg.exposureMaxLimit, m_cfg.exposureMaxLimitCoff);
        m_lastVolumeRecalcUs = nowUs;
    }

    // 2. 全量重算 orderParams (每5分钟)
    if (nowUs - m_lastSignalRecalcUs > m_cfg.signalRecalcIntervalSec * 1000000LL) {
        for (PairInfo* pi : pim.GetAllPairInfos()) {
            sg.RecalcOrderParams(*pi);
        }
        m_lastSignalRecalcUs = nowUs;
    }

    // 3. 风控检查 (每次定时器触发)
    for (PairInfo* pi : pim.GetAllPairInfos()) {
        ProcessRisk(*pi, nowUs);
    }

    if (nowUs - m_lastCsvSaveUs > m_cfg.csvSaveIntervalSec * 1000000LL) {
        if (!m_cfg.csvStatePath.empty()) {
            pim.SaveToCSV(m_cfg.csvStatePath);
        }
        m_lastCsvSaveUs = nowUs;
    }
}


}