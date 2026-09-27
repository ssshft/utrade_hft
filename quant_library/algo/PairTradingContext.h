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
#include "../signal/SpreadStatsBuilder.h"
#include "../risk/RiskManager.h"

#include <unordered_map>


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

    // 价差统计分位数（祖先 quantile_up=0.92 / quantile_dn=1-quantile_up=0.08）
    // 分位数越极端 -> 分位数边界越靠外 -> 入口阈值越宽 -> 开平机会越多
    double quantileUp{0.92};
    double quantileDn{0.08};

    // ---- 价差统计生产者（对齐祖先 pair_trading_c_gateio）----
    int spreadStatsWindowSec{86400};       // 24h 滚动窗口（祖先 spread_df_update_period）
    int spreadStatsUpdateIntervalSec{60};  // 60s 刷新统计（祖先 spread_df_update_timespan，原值 3600）
    int spreadStatsMinSamples{8640};       // 样本数门槛（祖先 spread_count > 24*3600/5*0.5）
    int spreadSampleIntervalMs{200};       // 采样间隔，控制内存；0 = 不降频（逐 tick 全存）
    int spreadFreshnessSec{30};            // 行情新鲜度门槛（祖先 lastGenerateTs < 30s）

    // 算法单机会超时ms（祖先 algo_order_cancel_time = 150s）。
    // 语义是"机会连续不成立的时长"，不是订单年龄：机会条件一成立就刷新 satisfyTime，
    // 所以只有连续不成立超过这个时长才会撤单。见 UpdateSatisfyTime / CheckAlgoOrderTimeout
    int64_t algoOrderTimeoutMs{150000};

    // 敞口异常倍数（祖先 :1081 的 4 * ttTargetVolume）：
    // 双腿净敞口 / activeMultiple 超过 exposureCancelTimes * ttTargetVolume 即视为敞口失控
    double exposureCancelTimes{4.0};

    std::string csvStatePath{"data/pair_info.csv"};

    int volumeRecalcIntervalSec{60};     // 1min 重算仓位参数
    int csvSaveIntervalSec{300};       // 5min 保存csv
};

// 回调直接传递创建好的算法单对象（不再拼 JSON 字符串）
using AlgoCommandCallback = std::function<void(BaseAlgoOrder* pAlgoOrder)>;

// 算法单变更回调（撤单 / 改参）：按 id 指回已在册的算法单，与 AlgoContext 的变更重载一一对应
using AlgoOrderModifyCallback = std::function<void(int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify)>;

class PairTradingContext {
public:
    PairTradingContext();
    ~PairTradingContext();

    void Init(const PairTradingConfig& cfg, sm::SecurityManager* s);

    void SetAlgoCommandCallback(AlgoCommandCallback cb) {
        m_algoCommandCb = std::move(cb);
    }

    void SetAlgoOrderModifyCallback(AlgoOrderModifyCallback cb) {
        m_algoOrderModifyCb = std::move(cb);
    }

