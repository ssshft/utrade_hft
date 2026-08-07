#pragma once

#include "base/base_strategy.h"
#include "algo/AlgoContext.h"
#include "algo/PairTradingContext.h"

class PairTradingStrategy : public BaseStrategy {
public:
    PairTradingStrategy();
    ~PairTradingStrategy();

    void pre_start(Config* config);

    void pre_stop();

protected:
    void on_command(const std::string& cmdStr);

    void on_timer(const long& utcTime);

    void on_marketdata(md::CryptoMarketData& cmd);

    void on_ordertrade(om::OrderTrade& orderTrade);

    void on_balance(om::Balance& balance);
    
    void on_position(om::Position& position);

    void on_total_account(om::TotalAccount& totalAccount);

    void on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum);

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