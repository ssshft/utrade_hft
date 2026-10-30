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
#include "../basic/RuntimeConfig.h"
#include "../basic/BaseAlgoOrder.h"
#include "../signal/SignalGenerator.h"
#include "../signal/SpreadStatsBuilder.h"
#include "../risk/RiskManager.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>
#include <boost/lockfree/spsc_queue.hpp>


namespace pt {

using SpreadSampleQueue = boost::lockfree::spsc_queue<SpreadSampleMsg, boost::lockfree::capacity<256>>;
using SpreadStatsQueue  = boost::lockfree::spsc_queue<SpreadStatsMsg,  boost::lockfree::capacity<256>>;
using PairInfoQueue = boost::lockfree::spsc_queue<PairInfoRecord, boost::lockfree::capacity<1024>>;
using RuntimeParamQueue = boost::lockfree::spsc_queue<RuntimeConfig, boost::lockfree::capacity<16>>;


using AlgoCommandCallback = std::function<void(BaseAlgoOrder* pAlgoOrder)>;
using AlgoOrderModifyCallback = std::function<void(int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify)>;

struct PairTradingConfig {
    // 对子清单：pre_start 边遍历它边订阅行情
    std::vector<std::string> pairKeys;
    int activeAccountId{0};
    int passiveAccountId{0};

    double maxPositionValue{0}; // 所有对子的总和的持仓金额
    double maxAmount{50};       // 单个对子最大持仓金额 -> RecalcVolumeParams -> pi.maxVolume
    double targetAmount{50};    // 单个报单目标金额 -> pi.ttTargetVolume / mtTargetVolume

    FeeSlippageConfig feeSlippage; // 执行成本 / 滑点 / 价差阈值
    RiskConfig risk; // 风控门槛 + 渐进式平仓三档

    OrderType ttActiveOrderType{OT_LIMIT};
    OrderType mtActiveOrderType{OT_POST_ONLY};
    OrderType passiveOrderType{OT_LIMIT};

    // ---- 盘口深度检查（当前四个都关）----
    bool activeDepthMakerCheck{false};
    bool activeDepthTakerCheck{false};
    bool passiveDepthMakerCheck{false};
    bool passiveDepthTakerCheck{false};

    // ---- 价格偏移比例（相对对手价，当前都是 0 = 不偏移）----
    double activePriceTakerPct{0.0};
    double activePriceMakerPct{0.0};
    double passivePriceTakerPct{0.0};
    double passivePriceMakerPct{0.0};

    // 被动腿报单量占主动腿的比例（0.5 = 半仓对冲）
    double passiveVolumePct{0.5};

    // ---- 子单撤单门槛 ----
    // 时间：配置里写**毫秒**，写进 BaseAlgoOrder 时 ×1000 变微秒
    int64_t activeMakerCancelOrderTimeMs{5LL * 1000};
    int64_t activeTakerCancelOrderTimeMs{5LL * 1000};
    int64_t passiveMakerCancelOrderTimeMs{5LL * 1000};
    int64_t passiveTakerCancelOrderTimeMs{5LL * 1000};
    // 比例：价格偏离超过它才撤（0.001 = 10bp）
    double activePassiveCancelOrderPct{0.001};
    double activeMakerCancelOrderPct{0.001};
    double activeTakerCancelOrderPct{0.001};
    double passiveMakerCancelOrderPct{0.001};
    double passiveTakerCancelOrderPct{0.001};

    // ---- 单张子单的最大报单量（1.0 = 一次只报一手目标量）----
    double maxMTOrderSize{1.0};
    double maxTTOrderSize{1.0};

    // ---- 目标价差口径 / 主动腿量算法 ----
    stra::TargetSpredPrice targetSpreadType{stra::TargetSpredPrice_NOW};
    stra::ActiveVolumeCalcualteType activeVolumeCalcualteType{stra::ActiveVolumeCalcualteType_PassiveVolumePct};

    // ---- rebalance（主动腿成交后是否用 rebalance 模式补被动腿）----
    bool mtRebalanceSwitch{true};
    bool ttRebalanceSwitch{false};
    bool mtRebalanceFlag{true};
    bool ttRebalanceFlag{false};

