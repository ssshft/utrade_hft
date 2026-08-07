#include "PairInfoManager.h"
#include "BasicInfoMgr.h"
#include "StrategyConfig.h"
#include "Utility.h"


namespace pt {

void PairInfoManager::Init(const std::vector<std::string>& pairKeys, int activeAccountId, int passiveAccountId) {
    m_pairKeys = pairKeys;
    
    for (const auto& pk : m_pairKeys) {
        PairInfo pi;
        pi.SetPairKey(pk.c_str());
        pi.activeAccountId = activeAccountId;
        pi.passiveAccountId = passiveAccountId;
        pi.modifyTime = GetCurrentTimeUs();
        pi.lastTinyCloseOnlyScanTime = GetCurrentTimeUs();

        size_t sep = pk.find("|");
        if (sep == std::string::npos) {
            LOG_WARN("bad key: {}", pk);
            continue;
        }

        std::string activeKey = pk.substr(0, sep);
        std::string passiveKey = pk.substr(sep + 1);
        pi.SetActiveKey(activeKey.c_str());
        pi.SetPassiveKey(passiveKey.c_str());

        auto& bim = BasicInfoMgr::GetInstance();
        stra::InstrumentInfo activeInfo;
        stra::InstrumentInfo passiveInfo;

        if (bim.GetInstrumentInfo(activeKey, activeInfo)) {
            pi.activeParam.multiple = activeInfo.multiple;
            pi.activeParam.minMove = activeInfo.tickSize;
            pi.activeParam.minVolume = activeInfo.minSize;
            pi.activeParam.calcType = activeInfo.calculateType;
        }

        if (bim.GetInstrumentInfo(passiveKey, passiveInfo)) {
            pi.passiveParam.multiple = passiveInfo.multiple;
            pi.passiveParam.minMove = passiveInfo.tickSize;
            pi.passiveParam.minVolume = passiveInfo.minSize;
            pi.passiveParam.calcType = passiveInfo.calculateType;
        }

        m_pairInfoMap[pk] = pi;
        RegisterInstrument(activeKey, pk);
        RegisterInstrument(passiveKey, pk);

    }

    LOG_INFO("");
}


bool PairInfoManager::SaveToCSV(const std::string& csvPath) {
    std::ofstream ofs(csvPath);
    if (!ofs) {
        LOG_ERROR("");
        return false;
    }

    ofs << "pair_instrument_key,pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,pairPassiveTotalVolume,\n";

    for (const auto& kv : m_pairInfoMap) {
        const PairInfo& p = kv.second;
        ofs << p.pairInstrumentKey << ","
          << p.pairTotalVolume << ","
          << p.pairActiveTotalPrice << ","
          << p.pairPassiveTotalPrice << ","
          << p.pairPassiveTotalVolume << ",\n";
    }
    return true;
}

bool PairInfoManager::LoadFromCSV(const std::string& csvPath) {
    std::ifstream ifs(csvPath);
    if (!ifs) {
        LOG_WARN("");
        return false;
    }

    std::string line;
    std::getline(ifs, line);
    int loaded = 0;

    while (std::getline(ifs, line)) {
        std::string pk = ""; // line
        auto it = m_pairInfoMap.find(pk);
        if (it == m_pairInfoMap.end()) {
            continue;
        }

        PairInfo& p = it->second;

        p.pairTotalVolume = 0;
    }
}


PairInfo* PairInfoManager::GetPairInfo(const std::string& pairKey) {
    auto it = m_pairInfoMap.find(pairKey);
    return it != m_pairInfoMap.end() ? &it->second : nullptr;
}

PairInfo* PairInfoManager::GetPairInfo(const char* pairKey) {
    return GetPairInfo(std::string((pairKey)));
}

std::vector<PairInfo*> PairInfoManager::GetAllPairInfos() {
    std::vector<PairInfo*> result;
    result.reserve(m_pairKeys.size());

    for (const auto& pk : m_pairKeys) {
        auto it = m_pairInfoMap.find(pk);
        if (it != m_pairInfoMap.end()) {
            result.push_back(&it->second);
        }
    }

    return result;
}


void PairInfoManager::UpdateRtSpread(const std::string& pairKey, const stra::MdSpread& spread) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    auto& rt = pi->rtSpread;
    rt.spreadBidAsk = spread.spreadBidAsk;
    rt.spreadBidBid = spread.spreadBidBid;
    rt.spreadAskBid = spread.spreadAskBid;
    rt.spreadAskAsk = spread.spreadAskAsk;

