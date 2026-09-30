#include "PairInfoManager.h"
#include "StrategyConfig.h"
#include "Utility.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sys/stat.h>

namespace pt {

void PairInfoManager::Init(const std::vector<std::string>& pairKeys, int activeAccountId, int passiveAccountId, sm::SecurityManager* s) {
    m_pairKeys = pairKeys;
    
    for (const auto& pk : m_pairKeys) {
        PairInfo pi;
        pi.SetPairKey(pk.c_str());
        pi.activeAccountId = activeAccountId;
        pi.passiveAccountId = passiveAccountId;
        pi.modifyTime = crypto::getCurrentTime();
        pi.lastTinyCloseOnlyScanTime = crypto::getCurrentTime();

        size_t sep = pk.find("|");
        if (sep == std::string::npos) {
            LOG_WARN("bad key: {}", pk);
            continue;
        }

        std::string activeKey = pk.substr(0, sep);
        std::string passiveKey = pk.substr(sep + 1);
        pi.SetActiveKey(activeKey.c_str());
        pi.SetPassiveKey(passiveKey.c_str());

        smc = s;

        vector<string> vActive;
        splitString(activeKey, vActive, ".");

        vector<string> vPassive;
        splitString(passiveKey, vPassive, ".");

        md::InstrumentInfo activeInfo;
        md::InstrumentInfo passiveInfo;

        if (smc->get_instrument_info(ExchangeTypeStr2EnumMap[vActive[0]], InstTypeStr2EnumMap[vActive[1]], vActive[2].c_str(), activeInfo)) {
            pi.activeParam.multiple = activeInfo.value;
            pi.activeParam.minMove = activeInfo.tickSize;
            pi.activeParam.minVolume = activeInfo.minSize;
            pi.activeParam.calcType = activeInfo.calcType;
        }

        if (smc->get_instrument_info(ExchangeTypeStr2EnumMap[vPassive[0]], InstTypeStr2EnumMap[vPassive[1]], vPassive[2].c_str(), passiveInfo)) {
            pi.passiveParam.multiple = passiveInfo.value;
            pi.passiveParam.minMove = passiveInfo.tickSize;
            pi.passiveParam.minVolume = passiveInfo.minSize;
            pi.passiveParam.calcType = passiveInfo.calcType;
        }

        m_pairInfoMap[pk] = pi;
        RegisterInstrument(activeKey, pk);
        RegisterInstrument(passiveKey, pk);

    }

    LOG_INFO("PairInfoManager::Init 完成: 配置对子数:{} 已注册对子数:{}", m_pairKeys.size(), m_pairInfoMap.size());
}


// ---------------------------------------------------------------------------
// 快照：只存"无法自己恢复"的字段（docs/restart_recovery_design.md §5.1.1）
//
// 账户真值（持仓 / 均价 / 浮盈 / 强平价 / 标记价 / ADL / 强平状态）一律不存 ——
// 由 pubsub 推送自愈。派生字段（ttTargetVolume / orderParams / *Stats）也不存。
//
// 列布局（33 列）。字符串列放最后，避免空字段 / 逗号带来的歧义：
//   1   pairInstrumentKey                      行键
//   3   持仓账本   pairTotalVolume / pairActiveTotalPrice / pairPassiveTotalPrice
//   2   建仓基准   openSmallSpreadBidBidUQ / openSmallSpreadAskAskDQ
//   18  风控档位   adlClose / spreadNoRegression / fundingAbnormal 各 6 个字段
//   2   风控计时   positionExceedThresholdStartTime / spreadNoRegressionStartTime
//   4   运维意图   autoFlag / stopFlag / closeFlag / profitPct
//   1   错误态     errorFlag
//   2   残留标记   hasActiveAlgoOrder / currentAlgoOrderId
// ---------------------------------------------------------------------------
namespace {

constexpr size_t kSnapshotColumns = 33;

constexpr const char* kSnapshotHeader =
    "pairInstrumentKey,"
    "pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,"
    "openSmallSpreadBidBidUQ,openSmallSpreadAskAskDQ,"
    "adlClose.triggered,adlClose.currentTier,adlClose.startTime,"
    "adlClose.tier1Times,adlClose.tier2Times,adlClose.tier3Times,"
    "spreadNoRegression.triggered,spreadNoRegression.currentTier,spreadNoRegression.startTime,"
    "spreadNoRegression.tier1Times,spreadNoRegression.tier2Times,spreadNoRegression.tier3Times,"
    "fundingAbnormal.triggered,fundingAbnormal.currentTier,fundingAbnormal.startTime,"
    "fundingAbnormal.tier1Times,fundingAbnormal.tier2Times,fundingAbnormal.tier3Times,"
    "positionExceedThresholdStartTime,spreadNoRegressionStartTime,"
    "autoFlag,stopFlag,closeFlag,profitPct,"
    "errorFlag,"
    "hasActiveAlgoOrder,"
    "currentAlgoOrderId";

// 逐级建父目录（POSIX）。已存在时 mkdir 返回 EEXIST，忽略即可；
// 失败不致命 —— 真打不开文件时 SaveSnapshot 会自己 LOG_ERROR 并返回 false。
void EnsureParentDir(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos)) != std::string::npos) {
        if (pos > 0) {
            ::mkdir(path.substr(0, pos).c_str(), 0755);
        }
        ++pos;
    }
}

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
}

