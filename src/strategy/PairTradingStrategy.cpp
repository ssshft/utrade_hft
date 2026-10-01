#include "strategy/PairTradingStrategy.h"
#include "basic/DataStruct.h"
#include "basic/PairInfoManager.h"
#include "basic/WriteFileContent.h"
#include "basic/SpreadManager.h"
#include "log_engine.h"
#include <chrono>


using namespace std::chrono;


PairTradingStrategy::PairTradingStrategy() {
    algoContext.SetTradeClient(tradeClient);
};

PairTradingStrategy::~PairTradingStrategy() {

};

void PairTradingStrategy::pre_start(Config* config) {
    lastOnCommand = crypto::getCurrentTime();

    _init(config);

    std::string cfgStr = config->get_document_str();
    rapidjson::Document d;
    rapidjson::Value& v = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(cfgStr.c_str());

    auto& op = v["op"];

    m_ptCfg.activeAccountId = std::stoi(op["activeAccountId"].GetString());
    m_ptCfg.passiveAccountId = std::stoi(op["passiveAccountId"].GetString());

    if (op.HasMember("pairKeys")) {
        for (auto& pk: op["pairKeys"].GetArray()) {
            std::string pairKey = pk.GetString();
            m_ptCfg.pairKeys.emplace_back(pairKey);

            LOG_INFO("pre_start Subscribe pairInstrumentKey:{}", pairKey);
            SpreadManager::Instance().AddSpreadPara(pairKey);
            dbpreader->Subscribe(pairKey);
        }
    }

    if (op.HasMember("maxPositionValue")) {
        m_ptCfg.maxPositionValue = std::stod(op["maxPositionValue"].GetString());
    }
    if (op.HasMember("maxAmount")) {
        m_ptCfg.maxAmount = std::stod(op["maxAmount"].GetString());
    }
    if (op.HasMember("targetAmount")) {
        m_ptCfg.targetAmount = std::stod(op["targetAmount"].GetString());
    }

    if (op.HasMember("csvStatePath")) {
        m_ptCfg.csvStatePath = op["csvStatePath"].GetString();
    }

    // =======================================================================
    // 可调参数：op 段下的三个子对象（都可以不写）
    //
    // 全部在这里逐字段读 —— 每个 key 先 HasMember 再赋值，所以**配置里没写的
    // key 保持结构体默认值**（只覆盖、不清零），可以只写想调的那几个。
    // 值解析不出来（比如 "abc"）会让 std::stod/stoll 抛异常、启动直接失败 ——
    // 这是有意的：配置写错就该在启动时炸，别带着一个"看起来对"的值跑起来。
    //
    //   op.feeSlippage -> m_ptCfg.feeSlippage  推给 SignalGenerator::SetConfig
    //   op.risk        -> m_ptCfg.risk         推给 RiskManager::SetConfig
    //   op.pairTrading -> m_ptCfg 本身         含算法单参数，BuildAlgoOrderJson 直接读
    //
    // 这两个 SetConfig 在改造前是**死接口**（全仓库零个生产调用点），
    // 所有阈值/费率/门槛只能改头文件重新编译。现在改 etc/config.json 即可。
    //
    // 取值一律 `Value.GetString()` 再交给 std::stod / std::stoll / std::stoi：
    // config.json 是用 `Parse<kParseNumbersAsStringsFlag>` 解析的，数值写
    // `"1000"` 或 `1000` 读出来都是字符串。布尔字段没有对应的 std 转换函数，
    // 直接和 "true" 比字符串。
    // ⚠️ 所以布尔字段在 config.json 里**必须写成带引号的 "true"/"false"**：
    //    写成裸 true 是原生 bool，`GetString()` 会触发 rapidjson 的
    //    RAPIDJSON_ASSERT(IsString()) —— 那是断言失败不是异常，进程直接挂。
    // =======================================================================

    // ---- 1) op.feeSlippage -> m_ptCfg.feeSlippage，再推给 SignalGenerator ----
    // 执行成本。这五个费率必须与算法单的 takerTakerFs / makerTakerFs 同源：
    // CalcExecCost 用它们算 F，BuildAlgoOrderJson 又把同一个 F 写进算法单。
    // 改这里的值会同时移动信号阈值与算法单阶梯，是**一次改两边**的参数。
    if (op.HasMember("feeSlippage")) {
        auto& s = op["feeSlippage"];
        auto& c = m_ptCfg.feeSlippage;

        if (s.HasMember("activeMakerFeeRate"))  {
            c.activeMakerFeeRate  = std::stod(s["activeMakerFeeRate"].GetString());
        }

        if (s.HasMember("activeTakerFeeRate"))  {
            c.activeTakerFeeRate  = std::stod(s["activeTakerFeeRate"].GetString());
        }

        if (s.HasMember("passiveMakerFeeRate")) {
            c.passiveMakerFeeRate = std::stod(s["passiveMakerFeeRate"].GetString());
        }   

        if (s.HasMember("passiveTakerFeeRate")) {
            c.passiveTakerFeeRate = std::stod(s["passiveTakerFeeRate"].GetString());
        }

        if (s.HasMember("basicSlippage")) {
            c.basicSlippage = std::stod(s["basicSlippage"].GetString());
        } 

        if (s.HasMember("slippagePctMinMove")) {
            c.slippagePctMinMove  = std::stod(s["slippagePctMinMove"].GetString());
        }

        if (s.HasMember("ttAddPercent")) {
            c.ttAddPercent = std::stod(s["ttAddPercent"].GetString());
        }

        if (s.HasMember("spreadAdjPct")) {
            c.spreadAdjPct = std::stod(s["spreadAdjPct"].GetString());
        } 

        if (s.HasMember("minSpreadSpan")) {
            c.minSpreadSpan = std::stod(s["minSpreadSpan"].GetString());
        }   

        if (s.HasMember("minSpreadTarget")) {
            c.minSpreadTarget = std::stod(s["minSpreadTarget"].GetString());
        } 

        if (s.HasMember("openProfitPct")) {
            c.openProfitPct = std::stod(s["openProfitPct"].GetString());
        } 

        if (s.HasMember("openMaxFundingRate"))  {
            c.openMaxFundingRate = std::stod(s["openMaxFundingRate"].GetString());
        }

        pt::SignalGenerator::Instance().SetConfig(c);
    }

    // ---- 2) op.risk -> m_ptCfg.risk，再推给 RiskManager ----
    // 时间字段在配置里写**秒**、结构体里是微秒，所以这里 ×1000000。
    // 写微秒的话 4 天是 345600000000，肉眼没法核对。
    if (op.HasMember("risk")) {
        auto& s = op["risk"];
        auto& c = m_ptCfg.risk;

        // ADL
        if (s.HasMember("adlPositionThresholdUsdt")) {
            c.adlPositionThresholdUsdt = std::stod(s["adlPositionThresholdUsdt"].GetString());
        }

        if (s.HasMember("adlHighRankThreshold")) {
            c.adlHighRankThreshold = std::stod(s["adlHighRankThreshold"].GetString());
        }

        if (s.HasMember("positionExceedDurationSec")) {
            c.positionExceedDuration  = std::stoll(s["positionExceedDurationSec"].GetString()) * 1000000LL;
        }

        // 价差不回归
        if (s.HasMember("minHoldDurationSec")) {
            c.minHoldDurationUs = std::stoll(s["minHoldDurationSec"].GetString()) * 1000000LL;
        }

        if (s.HasMember("spreadNoRegressionSec")) {
            c.spreadNoRegressionDuration = std::stoll(s["spreadNoRegressionSec"].GetString()) * 1000000LL;
        }

        // 资金费率异常（绝对金额，USDT）
        if (s.HasMember("fundingAbnormalUsdt")) {
            c.fundingAbnormalUsdt = std::stod(s["fundingAbnormalUsdt"].GetString());
        }

        // 碎单
        if (s.HasMember("tinyCloseThresholdUsdt"))  {
            c.tinyCloseThresholdUsdt  = std::stod(s["tinyCloseThresholdUsdt"].GetString());
        }

        if (s.HasMember("tinyCloseScanIntervalSec")) {
            c.tinyCloseScanIntervalUs = std::stoll(s["tinyCloseScanIntervalSec"].GetString()) * 1000000LL;
        }

        // 渐进式平仓：等待时长（秒 -> 微秒）
        if (s.HasMember("tier1WaitSec")) {
            c.tier1WaitUs = std::stoll(s["tier1WaitSec"].GetString()) * 1000000LL;
        }

        if (s.HasMember("tier2WaitSec")) {
            c.tier2WaitUs = std::stoll(s["tier2WaitSec"].GetString()) * 1000000LL;
        }

        if (s.HasMember("tier3WaitSec")) {
            c.tier3WaitUs = std::stoll(s["tier3WaitSec"].GetString()) * 1000000LL;
        }

        // 渐进式平仓：让利幅度。单位是**绝对价差**（不是比例），量纲必须与价差本身
        // 对齐（缓冲带 5e-6、openProfitPct 1e-4、祖先 modify_shift_pct 2e-4）。
        if (s.HasMember("tier1ForgoProfit")) {
            c.tier1ForgoProfit = std::stod(s["tier1ForgoProfit"].GetString());
        }

        if (s.HasMember("tier2ForgoProfit")) {
            c.tier2ForgoProfit = std::stod(s["tier2ForgoProfit"].GetString());
        }

        if (s.HasMember("tier3ForgoProfit")) {
            c.tier3ForgoProfit = std::stod(s["tier3ForgoProfit"].GetString());
        }

        pt::RiskManager::Instance().SetConfig(c);
    }

    // ---- 3) op.pairTrading -> m_ptCfg（策略参数 + 算法单参数，直接用，不用推送）----
    if (op.HasMember("pairTrading")) {
        auto& s = op["pairTrading"];

        // 敞口
        if (s.HasMember("exposureMaxLimit")) {
            m_ptCfg.exposureMaxLimit     = std::stod(s["exposureMaxLimit"].GetString());
        }

        if (s.HasMember("exposureMaxLimitCoff")) {
            m_ptCfg.exposureMaxLimitCoff = std::stod(s["exposureMaxLimitCoff"].GetString());
        }

        if (s.HasMember("exposureCancelTimes"))  {
            m_ptCfg.exposureCancelTimes  = std::stod(s["exposureCancelTimes"].GetString());
        }

        // 价差分位数。这两个直接决定 RecalcOrderParams 算出来的开平仓阈值：
        // 分位数越极端（quantileUp 越大 / quantileDn 越小）边界越靠外、越容易触发。
        if (s.HasMember("quantileUp")) {
            m_ptCfg.quantileUp = std::stod(s["quantileUp"].GetString());
        }

        if (s.HasMember("quantileDn")) {
            m_ptCfg.quantileDn = std::stod(s["quantileDn"].GetString());
        }

        // 价差统计生产者
        if (s.HasMember("spreadStatsWindowSec")) {
            m_ptCfg.spreadStatsWindowSec = std::stoi(s["spreadStatsWindowSec"].GetString());
        } 

        if (s.HasMember("spreadStatsUpdateIntervalSec")) {
            m_ptCfg.spreadStatsUpdateIntervalSec = std::stoi(s["spreadStatsUpdateIntervalSec"].GetString());
        }

        if (s.HasMember("spreadStatsMinSamples")) {
            m_ptCfg.spreadStatsMinSamples = std::stoi(s["spreadStatsMinSamples"].GetString());
        }

        if (s.HasMember("spreadSampleIntervalMs")) {
            m_ptCfg.spreadSampleIntervalMs = std::stoi(s["spreadSampleIntervalMs"].GetString());
        } 

        if (s.HasMember("spreadFreshnessSec")) {
            m_ptCfg.spreadFreshnessSec = std::stoi(s["spreadFreshnessSec"].GetString());
        } 

        // 分位数快照（重启免预热）。路径为空 / maxStaleSec<=0 都等于关掉这个功能。
        if (s.HasMember("spreadStatsStatePath")) {
            m_ptCfg.spreadStatsStatePath = s["spreadStatsStatePath"].GetString();
        }

        if (s.HasMember("spreadStatsMaxStaleSec")) {
            m_ptCfg.spreadStatsMaxStaleSec = std::stoi(s["spreadStatsMaxStaleSec"].GetString());
        }

        // 算法单机会超时（已经是毫秒，不用换算）
        if (s.HasMember("algoOrderTimeoutMs")) {
            m_ptCfg.algoOrderTimeoutMs = std::stoll(s["algoOrderTimeoutMs"].GetString());
        }

        // 改参
        if (s.HasMember("modifyTimespanSec")) {
            m_ptCfg.modifyTimespanSec = std::stoll(s["modifyTimespanSec"].GetString());
        }

        if (s.HasMember("modifyShiftPct")) {
            m_ptCfg.modifyShiftPct    = std::stod(s["modifyShiftPct"].GetString());
        }

        // 周期任务
        if (s.HasMember("volumeRecalcIntervalSec")) {
            m_ptCfg.volumeRecalcIntervalSec = std::stoi(s["volumeRecalcIntervalSec"].GetString());
        }

        if (s.HasMember("csvSaveIntervalSec")) {
            m_ptCfg.csvSaveIntervalSec = std::stoi(s["csvSaveIntervalSec"].GetString());
        } 

        // 启动对账
        if (s.HasMember("reconcileTolRatio")) {
            m_ptCfg.reconcileTolRatio = std::stod(s["reconcileTolRatio"].GetString());
        } 

        if (s.HasMember("reconcileWarnIntervalSec")) {
            m_ptCfg.reconcileWarnIntervalSec = std::stoi(s["reconcileWarnIntervalSec"].GetString());
        }

        if (s.HasMember("freezeOnOrphanAlgoOrder"))  {
            m_ptCfg.freezeOnOrphanAlgoOrder  = (s["freezeOnOrphanAlgoOrder"].GetString() == std::string("true"));
        }

        // ---- 算法单参数 ----
        // 这些字段原来硬编码在 PairTradingContext::BuildAlgoOrderJson 里，现在就是
        // 本段的普通字段，由 BuildAlgoOrderJson 直接读，没有中间结构体。

        // 报单类型。用现成的 OrderTypeStr2EnumMap，但必须 find：
        // operator[] 在 key 不存在时会**插入**一个默认值（OT_MIN 哨兵），
        // 子单会带着哨兵类型报出去。find 之后还要显式拒掉 OT_MIN。
        // 判据最终落在**子单**的 orderType 上（BaseAlgoOrder.cpp:457 用它决定走
        // Maker 还是 Taker 撤单分支），子单类型直接取自这三个值。
        if (s.HasMember("ttActiveOrderType")) {
            const std::string t = s["ttActiveOrderType"].GetString();
            auto it = OrderTypeStr2EnumMap.find(t);
            if (it != OrderTypeStr2EnumMap.end() && it->second != OT_MIN) {
                m_ptCfg.ttActiveOrderType = it->second;
            }
            else {
                LOG_WARN("配置项 ttActiveOrderType 的值 \"{}\" 不是合法 OrderType -> 保持默认值 {}", t, OrderTypeEnum2StrMap[m_ptCfg.ttActiveOrderType]);
            }
        }

        if (s.HasMember("mtActiveOrderType")) {
            const std::string t = s["mtActiveOrderType"].GetString();
            auto it = OrderTypeStr2EnumMap.find(t);
            if (it != OrderTypeStr2EnumMap.end() && it->second != OT_MIN) {
                m_ptCfg.mtActiveOrderType = it->second;
            }
            else {
                LOG_WARN("配置项 mtActiveOrderType 的值 \"{}\" 不是合法 OrderType -> 保持默认值 {}", t, OrderTypeEnum2StrMap[m_ptCfg.mtActiveOrderType]);
            }
        }

        if (s.HasMember("passiveOrderType")) {
            const std::string t = s["passiveOrderType"].GetString();
            auto it = OrderTypeStr2EnumMap.find(t);
            if (it != OrderTypeStr2EnumMap.end() && it->second != OT_MIN) {
                m_ptCfg.passiveOrderType = it->second;
            }
            else {
                LOG_WARN("配置项 passiveOrderType 的值 \"{}\" 不是合法 OrderType -> 保持默认值 {}", t, OrderTypeEnum2StrMap[m_ptCfg.passiveOrderType]);
            }
        }

        // 盘口深度检查
        if (s.HasMember("activeDepthMakerCheck"))  {
            m_ptCfg.activeDepthMakerCheck  = (s["activeDepthMakerCheck"].GetString()  == std::string("true"));
        }

        if (s.HasMember("activeDepthTakerCheck"))  {
            m_ptCfg.activeDepthTakerCheck  = (s["activeDepthTakerCheck"].GetString()  == std::string("true"));
        }

        if (s.HasMember("passiveDepthMakerCheck")) {
            m_ptCfg.passiveDepthMakerCheck = (s["passiveDepthMakerCheck"].GetString() == std::string("true"));
        }

        if (s.HasMember("passiveDepthTakerCheck")) {
            m_ptCfg.passiveDepthTakerCheck = (s["passiveDepthTakerCheck"].GetString() == std::string("true"));
        }

        // 价格偏移比例
        if (s.HasMember("activePriceTakerPct")) {
            m_ptCfg.activePriceTakerPct  = std::stod(s["activePriceTakerPct"].GetString());
        }

        if (s.HasMember("activePriceMakerPct")) {
            m_ptCfg.activePriceMakerPct  = std::stod(s["activePriceMakerPct"].GetString());
        }

        if (s.HasMember("passivePriceTakerPct")) {
            m_ptCfg.passivePriceTakerPct = std::stod(s["passivePriceTakerPct"].GetString());
        }

        if (s.HasMember("passivePriceMakerPct")) {
            m_ptCfg.passivePriceMakerPct = std::stod(s["passivePriceMakerPct"].GetString());
        }

        if (s.HasMember("passiveVolumePct")) {
            m_ptCfg.passiveVolumePct = std::stod(s["passiveVolumePct"].GetString());
        }

        // 子单撤单门槛：时间（**毫秒**，写进 BaseAlgoOrder 时 ×1000 变微秒）
        if (s.HasMember("activeMakerCancelOrderTimeMs"))  {
            m_ptCfg.activeMakerCancelOrderTimeMs  = std::stoll(s["activeMakerCancelOrderTimeMs"].GetString());
        }

        if (s.HasMember("activeTakerCancelOrderTimeMs"))  {
            m_ptCfg.activeTakerCancelOrderTimeMs  = std::stoll(s["activeTakerCancelOrderTimeMs"].GetString());
        }

        if (s.HasMember("passiveMakerCancelOrderTimeMs")) {
            m_ptCfg.passiveMakerCancelOrderTimeMs = std::stoll(s["passiveMakerCancelOrderTimeMs"].GetString());
        }

        if (s.HasMember("passiveTakerCancelOrderTimeMs")) {
            m_ptCfg.passiveTakerCancelOrderTimeMs = std::stoll(s["passiveTakerCancelOrderTimeMs"].GetString());
        }

        // 子单撤单门槛：比例（0.001 = 10bp）
        if (s.HasMember("activePassiveCancelOrderPct")) {
            m_ptCfg.activePassiveCancelOrderPct = std::stod(s["activePassiveCancelOrderPct"].GetString());
        }

        if (s.HasMember("activeMakerCancelOrderPct")) {
            m_ptCfg.activeMakerCancelOrderPct = std::stod(s["activeMakerCancelOrderPct"].GetString());
        }

        if (s.HasMember("activeTakerCancelOrderPct")) {
            m_ptCfg.activeTakerCancelOrderPct   = std::stod(s["activeTakerCancelOrderPct"].GetString());
        }

        if (s.HasMember("passiveMakerCancelOrderPct")) {
            m_ptCfg.passiveMakerCancelOrderPct = std::stod(s["passiveMakerCancelOrderPct"].GetString());
        }

        if (s.HasMember("passiveTakerCancelOrderPct")) {
            m_ptCfg.passiveTakerCancelOrderPct  = std::stod(s["passiveTakerCancelOrderPct"].GetString());
        }

        if (s.HasMember("maxMTOrderSize")) {
            m_ptCfg.maxMTOrderSize = std::stod(s["maxMTOrderSize"].GetString());
        }

        if (s.HasMember("maxTTOrderSize")) {
            m_ptCfg.maxTTOrderSize = std::stod(s["maxTTOrderSize"].GetString());
        }

        // 目标价差口径 / 主动腿量算法。这两个枚举表里也带 *_MIN / *_MAX 哨兵，
        // 所以不能用 find 就完事，得显式只认下面这几个真值。
        if (s.HasMember("targetSpreadType")) {
            const std::string t = s["targetSpreadType"].GetString();
            if (t == "TargetSpredPrice_NOW") {
                m_ptCfg.targetSpreadType = stra::TargetSpredPrice_NOW;
            }
            else if (t == "TargetSpredPrice_NOW_MEAN") {
                m_ptCfg.targetSpreadType = stra::TargetSpredPrice_NOW_MEAN;
            }
            else {
                LOG_WARN("配置项 targetSpreadType 的值 \"{}\" 不是合法 TargetSpredPrice（可用：TargetSpredPrice_NOW / TargetSpredPrice_NOW_MEAN） -> 保持默认值 {}", t, stra::TargetSpredPriceEnum2Str[m_ptCfg.targetSpreadType]);
            }
        }
        if (s.HasMember("activeVolumeCalcualteType")) {
            const std::string t = s["activeVolumeCalcualteType"].GetString();
            if (t == "ActiveVolumeCalcualteType_PassiveVolumePct") {
                m_ptCfg.activeVolumeCalcualteType = stra::ActiveVolumeCalcualteType_PassiveVolumePct;
            }
            else {
                LOG_WARN("配置项 activeVolumeCalcualteType 的值 \"{}\" 不是合法 ActiveVolumeCalcualteType（可用：ActiveVolumeCalcualteType_PassiveVolumePct） -> 保持默认值 {}", t, stra::ActiveVolumeCalcualteTypeEnum2Str[m_ptCfg.activeVolumeCalcualteType]);
            }   
        }

        // rebalance：MT 与 TT 分开配
        if (s.HasMember("mtRebalanceSwitch")) {
            m_ptCfg.mtRebalanceSwitch = (s["mtRebalanceSwitch"].GetString() == std::string("true"));
        }

        if (s.HasMember("ttRebalanceSwitch")) {
            m_ptCfg.ttRebalanceSwitch = (s["ttRebalanceSwitch"].GetString() == std::string("true"));
        }

        if (s.HasMember("mtRebalanceFlag")) {
            m_ptCfg.mtRebalanceFlag = (s["mtRebalanceFlag"].GetString()   == std::string("true"));
        }

        if (s.HasMember("ttRebalanceFlag")) {
            m_ptCfg.ttRebalanceFlag   = (s["ttRebalanceFlag"].GetString()   == std::string("true"));
        } 

        // 价格趋势保护
        if (s.HasMember("mtPriceTrendProtectFlag")) {
            m_ptCfg.mtPriceTrendProtectFlag = (s["mtPriceTrendProtectFlag"].GetString() == std::string("true"));
        }

        if (s.HasMember("ttPriceTrendProtectFlag")) {
            m_ptCfg.ttPriceTrendProtectFlag = (s["ttPriceTrendProtectFlag"].GetString() == std::string("true"));
        }

        // 价格 tick 偏移
        if (s.HasMember("activePriceTickFlag")) {
            m_ptCfg.activePriceTickFlag = (s["activePriceTickFlag"].GetString()  == std::string("true"));
        }

        if (s.HasMember("activePriceTickNum")) {
            m_ptCfg.activePriceTickNum = std::stoi(s["activePriceTickNum"].GetString());
        }

        if (s.HasMember("passivePriceTickFlag")) {
            m_ptCfg.passivePriceTickFlag = (s["passivePriceTickFlag"].GetString() == std::string("true"));
        }

        if (s.HasMember("passivePriceTickNum")) {
            m_ptCfg.passivePriceTickNum = std::stoi(s["passivePriceTickNum"].GetString());
        }
    }

    // 生效值打日志。实盘最怕"改了配置没生效"或"生效了但不是你以为的那份"，
    // 尤其是费率 —— 它同时决定信号阈值与算法单阶梯，错了会直接亏钱。
    // 这里打的是**解析之后**的值（不是文件里的原文），所以能直接对着核对。
    {
        const auto& fs  = m_ptCfg.feeSlippage;
        const auto& rc  = m_ptCfg.risk;
        const auto& aoc = m_ptCfg;
        LOG_INFO("配置生效[成本] F_tt={} F_mt={} | 主动 maker/taker={}/{} 被动 maker/taker={}/{} 滑点={}",
                 pt::SignalGenerator::Instance().CalcExecCost(true),
                 pt::SignalGenerator::Instance().CalcExecCost(false),
                 fs.activeMakerFeeRate, fs.activeTakerFeeRate,
                 fs.passiveMakerFeeRate, fs.passiveTakerFeeRate, fs.basicSlippage);
        LOG_INFO("配置生效[信号] spreadAdjPct={} minSpreadSpan={} minSpreadTarget={} ttAddPercent={} openProfitPct={} openMaxFundingRate={}",
                 fs.spreadAdjPct, fs.minSpreadSpan, fs.minSpreadTarget, fs.ttAddPercent,
                 fs.openProfitPct, fs.openMaxFundingRate);
        LOG_INFO("配置生效[统计] quantileUp={} quantileDn={} windowSec={} updateIntervalSec={} minSamples={} sampleIntervalMs={} freshnessSec={}",
                 m_ptCfg.quantileUp, m_ptCfg.quantileDn, m_ptCfg.spreadStatsWindowSec,
                 m_ptCfg.spreadStatsUpdateIntervalSec, m_ptCfg.spreadStatsMinSamples,
                 m_ptCfg.spreadSampleIntervalMs, m_ptCfg.spreadFreshnessSec);
        LOG_INFO("配置生效[分位数快照] path={} maxStaleSec={} ({})",
                 m_ptCfg.spreadStatsStatePath, m_ptCfg.spreadStatsMaxStaleSec,
                 m_ptCfg.spreadStatsMaxStaleSec > 0 ? "重启免预热已开" : "已关闭，每次重启正常预热");
        LOG_INFO("配置生效[风控] tinyCloseThresholdUsdt={} fundingAbnormalUsdt={} minHoldDurationSec={} spreadNoRegressionSec={} tier1WaitSec={} tier2WaitSec={}",
                 rc.tinyCloseThresholdUsdt, rc.fundingAbnormalUsdt,
                 rc.minHoldDurationUs / 1000000LL, rc.spreadNoRegressionDuration / 1000000LL,
                 rc.tier1WaitUs / 1000000LL, rc.tier2WaitUs / 1000000LL);
        LOG_INFO("配置生效[算法单] ttActiveOrderType={} mtActiveOrderType={} passiveOrderType={} cancelOrderTimeMs={} cancelOrderPct={} algoOrderTimeoutMs={} modifyTimespanSec={}",
                 OrderTypeEnum2StrMap[aoc.ttActiveOrderType], OrderTypeEnum2StrMap[aoc.mtActiveOrderType],
                 OrderTypeEnum2StrMap[aoc.passiveOrderType], aoc.activeMakerCancelOrderTimeMs,
                 aoc.activeMakerCancelOrderPct, m_ptCfg.algoOrderTimeoutMs, m_ptCfg.modifyTimespanSec);
    }

    algoContext.Init(smc);
    algoContext.SetDbp(dbpreader);
    algoContext.PreStart();

    ptContext.SetAlgoCommandCallback([this](BaseAlgoOrder* pAlgoOrder) { 
        SubmitAlgoCommand(pAlgoOrder); 
    });

    ptContext.SetAlgoOrderModifyCallback([this](int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify) {
        SubmitAlgoOrderChange(algoOrderId, cmd, modify);
    });

    ptContext.Init(m_ptCfg, smc);
}