    rt.spreadBidAskTema = spread.spreadBidAskTema;
    rt.spreadBidBidTema = spread.spreadBidBidTema;
    rt.spreadAskBidTema = spread.spreadAskBidTema;
    rt.spreadAskAskTema = spread.spreadAskAskTema;

    rt.activePriceTema = spread.activePriceTema;
    rt.passivePriceTema = spread.passivePriceTema;

    rt.lastGenerateTs = spread.generateTs;
    rt.valid = true;

    if (spread.activeFundingTs > rt.activeFundingRateTime) {
        rt.activeFundingRate = spread.activeFundingRate;
        rt.activeFundingRateTime = spread.activeFundingTs;
    }

    if (spread.passiveFundingTs > rt.passiveFundingRateTime) {
        rt.passiveFundingRate = spread.passiveFundingRate;
        rt.passiveFundingRateTime = spread.passiveFundingTs;
    }

    pi->modifyTime = GetCurrentTimeUs();
}


void PairInfoManager::UpdateLargeStats(const std::string& pairKey, const SpreadStats& stats) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->largeStats = stats;
    pi->modifyTime = GetCurrentTimeUs();
}

void PairInfoManager::UpdateSmallStats(const std::string& pairKey, const SpreadStats& stats) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->smallStats = stats;
    pi->modifyTime = GetCurrentTimeUs();
}


void PairInfoManager::UpdateOnPosition(const stra::TdPosition& pos) {
    std::string instrKey = std::string(stra::ExchangeTypeEnum2Str[pos.exchangType]) + "," + std::string(stra::InstTypeEnum2Str[pos.instType]) + "," + std::string(pos.instrument);

    const auto* pairs = FindPairsByInstrument(instrKey);
    if (!pairs) {
        return;
    }

    double volume = 0.0;
    if (pos.direction == stra::Direction_LONG) {
        volume = pos.volume;
    }
    else if (pos.direction == stra::Direction_SHORT) {
        volume = -pos.volume;
    }

    for (const auto& pk : *pairs) {
        auto* pi = GetPairInfo(pk);
        if (!pi) {
            continue;
        }

        if (instrKey == pi->activeInstrumentKey) {
            pi->activeRealPosition = volume;
            pi->activeAvgPrice = pos.avgPrice;
            pi->activeFloatPnl = pos.unrealizedPnl;
            pi->activeLiquidPrice = pos.liquidPrice;
            pi->activeMarkPrice = pos.markPrice;
            pi->activeAdlRank = pos.adlQuantile;
            UpdateLiquidStatus(*pi, true, pos);
        }
        else if (instrKey == pi->passiveInstrumentKey) {
            pi->passiveRealPosition = volume;
            pi->passiveAvgPrice = pos.avgPrice;
            pi->passiveFloatPnl = pos.unrealizedPnl;
            pi->passiveLiquidPrice = pos.liquidPrice;
            pi->passiveMarkPrice = pos.markPrice;
            pi->passiveAdlRank = pos.adlQuantile;
            UpdateLiquidStatus(*pi, true, pos);
        }
        pi->modifyTime = GetCurrentTimeUs();
    }
}


void PairInfoManager::UpdateLiquidStatus(const stra::TdPosition& pos) {
    UpdateOnPosition(pos);
}


// 更新单边的强平风险等级
static void UpdateSideLiquidStatus(PairInfo& pi, bool isActive, double liquidPrice, double markPrice) {
    int& status = isActive ? pi.activeLiquidStatus : pi.passiveLiquidStatus;

    if (liquidPrice > 0 && markPrice > 0) {
        double ratio = std::abs(liquidPrice / markPrice - 1.0);
        if (ratio < 0.3) {
            status = 2; // 危险：强平价距标记价不足30%
        }
        else if (ratio < 0.6) {
            status = 1;  // 警告: 不足60%
        }
        else {
            status = 0;
        }
    }
    else {
        status = 0;
    }
}


void PairInfoManager::UpdateLiquidStatus(PairInfo& pi, bool isActive, const stra::TdPosition& pos) {
    UpdateSideLiquidStatus(pi, isActive, pos.liquidPrice, pos.markPrice);
}