    void OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata);

    void OnPosition(const pubsub::Position& position);

    void OnBalance(const pubsub::Balance& balance);

    void OnTotalAccount(const pubsub::TotalAccount& totalAccount);

    // 算法单执行端回传：量 / 价 / 状态一次性带入。
    // 非终态只同步量价（保持 PairInfo 与算法单一致），终态才结算并释放对子。
    void OnAlgoOrderUpdate(BaseAlgoOrder* order);

    // 单实例入口（方案A）：算法单创建与执行同线程，AlgoContext / BaseAlgoOrder 直接回传，
    // 不走消息队列。未 Init 时返回 nullptr。
    static PairTradingContext* Instance();

    // AlgoContext / BaseAlgoOrder 的调用入口；未 Init 时安全跳过
    static void NotifyAlgoOrderUpdate(BaseAlgoOrder* order);

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
    AlgoOrderModifyCallback m_algoOrderModifyCb;

    int64_t m_lastSpreadStatsUpdateUs{0};
    int64_t m_lastVolumeRecalcUs{0};
    int64_t m_lastCsvSaveUs{0};

    // 每个币对一份 24h 滚动价差样本窗口
    // lastSampleTs 用于按 spreadSampleIntervalMs 降频采样，控制内存
    struct SpreadWindow {
        SpreadStatsBuilder builder;
        int64_t lastSampleTs{0};
    };
    std::unordered_map<std::string, SpreadWindow> m_spreadWindows;

    // 从 OnSpread 的原始行情里抽一条价差样本，写入对应币对的滚动窗口
    void AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata);

    void ProcessPairSignal(PairInfo& pi);

    void ProcessRisk(PairInfo& pi, int64_t nowUs);

    // 机会条件快照（祖先 cc_pricespread_gb_ltp.py:922/930/936）：
    // 每 tick 对**每个**对子都算一次，与是否已有算法单无关；条件成立就把 pi.satisfyTime 刷成 nowUs。
    // 于是 satisfyTime 的含义是"最后一次机会成立的时间"，CheckAlgoOrderTimeout 用它算"连续不成立"的时长。
    // 祖先是在 pair_info 整个 DataFrame 上整列赋值，所以这里也不能只对有单的对子算。
    void UpdateSatisfyTime(PairInfo& pi, const SignalResult& sig, bool canOpen, bool canClose, int64_t nowUs) const;

    // 撤单触发①：机会超时（祖先 :1021）。
    // nowUs - satisfyTime > algoOrderTimeoutMs 时请求撤单。手动单（autoFlag == false）不撤。
    void CheckAlgoOrderTimeout(const PairInfo& pi, int64_t nowUs) const;

    // 撤单触发⑤：敞口异常（祖先 :1081-1092）。
    // 双腿都有实盘持仓、且净敞口超过 exposureCancelTimes * ttTargetVolume 时，
    // 置 pi.errorFlag 停止该对子的一切报单，并撤掉在跑的算法单（祖先置 status=ERROR，留人工处理）。
    void CheckExposureAbnormal(PairInfo& pi) const;

    // 请求执行端撤销该对子当前的算法单。幂等：对子没有活跃算法单、或算法单已在撤单流程里，
    // 都会被忽略。撤单是异步的（撤主动腿报价 -> 等被动腿成交把敞口平掉 -> 子单清零
    // -> ALGO_OS_CANCELED -> 回传释放对子），调用方不能假设它同步生效。
    void RequestCancelAlgoOrder(const PairInfo& pi) const;

    // direction : OL/OS/CL/CS;   algoMode: TT/MT
    // ⚠️ 调用方必须在报单前保证 pi.satisfyTime 是新鲜的（见 CheckAlgoOrderTimeout）：
    //    信号驱动的报单已被 ProcessPairSignal 里的 UpdateSatisfyTime 覆盖；
    //    风控驱动的强平报单没有信号，必须在 ProcessRisk 里显式刷新，
    //    否则刚报出的强平单会因为 satisfyTime 陈旧被"机会超时"立刻撤掉。
    void SubmitAlgoOrder(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit = 0.0) const;

    // 函数名沿用旧名，但已不再拼 JSON：直接创建算法单对象并返回，创建失败（开关关闭/报单量非法）返回 nullptr
    BaseAlgoOrder* BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const;

    static int64_t NowUs();

    // 算法单唯一ID：必须是纯数字，ScanFinishedAlgoOrders 会用 stoll(currentAlgoOrderId)
    // 还原成 int64 去 AlgoContext 的 alogOrderManager 里查算法单
    static int64_t GenerateAlgoOrderId();

    std::string baseAsset{"USDT"};

    sm::SecurityManager* smc{nullptr};

    // 由 Init 注册，析构时注销；供执行端同线程直接回传算法单更新
    static PairTradingContext* s_instance;
};

}