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


namespace pt {

int64_t PairTradingContext::NowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

std::string PairTradingContext::GenerateAlgoOrderId() {
    static std::atomic<int64_t> seq{0};
    int64_t id = NowUs() * 1000 + (seq.fetch_add(1) % 1000);
    return "PT_" + std::to_string(id);
}

PairTradingContext::PairTradingContext() = default;

void PairTradingContext::Init(const PairTradingConfig& cfg) {
    m_cfg = cfg;

    auto& pim = PairInfoManager::Instance();
    pim.Init(cfg.pairKeys, cfg.activeAccountId, cfg.passiveAccountId);

    if (!cfg.csvStatePath.empty()) {
        if (pim.LoadFromCSV(cfg.csvStatePath)) {
            LOG_WARN("");
        }
    }

    LOG_INFO("size: {}", cfg.pairKeys.size());
}

void PairTradingContext::OnSpread(const stra::MdSpread& spread, int64_t eventTimeUs) {
    std::string pairKey(spread.pairInstrumentKey);

    auto& pim = PairInfoManager::Instance();
    PairInfo* pi = pim.GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pim.UpdateRtSpread(pairKey, spread);

    ProcessPairSignal(*pi, eventTimeUs);
}

void PairTradingContext::ProcessPairSignal(PairInfo& pi, int64_t nowUs) {
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

    std::string json = BuildAlgoOrderJson(pi, algoMode, direction, forgoProfit);
    if (json.empty()) {
        return;
    }

    auto algoId = GenerateAlgoOrderId();
    auto& pim = PairInfoManager::Instance();
    pim.SetActiveAlgoOrder(pi.pairInstrumentKey, algoId.c_str());

    m_algoCommandCb(json);
}

std::string PairTradingContext::BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const {
    const auto& op = pi.orderParams;
    bool isTT = (algoMode == "TT");

    double startSpread = 0.0;
    double endSpread = 0.0;
    double startVolume = 0.0;
    double endVolume = 0.0;
    bool sw = false;

    if (algoMode == "TT" && direction == "OL") {
        startSpread = op.ttOLStartSpread;
        endSpread = op.ttOLEndSpread;
        startVolume = op.ttOLStartVolume;
        endVolume = op.ttOLEndVolume;
        sw = op.ttOLSwitch;
    }
    else if (algoMode == "TT" && direction == "OS") {
        startSpread = op.ttOSStartSpread;
        endSpread = op.ttOSEndSpread;
        startVolume = op.ttOSStartVolume;
        endVolume = op.ttOSEndVolume;
        sw = op.ttOSSwitch;
    }
    else if (algoMode == "TT" && direction == "CL") {
        startSpread = op.ttCLStartSpread;
        endSpread = op.ttCLEndSpread;
        startVolume = op.ttCLStartVolume;
        endVolume = op.ttCLEndVolume;
        sw = op.ttCLSwitch;
    }
    else if (algoMode == "TT" && direction == "CS") {
        startSpread = op.ttCSStartSpread;
        endSpread = op.ttCSEndSpread;
        startVolume = op.ttCSStartVolume;
        endVolume = op.ttCSEndVolume;
        sw = op.ttCSSwitch;
    }
    else if (algoMode == "MT" && direction == "OL") {
        startSpread = op.mtOLStartSpread;
        endSpread = op.mtOLEndSpread;
        startVolume = op.mtOLStartVolume;
        endVolume = op.mtOLEndVolume;
        sw = op.mtOLSwitch;
    }
    else if (algoMode == "MT" && direction == "OS") {
        startSpread = op.mtOSStartSpread;
        endSpread = op.mtOSEndSpread;
        startVolume = op.mtOSStartVolume;
        endVolume = op.mtOSEndVolume;
        sw = op.mtOSSwitch;
    }
    else if (algoMode == "MT" && direction == "CL") {
        startSpread = op.mtCLStartSpread;
        endSpread = op.mtCLEndSpread;
        startVolume = op.mtCLStartVolume;
        endVolume = op.mtCLEndVolume;
        sw = op.mtCLSwitch;
    }
    else if (algoMode == "MT" && direction == "CS") {
        startSpread = op.mtCSStartSpread;
        endSpread = op.mtCSEndSpread;
        startVolume = op.mtCSStartVolume;
        endVolume = op.mtCSEndVolume;
        sw = op.mtCSSwitch;
    }
    else {
        return "";
    }

    bool isClose = (direction == "CL" || direction == "CS");
    if (!sw && !isClose && forgoProfit == 0.0) {
        return "";
    }

    if (isClose && forgoProfit > 0.0) {
        if (direction == "CL") {
            startSpread -= forgoProfit;
            endSpread -= forgoProfit;
        }
        else {
            startSpread += forgoProfit;
            endSpread += forgoProfit;     
        }
    }

    std::string activeOrderType = isTT ? "'OrderType_MARKET" : "OrderType_LIMIT";
    std::string passiveOrderType = "OrderType_MARKET";

    double targetVolume = isTT ? pi.ttTargetVolume : pi.mtTargetVolume;
    if (std::isnan(targetVolume) || targetVolume <= 0.0) {
        return "";
    }

    std::string algoOrderId = GenerateAlgoOrderId();
    std::ostringstream oss;
    oss << "{\"commandType\"" << "}";
    return oss.str();
}

void PairTradingContext::OnPosition(const stra::TdPosition& position) {
    PairInfoManager::Instance().UpdateOnPosition(position);
    PairInfoManager::Instance().UpdateLiquidStatus(position);
}

void PairTradingContext::OnBalance(const stra::TdBalance& balance) {
    PairInfoManager::Instance().UpdateOnBalance(balance, "baseAsset");
}

void PairTradingContext::OnTotalAccount(const stra::TdTotalAccount& totalAccount) {
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