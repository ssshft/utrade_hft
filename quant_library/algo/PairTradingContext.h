/***
 * 1. 初始化所有币对的pairinfo
 * 2. 响应价差、持仓、资金、定时器等事件
 * 3. 通过SignalGenerator检测开平仓信号
 * 4. 通过RiskManager检测异常风险
 * 5. 将算法单指令提交给AlgoContext
 * ***/

#pragma once

#include "../basic/PairInfo.h"
#include "../basic/PairInfoManager.h"
#include "../basic/BaseAlgoOrder.h"
#include "../signal/SignalGenerator.h"
#include "../risk/RiskManager.h"


namespace pt {

struct PairTradingConfig {
    std::vector<std::string> pairKeys;

    int activeAccountId{0};
    int passiveAccountId{0};

    double maxPositionValue{100}; // 总持仓上限
    double maxAmount{50};
    double targetAmount{50};
    double exposureMaxLimit{10}; // 敞口上限
    double exposureMaxLimitCoff{1.0}; // 敞口系数

    // 价差统计分位数
    double quantileUp{0.9};
    double quantileDn{0.1};

    // 算法单超时ms
    int64_t algoOrderTimeoutMs{30000};

    std::string csvStatePath{"data/pair_info.csv"};

    int spreadStatsUpdateIntervalSec{3600}; // 1h 更新统计
    int volumeRecalcIntervalSec{60};     // 1min 重算仓位参数
    int csvSaveIntervalSec{300};       // 5min 保存csv
    int signalRecalcIntervalSec{300};    // 5min 重算orderParams
};

// 回调直接传递创建好的算法单对象（不再拼 JSON 字符串）
using AlgoCommandCallback = std::function<void(BaseAlgoOrder* pAlgoOrder)>;

class PairTradingContext {
public:
    PairTradingContext();
    ~PairTradingContext() = default;

    void Init(const PairTradingConfig& cfg, sm::SecurityManager* s);

    void SetAlgoCommandCallback(AlgoCommandCallback cb) {
        m_algoCommandCb = std::move(cb);
    }

    void OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata);

    void OnPosition(const pubsub::Position& position);

    void OnBalance(const pubsub::Balance& balance);

    void OnTotalAccount(const pubsub::TotalAccount& totalAccount);

    // volumeFilled 实际成交量
    // isFullyFlat 平仓后是否完全归零
    void OnAlgoOrderUpdate(const std::string& pairKey, const std::string& algoOrderId, double volumeFilled, double activePriceFilled, double passivePriceFilled, bool isFinished, bool isFullyFlat) ;

    void OnTimer(int64_t nowUs);

    PairInfoManager& GetPairInfoManager() {
        return PairInfoManager::Instance();
    }

    const PairTradingConfig& GetConfig() const {
        return m_cfg;
    }

private:
    PairTradingConfig m_cfg;
    AlgoCommandCallback m_algoCommandCb;

    int64_t m_lastSpreadStatsUpdateUs{0};
    int64_t m_lastVolumeRecalcUs{0};
    int64_t m_lastCsvSaveUs{0};
    int64_t m_lastSignalRecalcUs{0};

    void ProcessPairSignal(PairInfo& pi);

    void ProcessRisk(PairInfo& pi, int64_t nowUs);

    // direction : OL/OS/CL/CS;   algoMode: TT/MT
    void SubmitAlgoOrder(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit = 0.0) const;

    // 函数名沿用旧名，但已不再拼 JSON：直接创建算法单对象并返回，创建失败（开关关闭/报单量非法）返回 nullptr
    BaseAlgoOrder* BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const;

    static int64_t NowUs();

    // 算法单唯一ID：必须是纯数字，ScanFinishedAlgoOrders 会用 stoll(currentAlgoOrderId)
    // 还原成 int64 去 AlgoContext 的 alogOrderManager 里查算法单
    static int64_t GenerateAlgoOrderId();

    std::string baseAsset{"USDT"};

    sm::SecurityManager* smc{nullptr};
};

}