void PairInfoManager::UpdateOnBalance(const stra::TdBalance& balance, const std::string& baseAsset) {
    std::string symbol = std::string(balance.currency) + "-" + baseAsset;

    for (auto& kv : m_pairInfoMap) {
        PairInfo& pi = kv.second;
        if (strstr(pi.activeInstrumentKey, symbol.c_str())) {
            pi.activeRealPosition = balance.total;
            pi.activeFloatPnl = balance.unrealizedPnl;
        }

        if (strstr(pi.passiveInstrumentKey, symbol.c_str())) {
            pi.passiveRealPosition = balance.total;
            pi.passiveFloatPnl = balance.unrealizedPnl;
        }
    }
}

void PairInfoManager::UpdateOnTotalAccount(const stra::TdTotalAccount& totalAccount) {

}

// 算法单同步
void PairInfoManager::UpdateOnAlgoOrderFinished(const std::string& pairKey, double activePriceFilled, double volumeFilled, double passivePriceFilled) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    } 

    double prevAbs = std::abs(pi->pairTotalVolume);
    double newAbs = prevAbs + std::abs(volumeFilled);

    if (std::abs(newAbs) > 1e-9) {
        if (pi->pairActiveTotalPrice < 0) {
            pi->pairActiveTotalPrice = 0;
        }

        if (pi->pairPassiveTotalPrice < 0) {
            pi->pairPassiveTotalPrice = 0;
        }

        pi->pairActiveTotalPrice = (pi->pairActiveTotalPrice * prevAbs + activePriceFilled * std::abs(volumeFilled)) / newAbs;

        pi->pairPassiveTotalPrice = (pi->pairPassiveTotalPrice * prevAbs + passivePriceFilled * std::abs(volumeFilled)) / newAbs;

    }

    pi->pairTotalVolume += volumeFilled;

    pi->positionValue = pi->CalcPositionValue();
    pi->modifyTime = GetCurrentTimeUs();

    ClearActiveAlgoOrder(pairKey);
}

void PairInfoManager::SetActiveAlgoOrder(const std::string& pairKey, const char* algoOrderId) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    strncpy(pi->currentAlgoOrderId, algoOrderId, sizeof(pi->currentAlgoOrderId) - 1);
    pi->hasActiveAlgoOrder = true;
}

void PairInfoManager::ClearActiveAlgoOrder(const std::string& pairKey) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    } 

    memset(pi->currentAlgoOrderId, 0, 128);
    pi->hasActiveAlgoOrder = false;
}

double PairInfoManager::ceil2min(double val, double minUnit) {
    if (minUnit <= 0) {
        return val;
    }

    return std::ceil(val / minUnit) * minUnit; 
}