// 解析辅助：坏值一律回退到 fallback 并告警 —— 不让一条坏行毁掉整次恢复
double ParseDouble(const std::string& s, double fallback, const char* field, const std::string& pairKey) {
    if (s.empty()) {
        return fallback;
    }
    try {
        // stod 走 strtod，能接受 "nan" / "inf" —— 这是 §5.1.1 要求的往返能力
        return std::stod(s);
    } catch (const std::exception&) {
        LOG_WARN("LoadSnapshot: pairKey:{} field:{} bad double '{}' -> use {}", pairKey, field, s, fallback);
        return fallback;
    }
}

int64_t ParseI64(const std::string& s, int64_t fallback, const char* field, const std::string& pairKey) {
    if (s.empty()) {
        return fallback;
    }
    try {
        return static_cast<int64_t>(std::stoll(s));
    } catch (const std::exception&) {
        LOG_WARN("LoadSnapshot: pairKey:{} field:{} bad int '{}' -> use {}", pairKey, field, s, fallback);
        return fallback;
    }
}

bool ParseBool(const std::string& s, bool fallback) {
    if (s.empty()) {
        return fallback;
    }
    return s != "0" && s != "false";
}

void WriteAbnormalState(std::ostream& os, const AbnormalCloseState& st) {
    os << (st.triggered ? 1 : 0) << ","
       << st.currentTier << ","
       << st.startTime << ","
       << st.tier1Times << ","
       << st.tier2Times << ","
       << st.tier3Times << ",";
}

// f[off .. off+5] 对应一个 AbnormalCloseState（列序见 kSnapshotHeader）
void ReadAbnormalState(const std::vector<std::string>& f, size_t off, AbnormalCloseState& st,
                       const std::string& pairKey, const char* name) {
    st.triggered   = ParseBool(f[off + 0], st.triggered);
    st.currentTier = static_cast<int>(ParseI64(f[off + 1], st.currentTier, name, pairKey));
    st.startTime   = ParseI64(f[off + 2], st.startTime, name, pairKey);
    st.tier1Times  = static_cast<int>(ParseI64(f[off + 3], st.tier1Times, name, pairKey));
    st.tier2Times  = static_cast<int>(ParseI64(f[off + 4], st.tier2Times, name, pairKey));
    st.tier3Times  = static_cast<int>(ParseI64(f[off + 5], st.tier3Times, name, pairKey));
}

} // namespace


