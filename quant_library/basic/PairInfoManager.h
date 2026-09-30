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
        bool SaveSnapshot(const std::string& path);

        // 返回实际恢复的对子数；<0 表示失败（文件不存在 / 解析失败）
        int LoadSnapshot(const std::string& path);

        PairInfo* GetPairInfo(const std::string& pairKey);
        PairInfo* GetPairInfo(const char* pairKey);

        const std::vector<std::string>& GetAllPairKeys() const {
            return m_pairKeys;
        }

        std::vector<PairInfo*> GetAllPairInfos();

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