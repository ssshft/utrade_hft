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

    void on_timer(const int64_t& utcTime);

    void on_marketdata(md::CryptoMarketData& cmd);

    void on_balance(pubsub::Balance& balance);

    //仓位推送
    void on_position(pubsub::Position& position);

    //账户总览推送
    void on_total_account(pubsub::TotalAccount& totalAccount);

    //订单，成交推送
    void on_ordertrade(pubsub::OrderResponse& orderResponse);

    void on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum);

private:
    AlgoContext algoContext;

    pt::PairTradingContext ptContext;

    void SubmitAlgoCommand(const std::string& json); // 策略层构建的json指令转发给AlgoContext

    void ScanFinishedAlgoOrders(int64_t nowUs);
    
    pt::PairTradingConfig m_ptCfg;

    int64_t m_lastScanUs{0};
    static constexpr int64_t SCAN_INTERVAL_US = 200000LL; // 200ms
};