    // ---- 价格趋势保护（当前都关）----
    bool mtPriceTrendProtectFlag{false};
    bool ttPriceTrendProtectFlag{false};

    // ---- 价格 tick 偏移（当前都关）----
    bool activePriceTickFlag{false};
    int activePriceTickNum{0};
    bool passivePriceTickFlag{false};
    int passivePriceTickNum{0};

    // ---- 策略参数 ----
    double exposureMaxLimit{10}; // 敞口上限
    double exposureMaxLimitCoff{1.0}; // 敞口系数

    // 价差统计分位数
    // 分位数越极端 -> 分位数边界越靠外 -> 入口阈值越宽 -> 开平机会越多
    double quantileUp{0.92};
    double quantileDn{0.08};

    // ---- 价差统计生产者
    int spreadStatsWindowSec{86400};       // 24h 滚动窗口（祖先 spread_df_update_period）
    int spreadStatsUpdateIntervalSec{60};  // 60s 刷新统计（祖先 spread_df_update_timespan，原值 3600）
    int spreadStatsMinSamples{5};       // 样本数门槛（祖先 spread_count > 24*3600/5*0.5）
    int spreadSampleIntervalSec{5};       // 采样间隔 5s，控制内存；0 = 不降频（逐 tick 全存）
    int spreadStatsMaxStaleSec{3600};  // 允许恢复的最大陈旧度（秒，按样本时间 calcTs 算，不是墙钟）<= 0 关闭恢复，永远正常预热。
    std::string spreadStatsStatePath{"data/spread_stats.csv"};  // ---- 分位数快照（重启免预热）----

    // 算法单机会超时ms 会撤单。
    // 语义是"机会连续不成立的时长"，不是订单年龄：机会条件一成立就刷新 satisfyTime，
    int64_t algoOrderTimeoutMs{150000};

    // 敞口异常倍数（4 * ttTargetVolume）：
    // 双腿净敞口 / activeMultiple 超过 exposureCancelTimes * ttTargetVolume 即视为敞口失控
    double exposureCancelTimes{4.0};

    int64_t modifyTimespanSec{60}; // 改参周期 每隔这么久把 pair_info 的最新参数推给在跑的算法单

    double modifyShiftPct{0.0002}; // 流动性危险时的平仓让利 CL 的起止价差各减这么多、CS 的各加这么多，让平仓更容易成交

    std::string csvStatePath{"data/pair_info.csv"};  // 快照路径 空 = 不落盘、也不从快照恢复。
    int csvSaveIntervalSec{10};        // 快照落盘周期

    int volumeRecalcIntervalSec{60};     // 1min 重算仓位参数
    
    // 启动对账
    double reconcileTolRatio{1e-6}; // 对账判据容差：|pairTotalVolume - activeRealPosition| <= max(1e-9, |activeRealPosition| * ratio)
    int reconcileWarnIntervalSec{30}; // 等持仓推送的超时告警周期。超时只告警、**不放行** —— 按"无持仓"放行的后果是重复开仓

    bool freezeOnOrphanAlgoOrder{true}; // 快照里 hasActiveAlgoOrder == true（上一轮有算法单未终结）时，把该对子置 stopFlag 冻结，

    // ---- 运行时配置（可热改）----
    bool closeOnly{false};
    // 停掉所有算法单的**初值**（启动时读一次，之后由 T_signal 轮询覆盖）。
    // true = 撤掉在跑的算法单、且不再创建新的（比 closeOnly 更狠：连平仓和风控强平也不发）。
    bool stopAllAlgo{false};

    // 被监视的配置文件路径（= 本次启动用的那份 config.json）。空 = 不监视、不热更新。
    std::string configPath;
};


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
    void OnAlgoOrderUpdate(BaseAlgoOrder* order);

    // 单实例入口（方案A）：算法单创建与执行同线程，AlgoContext / BaseAlgoOrder 直接回传，
    static PairTradingContext* Instance();

    // AlgoContext / BaseAlgoOrder 的调用入口；未 Init 时安全跳过
    static void NotifyAlgoOrderUpdate(BaseAlgoOrder* order);

    void OnTimer(int64_t nowUs);

    void StopSignalThread();

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

    int64_t m_lastVolumeRecalcUs{0};
    int64_t m_lastCsvSaveUs{0};

