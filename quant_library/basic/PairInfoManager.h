/****
 * 
 * 管理所有配对交易对的运行状态
 * 1. 初始化所有对子的PairInfo
 * 2. 从on_position, on_balance更新持仓/pnl
 * 3. 从实时价差更新 rtSpread 字段
 * 4. 从统计结果更新 largeStats smallStats
 * 5. 计算报单参数 maxVolume/ttTargetVolume等
 * 6. 快照 SaveSnapshot / LoadSnapshot，支持重启继续运行
 *    （只存"无法自己恢复"的字段，账户真值等推送自愈）
 * ***/

 #pragma once

 #include "PairInfo.h"
 #include "DataStruct.h"
 #include "dbp/include.h"
 #include "securitymanager.h"

 namespace pt {

    bool WriteSpreadStatsFile(const std::string& path, const std::vector<SpreadStatsMsg>& v);

    // 把 PairInfoRecord 列表按 33 列格式化成 CSV，写 .tmp 再 rename 原子替换。
    // 空列表 ⇒ 写"只有表头"的文件（与老 SaveSnapshot 行为一致）。
    // 纯 IO + 格式化，不碰 PairInfo —— 可以在任何线程调（生产就在 T_signal 上调）。
    bool WritePairInfoRecordFile(const std::string& path, const std::vector<PairInfoRecord>& v);

    class PairInfoManager {
    public:
        static PairInfoManager& Instance() {
            static PairInfoManager inst;
            return inst;
        }

        void Init(const std::vector<std::string>& pairKeys, int activeAccountId, int passiveAccountId, sm::SecurityManager* s);

        // 快照：只存"无法自己恢复"的字段（见 docs/restart_recovery_design.md §5.1.1）。
        // 账户真值（持仓/均价/浮盈/强平价/标记价/ADL/强平状态）一律不存 —— 由推送自愈。
        // SaveSnapshot 写临时文件 + rename 原子替换，避免崩在写一半留下半截文件。
        // 它 = CollectSnapshotRows + WritePairInfoRecordFile，是**同步参考实现**：
        // 生产走"交易线程收行 -> T_signal 写文件"，这条留给 pre_stop 和测试。
        bool SaveSnapshot(const std::string& path);

        // 按 m_pairKeys 的顺序收行（顺序 = 文件行序，必须稳定），返回行数。
        // **只读 PairInfo，必须在交易线程调用**（别的线程读 PairInfo 就是数据竞争）。
        int CollectSnapshotRows(std::vector<PairInfoRecord>& out);

        // 返回实际恢复的对子数；<0 表示失败（文件不存在 / 解析失败）
        int LoadSnapshot(const std::string& path);

        int LoadSpreadStats(const std::string& path, int maxStaleSec);

        PairInfo* GetPairInfo(const std::string& pairKey);
        PairInfo* GetPairInfo(const char* pairKey);

        const std::vector<std::string>& GetAllPairKeys() const {
            return m_pairKeys;
        }

        std::vector<PairInfo*> GetAllPairInfos();

        // 所有对子持仓市值之和：Σ |pairTotalVolume × 腿价 × 面值|（逐对子**现算**）。
        // 这是"全局持仓闸门"的输入（祖先 cc_pricespread_gb_ltp.py:825 的 position_value_all）。
        //
        // 为什么放在这里而不是调用方自己遍历：
        //   ① m_pairInfoMap 只有本类能直接遍历，走 GetAllPairInfos() 每次要拷一份
        //      vector<PairInfo*>（500 对子实测 5.4 µs，见 docs 执行线程清单 B4）；
        //   ② ⚠️ 不能用缓存的 pi.positionValue —— 它只在 UpdateOnAlgoOrder 里刷
        //      （PairInfoManager.cpp:800），两次成交之间价格变动不会更新，
        //      拿它当风控输入会滞后。这里现算，成本只是一次乘法。
        // **只读 PairInfo，必须在交易线程调用。**
        double TotalPositionValue() const;

        void UpdateRtSpread(const std::string& pairKey, const dbp::DbpData* pdata);

        // 更新大周期价差统计
        void UpdateLargeStats(const std::string& pairKey, const SpreadStats& stats);

        // 更新小周期价差统计
        void UpdateSmallStats(const std::string& pairKey, const SpreadStats& stats);

        void UpdateOnPosition(const pubsub::Position& pos);

        void UpdateOnBalance(const pubsub::Balance& balance, const std::string& baseAsset);

        void UpdateOnTotalAccount(const pubsub::TotalAccount& totalAccount);

        // 算法单同步：执行端是唯一数据源，量 / 价直接覆盖（不做策略侧加权混合）
        void UpdateOnAlgoOrder(const std::string& pairKey, double volume, double activePrice, double passivePrice);

        void SetActiveAlgoOrder(const std::string& pairKey, int64_t algoOrderId);

        void ClearActiveAlgoOrder(const std::string& pairKey);


        // 仓位量参数计算 --- 根据K线统计与配置参数 计算各对子的报单量参数
        void RecalcVolumeParams(double maxAmount, double targetAmount, double exposureMaxLimit, double exposureMaxLimitCoff);

        // k线统计更新
        void UpdateKlineStats(const std::string& instrumentKey, double dailyAmount, double meanClose, double oi, double oiUsdt);

        // 流动性检查 --- 强平价格监控
        void UpdateLiquidStatus(const pubsub::Position& pos);

        void UpdateLiquidStatus(PairInfo& pi, bool isActive, const pubsub::Position& pos);

        void ResetAbnormalCloseState(const std::string& pairKey, AbnormalCloseType type);

        void ApplyCommand(const std::string& pairKey, PairCommandType cmd, const std::unordered_map<std::string, double>& params = {});

        double ceil2min(double val, double minUnit);

    private:
        PairInfoManager() = default;
        ~PairInfoManager() = default;

        PairInfoManager(const PairInfoManager&) = delete;
        PairInfoManager& operator=(const PairInfoManager&) = delete;

        std::unordered_map<std::string, PairInfo> m_pairInfoMap;
        std::unordered_map<std::string, std::vector<std::string>> m_instrToPairs;
        std::vector<std::string> m_pairKeys;

        void RegisterInstrument(const std::string& instrKey, const std::string& pairKey);

        const std::vector<std::string>* FindPairsByInstrument(const std::string& instrKey) const;

        static void ParseInstrumentKey(const char* key, char* exchange, size_t exchLen, char* instType, size_t instTypeLen, char* symbol, size_t symLen);

        static constexpr double MIN_ORDER_USDT = 25;

        sm::SecurityManager* smc{nullptr};

    };
 }