void PairTradingStrategy::pre_stop() {
    if (!m_ptCfg.csvStatePath.empty()) {
        // 停机前落一次快照（原子写），崩溃/被杀时最坏只丢一个保存周期
        pt::PairInfoManager::Instance().SaveSnapshot(m_ptCfg.csvStatePath);
    }

    // 显式停掉落库线程并 flush 缓冲区。
    // 只靠 ~WriteFileContent() 不够：exit() 里的静态析构顺序跨 TU 未定义，
    // 万一 contentQueue（BaseAlgoOrder.cpp 里的全局对象）先被析构，
    // 写线程还在跑就会访问已析构对象。
    WriteFileContent::GetInstance().Stop();

    BaseStrategy::pre_stop();
}

void PairTradingStrategy::SubmitAlgoCommand(BaseAlgoOrder* pAlgoOrder) {
    algoContext.SubmitAlgoOrder(pAlgoOrder);
}

void PairTradingStrategy::SubmitAlgoOrderChange(int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify) {
    algoContext.SubmitAlgoOrder(algoOrderId, cmd, modify);
}

void PairTradingStrategy::on_command(const std::string& json) {
    algoContext.OnCommand(json);
}

void PairTradingStrategy::on_timer(const int64_t& utcTime) {
    algoContext.OnTimer(utcTime);
    ptContext.OnTimer(utcTime);

    if (utcTime - m_lastScanUs >= SCAN_INTERVAL_US) {
        ScanFinishedAlgoOrders(utcTime);
        m_lastScanUs = utcTime;
    }


    if (utcTime - lastOnCommand > 10000000LL) {
        if (!createAlgo) {
            algoContext.OnCommand("");
            lastOnCommand = utcTime;
            createAlgo = true;
        }
    }
}

