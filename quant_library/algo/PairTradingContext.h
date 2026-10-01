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
    // 对子清单。**平铺**在 op 段（`op.pairKeys`），不在 `op.pairTrading` 里 ——
    // 因为 pre_start 是边遍历它边订阅行情的，跟纯参数不是一类东西。
    std::vector<std::string> pairKeys;

    // 账户。同样平铺在 op 段（`op.activeAccountId` / `op.passiveAccountId`）。
    int activeAccountId{0};
    int passiveAccountId{0};

    // 报单额。平铺在 op 段（`op.maxPositionValue` / `op.maxAmount` / `op.targetAmount`），
    // 单位是 **USDT 名义价值**，不是币数 —— 换算成币数在
    // PairInfoManager::RecalcVolumeParams（除以腿价再 ceil 到 minVolume）。
    double maxPositionValue{100}; // 总持仓上限
    double maxAmount{50};
    double targetAmount{50};

    // -----------------------------------------------------------------------
    // 两个"参数块"。挂在 op 段下的**独立子对象**里，解析全在
    // src/strategy/PairTradingStrategy.cpp 的 pre_start 里用 rapidjson 逐字段读
    // （每个 key 先 HasMember 再赋值：配置里没写的保持结构体默认值）。
    //
    //   op.feeSlippage -> feeSlippage  执行成本 / 滑点 / 价差阈值
    //                    解析后推给 SignalGenerator::SetConfig（信号侧与算法单侧共用）
    //   op.risk        -> risk         风控门槛 + 渐进式平仓三档
    //                    解析后推给 RiskManager::SetConfig
    //
    // 为什么这两个也放进来：这两个单例（SignalGenerator / RiskManager）是跨组件
    // 使用的，各自必须持有一份自己的配置副本；放在这里是为了让"本次启动实际生效
    // 的全部参数"集中在一处，便于日志打印与核对。
    //
    // 算法单参数（报单类型 / 撤单门槛 / rebalance …）**不单独建结构体**，
    // 就是下面 `op.pairTrading` 段里的普通字段，由 BuildAlgoOrderJson 直接读。
    // -----------------------------------------------------------------------
    FeeSlippageConfig feeSlippage;
    RiskConfig       risk;

    // ---- 以下全部走 `op.pairTrading` 段 ----

    // ---- 腿报单类型（OrderType 枚举，配置里写字符串）----
    // 判据落在**子单**的 orderType 上（BaseAlgoOrder.cpp:457 的
    // `it->second.orderType == OT_POST_ONLY` 决定走 Maker 还是 Taker 撤单分支），
    // 子单类型直接取自这里（AlgoPairOrder.cpp:321-322 / 388-389 / 439-440 / 503-504）。
    //   主动腿：TT 用 OT_LIMIT（穿价限价，效果是吃单但把最差成交价钉住）
    //           MT 用 OT_POST_ONLY（必须挂单，否则会被交易所当吃单拒掉）
    //   被动腿：恒 OT_LIMIT
    // ⚠️ 2026-10-01 改过（4f3e2a5）：原来是 TT=OT_MARKET / MT=OT_LIMIT / 被动=OT_MARKET。
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
    // targetSpreadType = NOW：按实时价差算目标，不用均值（NOW_MEAN 是另一种口径）
    // activeVolumeCalcualteType = PassiveVolumePct：主动腿量由被动腿量 × passiveVolumePct 推
    stra::TargetSpredPrice targetSpreadType{stra::TargetSpredPrice_NOW};
    stra::ActiveVolumeCalcualteType activeVolumeCalcualteType{
        stra::ActiveVolumeCalcualteType_PassiveVolumePct};

    // ---- rebalance（主动腿成交后是否用 rebalance 模式补被动腿）----
    // ⚠️ 2026-10-01 改过（4f3e2a5）：TT 的两个由 true 改成 false，MT 两个保持 true。
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

    // 价差统计分位数（祖先 quantile_up=0.92 / quantile_dn=1-quantile_up=0.08）
    // 分位数越极端 -> 分位数边界越靠外 -> 入口阈值越宽 -> 开平机会越多
    double quantileUp{0.92};
    double quantileDn{0.08};

    // ---- 价差统计生产者（对齐祖先 pair_trading_c_gateio）----
    int spreadStatsWindowSec{86400};       // 24h 滚动窗口（祖先 spread_df_update_period）
    int spreadStatsUpdateIntervalSec{60};  // 60s 刷新统计（祖先 spread_df_update_timespan，原值 3600）
    int spreadStatsMinSamples{5};       // 样本数门槛（祖先 spread_count > 24*3600/5*0.5）
    int spreadSampleIntervalMs{5000};       // 采样间隔 5s，控制内存；0 = 不降频（逐 tick 全存）
    int spreadFreshnessSec{30};            // 行情新鲜度门槛（祖先 lastGenerateTs < 30s）

    // 算法单机会超时ms（祖先 algo_order_cancel_time = 150s）。
    // 语义是"机会连续不成立的时长"，不是订单年龄：机会条件一成立就刷新 satisfyTime，
    // 所以只有连续不成立超过这个时长才会撤单。见 UpdateSatisfyTime / CheckAlgoOrderTimeout
    int64_t algoOrderTimeoutMs{150000};

    // 敞口异常倍数（祖先 :1081 的 4 * ttTargetVolume）：
    // 双腿净敞口 / activeMultiple 超过 exposureCancelTimes * ttTargetVolume 即视为敞口失控
    double exposureCancelTimes{4.0};

    // 改参周期（祖先 modify_timespan = 60s）：每隔这么久把 pair_info 的最新参数推给在跑的算法单
    int64_t modifyTimespanSec{60};

    // 流动性危险时的平仓让利（祖先 modify_shift_pct = 0.0002）：
    // CL 的起止价差各减这么多、CS 的各加这么多，让平仓更容易成交
    double modifyShiftPct{0.0002};

    // 快照路径。平铺在 op 段（`op.csvStatePath`）。
    std::string csvStatePath{"data/pair_info.csv"};

    int volumeRecalcIntervalSec{60};     // 1min 重算仓位参数
    int csvSaveIntervalSec{10};        // 快照落盘周期（原 300s；只有几十行，收紧以缩小崩溃窗口）

    // ---- 启动对账（docs/restart_recovery_design.md §5.2）----
    // 对账判据容差：|pairTotalVolume - activeRealPosition| <= max(1e-9, |activeRealPosition| * ratio)
    double reconcileTolRatio{1e-6};
    // 等持仓推送的超时告警周期。超时只告警、**不放行** —— 按"无持仓"放行的后果是重复开仓
    int reconcileWarnIntervalSec{30};
    // 快照里 hasActiveAlgoOrder == true（上一轮有算法单未终结）时，把该对子置 errorFlag 冻结，
    // 等运维 PairCmd_RESUME 复活。交易所侧可能仍有子单在成交，账本不可信（§5.4）。
    bool freezeOnOrphanAlgoOrder{true};
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

    // ---- 启动闸门（§5.2）----
    // Init 读完快照后停在 Reconciling：五个交易入口全部直接 return，
    // 等两腿 pubsub::Position 推送到位、对账通过，才进 Trading。
    // 之所以需要它：价差推送通常早于持仓推送，而 CanOpen 没有持仓门槛、
    // CanClose 又要求 HasPosition —— 账本不对就会"在孤儿仓上再开一笔"或"平仓单变反向开仓"。
    enum class StartupPhase {
        Reconciling = 0,   // 等推送 + 对账，交易冻结
        Trading = 1        // 账本可信，放行
    };
    StartupPhase m_phase{StartupPhase::Reconciling};
    int64_t m_startupTimeUs{0};
    int64_t m_lastReconcileWarnUs{0};

    // 每个账户"最近一次持仓批次结束"的时间（pubsub::Position::isLast == true）。
    // 用途：区分"该腿确实没持仓"和"该腿的推送还没到"。
    // ⚠️ 必须按 accountId 分开记：isLast 是**单账户单次应答**的批次尾标记
    //    （tb 适配器写成 `isLast = (i + 1 == pending.size())`），
    //    用全局一个标记会把"主动腿账户的批次到了"误当成"被动腿也到了"。
    // Init 时是空的，所以任何一条记录必然是本次启动之后收到的。
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
    void UpdateSatisfyTime(PairInfo& pi, const SignalResult& sig, bool canOpen, bool canClose) const;

    // 撤单触发①：机会超时（祖先 :1021）。
    // nowUs - satisfyTime > algoOrderTimeoutMs 时请求撤单。
    // 只有手动单不撤（autoFlag == false，祖先 :1018-1020）。
    // 风控强平单**同样受这条约束**（2026-09-27 确认）：强平单长时间不成交也要撤掉、
    // 下一轮按新档位重报 —— 撤单 -> OnAlgoFinished -> tierNTimes++ 是档位升级的唯一驱动，
    // 豁免它等于把档位永远锁在 tier1。
    void CheckAlgoOrderTimeout(const PairInfo& pi, int64_t nowUs) const;

    // 撤单触发⑤：敞口异常（祖先 :1081-1092）。
    // 双腿都有实盘持仓、且净敞口超过 exposureCancelTimes * ttTargetVolume 时，
    // 置 pi.errorFlag 停止该对子的一切报单，并撤掉在跑的算法单（祖先置 status=ERROR，留人工处理）。
    void CheckExposureAbnormal(PairInfo& pi) const;

    // 改参触发（祖先 :1059-1078）：每 modifyTimespanSec 秒把最新参数推给在跑的算法单。
    // 流动性危险（liquid_status == 2）时用 BuildCloseModify（更激进的平仓），否则用 BuildNormalModify。
    // 计时靠 pi.algoModifyTime，由 SubmitAlgoOrder 在报单时重置。
    // ⚠️ 不能用 PairInfo::modifyTime：那个字段被 PairInfoManager 在 UpdateRtSpread 等七处
    //    刷新（每个价差 tick 一次），拿它计时这个判断永远不成立。
    void ProcessModify(PairInfo& pi, int64_t nowUs) const;

    // 祖先 algo_order_manager.py:211-252 create_modify_dict 的等价物：
    // 按 pair_info 的最新参数生成整份 42 字段快照。
    // ⚠️ 祖先那个函数**只带 6 个开关**（profitSwitch/profitPct/ttOL/ttOS/mtOL/mtOS），
    //    故意不带 ttCL/ttCS/mtCL/mtCS —— 因为 AEC 侧是"缺字段就保持原值"的部分更新。
    //    C++ 的 MODIFY 分支是整份覆盖，所以这里必须把 8 个开关**全部**带上，
    //    否则平仓开关会被默认值 false 清零，等于关掉平仓能力。
    static stra::AlgoOrderModify BuildNormalModify(const PairInfo& pi);

    // 祖先 algo_order_manager.py:254-298 create_close_modify_dict 的等价物：
    // 流动性危险时"更激进的平仓"——关掉全部开仓开关、关掉盈利保护，
    // 平仓价差朝更容易成交的方向让 shiftPct。
    static stra::AlgoOrderModify BuildCloseModify(const PairInfo& pi, double shiftPct);

    // 请求执行端撤销该对子当前的算法单。幂等：对子没有活跃算法单、或算法单已在撤单流程里，
    // 都会被忽略。撤单是异步的（撤主动腿报价 -> 等被动腿成交把敞口平掉 -> 子单清零
    // -> ALGO_OS_CANCELED -> 回传释放对子），调用方不能假设它同步生效。
    void RequestCancelAlgoOrder(const PairInfo& pi) const;

    // direction : OL/OS/CL/CS;   algoMode: TT/MT
    // 报单成功时会把 pi.satisfyTime 与 pi.algoModifyTime 一起刷成当前时刻（所以形参是非 const 引用）：
    //   satisfyTime    —— 机会超时（CheckAlgoOrderTimeout）从报单起算。祖先的报单只可能发生在
    //       机会成立的那一 tick，所以刷新它不改变语义；不刷的话算法单会因为时间戳陈旧
    //       被"机会超时"立刻撤掉。风控强平单的 150s 窗口也靠这里起算：强平是在机会不成立时
    //       发出去的，之后 satisfyTime 刷不动，正好 150s 后超时撤单、下一轮按新档位重报。
    //   algoModifyTime —— 改参（ProcessModify）从报单起算。报单时参数已经推给算法单了，
    //       字段语义就是"最后一次把参数推给这个对子"。
    void SubmitAlgoOrder(PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit = 0.0) const;

    // 函数名沿用旧名，但已不再拼 JSON：直接创建算法单对象并返回，创建失败（开关关闭/报单量非法）返回 nullptr
    BaseAlgoOrder* BuildAlgoOrderJson(const PairInfo& pi, const std::string& algoMode, const std::string& direction, double forgoProfit) const;

    std::string baseAsset{"USDT"};

    sm::SecurityManager* smc{nullptr};

    // 由 Init 注册，析构时注销；供执行端同线程直接回传算法单更新
    static PairTradingContext* s_instance;
};

}