bool PairInfoManager::SaveSnapshot(const std::string& path) {
    EnsureParentDir(path);

    // 写临时文件 + rename 原子替换：崩在写一半时旧快照仍然完好
    const std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::trunc);
    if (!ofs) {
        LOG_ERROR("SaveSnapshot: open failed: {}", tmpPath);
        return false;
    }

    // 17 位有效数字 —— double 无损往返（openSmallSpread* 是 NaN 时打印 "nan"）
    ofs << std::setprecision(17);
    ofs << kSnapshotHeader << "\n";

    for (const auto& pk : m_pairKeys) {
        auto it = m_pairInfoMap.find(pk);
        if (it == m_pairInfoMap.end()) {
            continue;
        }
        const PairInfo& p = it->second;

        ofs << p.pairInstrumentKey << ","
            << p.pairTotalVolume << ","
            << p.pairActiveTotalPrice << ","
            << p.pairPassiveTotalPrice << ","
            << p.openSmallSpreadBidBidUQ << ","
            << p.openSmallSpreadAskAskDQ << ",";

        WriteAbnormalState(ofs, p.adlClose);
        WriteAbnormalState(ofs, p.spreadNoRegression);
        WriteAbnormalState(ofs, p.fundingAbnormal);

        ofs << p.positionExceedThresholdStartTime << ","
            << p.spreadNoRegressionStartTime << ","
            << (p.autoFlag ? 1 : 0) << ","
            << (p.stopFlag ? 1 : 0) << ","
            << (p.closeFlag ? 1 : 0) << ","
            << p.profitPct << ","
            << (p.errorFlag ? 1 : 0) << ","
            << (p.hasActiveAlgoOrder ? 1 : 0) << ","
            << p.currentAlgoOrderId << "\n";
    }

    ofs.flush();
    const bool writeOk = static_cast<bool>(ofs);
    ofs.close();

    if (!writeOk) {
        LOG_ERROR("SaveSnapshot: write failed: {}", tmpPath);
        return false;
    }

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        LOG_ERROR("SaveSnapshot: rename {} -> {} failed: {}", tmpPath, path, std::strerror(errno));
        return false;
    }

    return true;
}


int PairInfoManager::LoadSnapshot(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        LOG_WARN("LoadSnapshot: no snapshot at {} (首次启动属正常)", path);
        return -1;
    }

    std::string line;
    if (!std::getline(ifs, line)) {
        LOG_WARN("LoadSnapshot: empty snapshot: {}", path);
        return -1;
    }

    // 先验表头列数，挡住不兼容的旧格式（例如老的 5 列 CSV）
    {
        const auto header = SplitCsv(line);
        if (header.size() != kSnapshotColumns) {
            LOG_ERROR("LoadSnapshot: header has {} columns, expected {} -> 忽略该快照（格式不兼容）",
                      header.size(), kSnapshotColumns);
            return -1;
        }
    }

    int restored = 0;
    int skipped  = 0;
    int lineno   = 1;

    while (std::getline(ifs, line)) {
        ++lineno;
        if (line.empty()) {
            continue;
        }

        const auto f = SplitCsv(line);
        if (f.size() != kSnapshotColumns) {
            LOG_WARN("LoadSnapshot: line:{} has {} columns, expected {} -> skip",
                     lineno, f.size(), kSnapshotColumns);
            ++skipped;
            continue;
        }

        const std::string& pk = f[0];
        auto it = m_pairInfoMap.find(pk);
        if (it == m_pairInfoMap.end()) {
            LOG_WARN("LoadSnapshot: line:{} pairKey:{} 不在本次配置里 -> skip", lineno, pk);
            ++skipped;
            continue;
        }

        PairInfo& p = it->second;

        // ---- 持仓账本（策略自己的记账，交易所反推不出来）----
        p.pairTotalVolume       = ParseDouble(f[1], 0.0,  "pairTotalVolume", pk);
        p.pairActiveTotalPrice  = ParseDouble(f[2], -1.0, "pairActiveTotalPrice", pk);
        p.pairPassiveTotalPrice = ParseDouble(f[3], -1.0, "pairPassiveTotalPrice", pk);

        // ---- 建仓基准（NaN 必须往返，isnan 是"尚未快照"的哨兵）----
        p.openSmallSpreadBidBidUQ = ParseDouble(f[4], p.openSmallSpreadBidBidUQ, "openSmallSpreadBidBidUQ", pk);
        p.openSmallSpreadAskAskDQ = ParseDouble(f[5], p.openSmallSpreadAskAskDQ, "openSmallSpreadAskAskDQ", pk);

        // ---- 风控档位（跨轮次累积，丢了等于重置风控耐心）----
        ReadAbnormalState(f, 6,  p.adlClose,           pk, "adlClose");
        ReadAbnormalState(f, 12, p.spreadNoRegression, pk, "spreadNoRegression");
        ReadAbnormalState(f, 18, p.fundingAbnormal,    pk, "fundingAbnormal");

        // ---- 风控计时起点 ----
        p.positionExceedThresholdStartTime = ParseI64(f[24], 0, "positionExceedThresholdStartTime", pk);
        p.spreadNoRegressionStartTime      = ParseI64(f[25], 0, "spreadNoRegressionStartTime", pk);

        // ---- 运维意图（ApplyCommand 写的，没有自愈路径）----
        p.autoFlag  = ParseBool(f[26], p.autoFlag);
        p.stopFlag  = ParseBool(f[27], p.stopFlag);
        p.closeFlag = ParseBool(f[28], p.closeFlag);
        p.profitPct = ParseDouble(f[29], p.profitPct, "profitPct", pk);

        // ---- 错误态（判死的对子不能静默复活；复活走 PairCmd_RESUME）----
        p.errorFlag = ParseBool(f[30], p.errorFlag);

        // ---- 上一轮残留标记（只用于启动告警，见 §5.4）----
        p.hasActiveAlgoOrder = ParseBool(f[31], false);
        p.currentAlgoOrderId = ParseI64(f[32], 0);
        p.modifyTime = crypto::getCurrentTime();

        ++restored;
    }

    LOG_INFO("LoadSnapshot: restored:{} skipped:{} from {}", restored, skipped, path);
    return restored;
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


