#pragma once

#include "base/base_strategy.h"
#include "algo/AlgoContext.h"
#include "algo/PairTradingContext.h"

class PairTradingStrategy : public BaseStrategy {
public:
    PairTradingStrategy();
    ~PairTradingStrategy();

    void pre_start(Config* config) override;

    void pre_stop() override;

protected:
    void on_command(const std::string& cmdStr) override;

    void on_timer(const long& utcTime) override;

    void on_marketdata(md::CryptoMarketData& cmd) override;

    void on_ordertrade(om::OrderTrade& orderTrade) override;

    void on_balance(om::Balance& balance) override;
    
    void on_position(om::Position& position) override;

    void on_total_account(om::TotalAccount& totalAccount) override;

    void on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum) override;

private:
    AlgoContext algoContext;

    pt::PairTradingContext ptContext;

    void SubmitAlgoCommand(const std::string& json); // 策略层构建的json指令转发给AlgoContext

    void ScanFinishedAlgoOrders(int64_t nowUs);

    static stra::TdPosition ConvertPosition(const om::Position& pos);

    static stra::TdBalance ConvertBalance(const om::Balance& bal);

    static stra::TdTotalAccount ConvertTotalAccount(const om::TotalAccount& ta);

    static stra::TdOrder ConvertOrderTrade(const om::OrderTrade& ot);

    pt::PairTradingConfig m_ptCfg;

    int64_t m_lastScanUs{0};
    static constexpr int64_t SCAN_INTERVAL_US = 200000LL; // 200ms
};