void PairTradingStrategy::on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum) {
    algoContext.OnSpread(topic, pdata);
    ptContext.OnSpread(topic, pdata);
}


void PairTradingStrategy::on_balance(pubsub::Balance& balance) {
    algoContext.OnBalance(balance);
    ptContext.OnBalance(balance);
}


void PairTradingStrategy::on_position(pubsub::Position& position) {
    algoContext.OnPosition(position);
    ptContext.OnPosition(position);
}


void PairTradingStrategy::on_total_account(pubsub::TotalAccount& totalAccount) {
    algoContext.OnTotalAccount(totalAccount);
    ptContext.OnTotalAccount(totalAccount);
}


void PairTradingStrategy::on_ordertrade(pubsub::OrderResponse& orderResponse) {
    algoContext.OnOrder(orderResponse);
}

// 兜底扫描：算法单终结的回传已由 AlgoContext 在同线程内直接调用
// （OnTimer 的终态分支 + BaseAlgoOrder::PairOrderTrade 的成交分支），
// 这里只处理漏网的情况，正常路径不会命中。
void PairTradingStrategy::ScanFinishedAlgoOrders(int64_t nowUs) {
    auto& pim = pt::PairInfoManager::Instance();

    for (pt::PairInfo* pi : pim.GetAllPairInfos()) {
        if (!pi->hasActiveAlgoOrder) {
            continue;
        }

        int64_t algoOrderIdInt = pi->currentAlgoOrderId;
        BaseAlgoOrder* order = algoContext.GetAlgoOrder(algoOrderIdInt);

        if (!order) {
            // 拿不到算法单时任何量价都是错的，只释放对子，不碰 pair_info
            LOG_INFO("ScanFinishedAlgoOrders: algo order not found, release pair. algoOrderId:{} pairKey:{}", pi->currentAlgoOrderId, pi->pairInstrumentKey);
            pim.ClearActiveAlgoOrder(pi->pairInstrumentKey);
            continue;
        }

        if (order->algoOrderStatus == stra::ALGO_OS_FILLED || order->algoOrderStatus == stra::ALGO_OS_CANCELED || order->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED) {
            ptContext.OnAlgoOrderUpdate(order);
        }
    }
}