void PairInfoManager::UpdateRtSpread(const std::string& pairKey, const dbp::DbpData* pdata) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    auto& rt = pi->rtSpread;
    rt.spreadBidAsk = pdata->spreadBidAsk;
    rt.spreadBidBid = pdata->spreadBidBid;
    rt.spreadAskBid = pdata->spreadAskBid;
    rt.spreadAskAsk = pdata->spreadAskAsk;

    rt.spreadBidAskTema = pdata->spreadBidAskTema;
    rt.spreadBidBidTema = pdata->spreadBidBidTema;
    rt.spreadAskBidTema = pdata->spreadAskBidTema;
    rt.spreadAskAskTema = pdata->spreadAskAskTema;

    rt.activePriceTema = pdata->activePriceTema;
    rt.passivePriceTema = pdata->passivePriceTema;

    rt.lastGenerateTs = pdata->generateTs;
    rt.valid = true;

    if (pdata->activeFundingTs > rt.activeFundingRateTime) {
        rt.activeFundingRate = pdata->activeFundingRate;
        rt.activeFundingRateTime = pdata->activeFundingTs;
    }

    if (pdata->passiveFundingTs > rt.passiveFundingRateTime) {
        rt.passiveFundingRate = pdata->passiveFundingRate;
        rt.passiveFundingRateTime = pdata->passiveFundingTs;
    }

    pi->modifyTime = crypto::getCurrentTime();
}


void PairInfoManager::UpdateLargeStats(const std::string& pairKey, const SpreadStats& stats) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->largeStats = stats;
    pi->modifyTime = crypto::getCurrentTime();
}

void PairInfoManager::UpdateSmallStats(const std::string& pairKey, const SpreadStats& stats) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->smallStats = stats;
    pi->modifyTime = crypto::getCurrentTime();
}