    // ---- 启动闸门（§5.2）----
    // Init 读完快照后停在 Reconciling：五个交易入口全部直接 return，
    // 等两腿 pubsub::Position 推送到位、对账通过，才进 Trading。
    enum class StartupPhase {
        Reconciling = 0,   // 等推送 + 对账，交易冻结
        Trading = 1        // 账本可信，放行
    };
    StartupPhase m_phase{StartupPhase::Reconciling};
    int64_t m_startupTimeUs{0};
    int64_t m_lastReconcileWarnUs{0};

    // 每个账户"最近一次持仓批次结束"的时间（pubsub::Position::isLast == true）用途：区分"该腿确实没持仓"和"该腿的推送还没到"。
    std::unordered_map<int, int64_t> m_positionBatchDoneUs;

    // 该账户的持仓批次是否已在本次启动后完整到达过
    bool BatchDoneAfterStart(int accountId) const {
        auto it = m_positionBatchDoneUs.find(accountId);
        return it != m_positionBatchDoneUs.end() && it->second >= m_startupTimeUs;
    }

    bool IsTradingReady() const {
        return m_phase == StartupPhase::Trading;
    }

    // 周期尝试对账；返回 true 表示可以放行
    bool TryReconcile(int64_t nowUs);

    // 对账单个对子（假定两腿推送都已到位）。返回 true 表示该对子已一致
    bool ReconcilePair(PairInfo& pi);

    // 快照里 hasActiveAlgoOrder == true 的启动处理（告警 + 可选冻结），§5.4
    void HandleOrphanAlgoOrders();

    // ---- T_signal：IO 线程分位数计算/分位数落盘/pairinfo定期落盘/配置轮询
    void StartSignalThread();
    void SignalWork();

    // 变更队列（T_signal 生产 / T_market 消费）。元素是整份 RuntimeConfig。
    RuntimeParamQueue m_cfgQ;

    // 把 rc.pairCommands 落到 PairInfo 上，按 cmdType 分三支：
    //   1 STOP     stopFlag = true  （开仓、平仓都不发 = 冻结该对子）
    //   2 CLOSE    closeFlag = true （只平不开）+ 按需写 profitPct / closeRemainVolume
    //   3 RESUME   清两个 flag，closeRemainVolume 回 0，profitPct 回 kDefaultProfitPct
    // **不碰 autoFlag**（那是"参数由谁算"的标签，写成 false 会免掉超时撤单）。
    //
    // **单向**：不记账、不做差集，删掉条目 = 什么都不做，字段一直粘着。
    // 恢复靠 cmdType=3，或人工改快照 + 重启：`stopFlag` 第 28 列、`closeFlag` 第 29 列、
    // `profitPct` 第 30 列；`closeRemainVolume` 不进快照，重启后自动回 0 = 平干净。
    // 为什么不做差集恢复：那要记住"上一版命令了哪些对子"，还要区分"快照带来的 closeFlag"与
    // "命令写的 closeFlag"（否则启动 1 秒就会把人工停机解开）—— 一整套账本 + 基线，
    // 只为一件很少发生的事。
    //
    // 调用点：OnTimer 第 0.6 步，**必须在全局那份 RecalcOrderParams 循环之前** ——
    // 那个循环要用到这里刚写进去的 closeFlag / closeRemainVolume / profitPct。
    // 本函数只写字段、不重算参数、不动 algoModifyTime（都由调用点统一做）。
    void ApplyPairCommands(const RuntimeConfig& rc);

    // 配置文件指纹（T_signal 独占）：inode + mtime(纳秒) + size 三者都没变就不重读。
    // 用纳秒而不是秒级 mtime：脚本/编辑器在同一秒里改两次，秒级精度看不出来。
    // 带上 inode 是为了覆盖"写临时文件 + rename 替换"这种原子写法（inode 会变）。
    struct CfgStamp {
        uint64_t ino{0};
        int64_t mtimeNs{0};
        int64_t size{-1};
        bool valid{false};
    };
    CfgStamp m_cfgStamp;

    // T_signal 的**一轮**：吃样本 -> 到点就 Build + 落盘。SignalWork() 循环调的就是它。
    void RunSignalOnce(int64_t nowUs, int64_t& lastStatsUpdateUs);