// 报单量参数计算
void PairInfoManager::RecalcVolumeParams(double maxAmount, double targetAmount, double exposureMaxLimit, double exposureMaxLimitCoff) {
    const double MIN_AMOUNT = MIN_ORDER_USDT;

    for (auto& pk : m_pairKeys) {
        auto it = m_pairInfoMap.find(pk);
        if (it == m_pairInfoMap.end()) {
            continue;
        }

        PairInfo& pi = it->second;
        double ap = pi.activeMeanClose;
        double pp = pi.passiveMeanClose;

        if (std::isnan(ap) || ap <= 0 || std::isnan(pp) || pp <= 0) {
            continue;
        }

        const InstrumentParam& aP = pi.activeParam;
        const InstrumentParam& pP = pi.passiveParam;

        double aMinVol = aP.minVolume;
        double pMinVol = pP.minVolume;


        if (aP.calcType == 0) {
            double a_target = ceil2min(targetAmount / ap / aP.multiple, aMinVol);
            double p_target = ceil2min(targetAmount / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_max = ceil2min(std::min(maxAmount, pi.activeDailyAmount * 0.025) / ap / aP.multiple, aMinVol);
            double p_max = ceil2min(std::min(maxAmount, pi.passiveDailyAmount * 0.025) / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_min = ceil2min(std::min(MIN_AMOUNT, pi.activeDailyAmount * 0.025) / ap / aP.multiple, aMinVol);
            double p_min = ceil2min(std::min(MIN_AMOUNT, pi.passiveDailyAmount * 0.025) / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            pi.ttTargetVolume = std::max(a_target, p_target);
            pi.mtTargetVolume = pi.ttTargetVolume;
            pi.maxVolume = std::min(a_max, p_max);
            pi.minVolume = std::max(a_min, p_min);
            pi.maxExposure = std::max(exposureMaxLimit * exposureMaxLimitCoff, pi.minVolume * ap * aP.multiple);
        }
        else {
            double a_target = ceil2min(targetAmount / aP.multiple, aMinVol);
            double p_target = ceil2min(targetAmount / pP.multiple, pMinVol) * pP.multiple / ap.multiple;

            double a_max = ceil2min(std::min(maxAmount, pi.activeDailyAmount * 0.025) / aP.multiple, aMinVol);
            double p_max = ceil2min(std::min(maxAmount, pi.passiveDailyAmount * 0.025) / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_min = ceil2min(std::min(MIN_AMOUNT, pi.activeDailyAmount * 0.025) / aP.multiple, aMinVol);
            double p_min = ceil2min(std::min(MIN_AMOUNT, pi.passiveDailyAmount * 0.025) / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            pi.ttTargetVolume = std::max(a_target, p_target);
            pi.mtTargetVolume = pi.ttTargetVolume;
            pi.maxVolume = std::min(a_max, p_max);
            pi.minVolume = std::max(a_min, p_min);
            pi.maxExposure = std::max(exposureMaxLimit * exposureMaxLimitCoff, pi.minVolume * aP.multiple);     
        }

        pi.maxVolume = std::max(pi.maxVolume, std::abs(pi.pairTotalVolume));
    }
}


void PairInfoManager::UpdateKlineStats(const std::string& instrKey, double dailyAmount, double meanClose, double oi, double oiUsdt) {
    const auto* pairs = FindPairsByInstrument(instrKey);
    if (!pairs) {
        return;
    }

    for (const auto& pk : *pairs) {
        auto* pi = GetPairInfo(pk);
        if (!pi) {
            return;
        }   

        if (instrKey == pi->activeInstrumentKey) {
            pi->activeDailyAmount = dailyAmount;
            pi->activeMeanClose = meanClose;
            pi->activeOI = oi;
            pi->activeOIUsdt = oiUsdt;
        }
        else {
            pi->passiveDailyAmount = dailyAmount;
            pi->passiveMeanClose = meanClose;
            pi->passiveOI = oi;
            pi->passiveOIUsdt = oiUsdt;    
        }
    }
}


void PairInfoManager::ResetAbnormalCloseState(const std::string& pairKey, AbnormalCloseType type) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }  

    AbnormalCloseState* st = nullptr;
    if (type == AbnormalClose_ADL) {
        st = &pi->adlClose;
    }
    else if (type == AbnormalClose_SPREAD_REGRESSION) {
        st = &pi->spreadNoRegression;
    }
    else if (type == AbnormalClose_FUNDING_ABNORMAL) {
        st = &pi->fundingAbnormal;
    }

    if (!st) {
        *st = AbnormalCloseState();
    }
}



void PairInfoManager::ApplyCommand(const std::string& pairKey, PairCommandType cmd, const std::unordered_map<std::string, double>& params) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    switch (cmd) {
        case PairCmd_STOP:
            pi->stopFlag = true;
            pi->autoFlag = false;
            break;
        case PairCmd_CLOSE:
            pi->closeFlag = true;
            break;
        case PairCmd_RESUME:
            pi->stopFlag = false;
            pi->closeFlag = false;
            pi->autoFlag = true;
            break;
        case PairCmd_MODIFY:
            for (const auto& kv : params) {
                if (kv.first == "profit") {
                    pi->profitPct = kv.second;
                }
                if (kv.first == "maxVolume") {
                    pi->maxVolume = kv.second;
                }
                if (kv.first == "ttTargetVolume") {
                    pi->ttTargetVolume = kv.second;
                }
                if (kv.first == "mtTargetVolume") {
                    pi->mtTargetVolume = kv.second;
                }
            }
            break;
        default:
            break;
    }

    pi->modifyTime = GetCurrentTimeUs();
}

void PairInfoManager::RegisterInstrument(const std::string& instrKey, const std::string& pairKey) {
    m_instrToPairs[instrKey].push_back(pairKey);
}

const std::vector<std::string>* PairInfoManager::FindPairsByInstrument(const std::string& instrKey) const {
    auto it = m_instrToPairs.find(instrKey);
    return it != m_instrToPairs.end() ? &it->second : nullptr;
}



}