void PairInfoManager::UpdateOnPosition(const pubsub::Position& pos) {
    std::string instrKey = ExchangeTypeEnum2StrMap[pos.exchangeTypeEnum] + "." + InstTypeEnum2StrMap[pos.instTypeEnum] + "." + std::string(pos.instId);

    const auto* pairs = FindPairsByInstrument(instrKey);
    if (!pairs) {
        return;
    }

    double volume = 0.0;
    if (pos.direction == DT_LONG) {
        volume = pos.volume;
    }
    else if (pos.direction == DT_SHORT) {
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
            pi->activePushArrived = true;   // 启动对账的"推送到位"判据
            UpdateLiquidStatus(*pi, true, pos);
        }
        else if (instrKey == pi->passiveInstrumentKey) {
            pi->passiveRealPosition = volume;
            pi->passiveAvgPrice = pos.avgPrice;
            pi->passiveFloatPnl = pos.unrealizedPnl;
            pi->passiveLiquidPrice = pos.liquidPrice;
            pi->passiveMarkPrice = pos.markPrice;
            pi->passiveAdlRank = pos.adlQuantile;
            pi->passivePushArrived = true;  // 启动对账的"推送到位"判据
            UpdateLiquidStatus(*pi, true, pos);
        }
        pi->modifyTime = crypto::getCurrentTime();
    }
}


void PairInfoManager::UpdateLiquidStatus(const pubsub::Position& pos) {
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


void PairInfoManager::UpdateLiquidStatus(PairInfo& pi, bool isActive, const pubsub::Position& pos) {
    UpdateSideLiquidStatus(pi, isActive, pos.liquidPrice, pos.markPrice);
}

void PairInfoManager::UpdateOnBalance(const pubsub::Balance& balance, const std::string& baseAsset) {
    std::string symbol = std::string(balance.currency) + "-" + baseAsset;
    std::string symKey = fmt::format("{}.{}.{}", ExchangeTypeEnum2StrMap[balance.exchangeTypeEnum], InstTypeEnum2StrMap[balance.instTypeEnum], symbol);

    for (auto& kv : m_pairInfoMap) {
        PairInfo& pi = kv.second;
        if (strstr(pi.activeInstrumentKey, symKey.c_str())) {
            pi.activeRealPosition = balance.total;
            pi.activeFloatPnl = balance.unrealizedPnl;
        }

        if (strstr(pi.passiveInstrumentKey, symKey.c_str())) {
            pi.passiveRealPosition = balance.total;
            pi.passiveFloatPnl = balance.unrealizedPnl;
        }
    }
}

void PairInfoManager::UpdateOnTotalAccount(const pubsub::TotalAccount& totalAccount) {

}

// 算法单同步：执行端是唯一数据源，量 / 价直接覆盖
void PairInfoManager::UpdateOnAlgoOrder(const std::string& pairKey, double volume, double activePrice, double passivePrice) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->pairTotalVolume = volume;
    pi->pairActiveTotalPrice = activePrice;
    pi->pairPassiveTotalPrice = passivePrice;

    pi->positionValue = pi->CalcPositionValue();
    pi->modifyTime = crypto::getCurrentTime();
}

void PairInfoManager::SetActiveAlgoOrder(const std::string& pairKey, int4_t algoOrderId) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    }

    pi->currentAlgoOrderId = algoOrderId;
    pi->hasActiveAlgoOrder = true;
}