    std::thread m_signalThread;
    std::atomic<bool> m_signalRun{false};

    SpreadSampleQueue m_sampleQ;   // T_market -> T_signal（价差样本）
    SpreadStatsQueue  m_statsQ;    // T_signal -> T_market（分位数快照）

    // 每个币对的降频采样计时，**行情线程独占**。
    std::unordered_map<std::string, int64_t> m_lastSampleTs;

    // 每个币对一份 24h 滚动价差样本窗口，**T_signal 独占**（行情线程一行都不碰）。
    std::unordered_map<std::string, SpreadStatsBuilder> m_spreadWindows;

    PairInfoQueue m_snapQ;   // T_market -> T_signal（空 pairKey = 一批结束）
    std::vector<PairInfoRecord> m_snapScratch; // 交易线程独占：收行的暂存（resize 一次，之后零分配）
    std::vector<PairInfoRecord> m_snapWrite;   // T_signal 独占：攒批的暂存

    // 从 OnSpread 的原始行情里抽一条价差样本，按 spreadSampleIntervalSec 降频后非阻塞投给 T_signal
    void AccumulateSpreadSample(const std::string& pairKey, const dbp::DbpData* pdata);

    void ProcessPairSignal(PairInfo& pi);

    void ProcessRisk(PairInfo& pi, int64_t nowUs);

    // 每 tick 对**每个**对子都算一次，与是否已有算法单无关；条件成立就把 pi.satisfyTime 刷成 nowUs。
    // 于是 satisfyTime 的含义是"最后一次机会成立的时间"，CheckAlgoOrderTimeout 用它算"连续不成立"的时长。
    void UpdateSatisfyTime(PairInfo& pi, const SignalResult& sig, bool canOpen, bool canClose) const;

    // 撤单触发①：机会超时，手动单不撤
    // nowUs - satisfyTime > algoOrderTimeoutMs 时请求撤单。
    void CheckAlgoOrderTimeout(const PairInfo& pi, int64_t nowUs) const;

    // 双腿都有实盘持仓、且净敞口超过 exposureCancelTimes * ttTargetVolume 时，置 pi.stopFlag 停止该对子的一切报单，并撤掉在跑的算法单。
    // one-shot：pi.stopFlag 已为 true 时直接返回，不重复告警/重复撤单（见 .cpp 里的实现注释）。
    void CheckExposureAbnormal(PairInfo& pi) const;

    // 改参触发（祖先 :1059-1078）：每 modifyTimespanSec 秒把最新参数推给在跑的算法单。
    // 流动性危险（liquid_status == 2）时用 BuildCloseModify（更激进的平仓），否则用 BuildNormalModify。
    void ProcessModify(PairInfo& pi, int64_t nowUs) const;

    // 按 pair_info 的最新参数生成整份 42 字段快照。
    static stra::AlgoOrderModify BuildNormalModify(const PairInfo& pi);

    // 流动性危险时"更激进的平仓"——关掉全部开仓开关、关掉盈利保护，平仓价差朝更容易成交的方向让 shiftPct。
    static stra::AlgoOrderModify BuildCloseModify(const PairInfo& pi, double shiftPct);

    // 请求执行端撤销该对子当前的算法单。撤单是异步的，调用方不能假设它同步生效。
    void RequestCancelAlgoOrder(const PairInfo& pi) const;

    // direction : OL/OS/CL/CS;   algoMode: TT/MT
    // role : 建单角色，写进算法单后只读，只影响"平到 0 算不算结束"（见 BaseAlgoOrder.h 的 AlgoRole）。
    //        **刻意不给默认值** —— 漏传就编译不过，比默默把平仓单当开仓单强。
    // 报单成功时会把 pi.satisfyTime 与 pi.algoModifyTime 一起刷成当前时刻（所以形参是非 const 引用）：
    void SubmitAlgoOrder(PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit, AlgoRole role) const;

    BaseAlgoOrder* BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit, AlgoRole role) const;

    std::string baseAsset{"USDT"};

    sm::SecurityManager* smc{nullptr};

    // 由 Init 注册，析构时注销；供执行端同线程直接回传算法单更新
    static PairTradingContext* s_instance;
};

}