void PairInfoManager::ClearActiveAlgoOrder(const std::string& pairKey) {
    auto* pi = GetPairInfo(pairKey);
    if (!pi) {
        return;
    } 

    pi->currentAlgoOrderId = 0;
    pi->hasActiveAlgoOrder = false;
    // 算法单已终结，风控强平单的"在途"标记随之失效（ProcessRisk 会重新判断）
    pi->riskCloseOrderInFlight = false;
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

        // 腿价：优先用 K 线统计的日均收盘价。K 线统计尚未接入
        // （UpdateKlineStats 全仓库无调用者）时退回实时腿价，
        // 否则这里恒 continue -> ttTargetVolume/maxVolume 恒 0 -> 算法单永远建不出来
        double ap = pi.activeMeanClose;
        double pp = pi.passiveMeanClose;
        if (std::isnan(ap) || ap <= 0) {
            ap = pi.rtSpread.activePriceTema;
        }
        if (std::isnan(pp) || pp <= 0) {
            pp = pi.rtSpread.passivePriceTema;
        }

        if (std::isnan(ap) || ap <= 0 || std::isnan(pp) || pp <= 0) {
            LOG_WARN("skip volume calc, invalid leg price. pairKey:{} ap:{} pp:{}", pk, ap, pp);
            continue;
        }

        // 日成交量上限：缺失时（<=0）不施加「不超过日成交量 2.5%」的约束。
        // 否则 min(maxAmount, 0) = 0 -> a_max = 0 -> maxVolume = 0
        auto capByDailyAmount = [](double amount, double dailyAmount) {
            return (dailyAmount > 0.0) ? std::min(amount, dailyAmount * 0.025) : amount;
        };

        const InstrumentParam& aP = pi.activeParam;
        const InstrumentParam& pP = pi.passiveParam;

        double aMinVol = aP.minVolume;
        double pMinVol = pP.minVolume;


        if (aP.calcType == 0) {
            double a_target = ceil2min(targetAmount / ap / aP.multiple, aMinVol);
            double p_target = ceil2min(targetAmount / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_max = ceil2min(capByDailyAmount(maxAmount, pi.activeDailyAmount) / ap / aP.multiple, aMinVol);
            double p_max = ceil2min(capByDailyAmount(maxAmount, pi.passiveDailyAmount) / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_min = ceil2min(capByDailyAmount(MIN_AMOUNT, pi.activeDailyAmount) / ap / aP.multiple, aMinVol);
            double p_min = ceil2min(capByDailyAmount(MIN_AMOUNT, pi.passiveDailyAmount) / pp / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            pi.ttTargetVolume = std::max(a_target, p_target);
            pi.mtTargetVolume = pi.ttTargetVolume;
            pi.maxVolume = std::min(a_max, p_max);
            pi.minVolume = std::max(a_min, p_min);
            pi.maxExposure = std::max(exposureMaxLimit * exposureMaxLimitCoff, pi.minVolume * ap * aP.multiple);
        }
        else {
            double a_target = ceil2min(targetAmount / aP.multiple, aMinVol);
            double p_target = ceil2min(targetAmount / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_max = ceil2min(capByDailyAmount(maxAmount, pi.activeDailyAmount) / aP.multiple, aMinVol);
            double p_max = ceil2min(capByDailyAmount(maxAmount, pi.passiveDailyAmount) / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

            double a_min = ceil2min(capByDailyAmount(MIN_AMOUNT, pi.activeDailyAmount) / aP.multiple, aMinVol);
            double p_min = ceil2min(capByDailyAmount(MIN_AMOUNT, pi.passiveDailyAmount) / pP.multiple, pMinVol) * pP.multiple / aP.multiple;

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

    if (st) {
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
            // 错误态也在这里复活：errorFlag 的唯一写入方是 CheckExposureAbnormal，
            // 语义是"该对子判死、停自动、留人工处理"。RESUME = "恢复自动"，
            // 正是它的对偶操作，否则置位后除了改快照/重启没有第二条路。
            // 风险自限：敞口若没真正解决，下一轮 CheckExposureAbnormal 会立刻再次置位。
            if (pi->errorFlag) {
                LOG_WARN("ApplyCommand RESUME: pairKey:{} clear errorFlag (was set by CheckExposureAbnormal)", pairKey);
                pi->errorFlag = false;
            }
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

    pi->modifyTime = crypto::getCurrentTime();
}

void PairInfoManager::RegisterInstrument(const std::string& instrKey, const std::string& pairKey) {
    m_instrToPairs[instrKey].push_back(pairKey);
}

const std::vector<std::string>* PairInfoManager::FindPairsByInstrument(const std::string& instrKey) const {
    auto it = m_instrToPairs.find(instrKey);
    return it != m_instrToPairs.end() ? &it->second : nullptr;
}



}