#pragma once
#include "base/base_strategy.h"
#include "algo/AlgoContext.h"


class AlgoPairStrategy : public BaseStrategy {
public:
    AlgoPairStrategy();
    ~AlgoPairStrategy();
    void pre_start(Config* config);
    void pre_stop();

protected:
    //中控aec控制命令到达
    void on_command(const string &cmdStr);
    //指定间隔触发一次
    void on_timer(const long &utcTime);

    //行情到达触发
    // void on_marketdata(md::CryptoMarketData &cmd);

    //资金推送
    void on_balance(pubsub::Balance &balance);

    //仓位推送
    void on_position(pubsub::Position &position);

    //账户总览推送
    void on_total_account(pubsub::TotalAccount &totalAccount);

    //订单，成交推送
    void on_ordertrade(pubsub::OrderResponse& orderResponse);

    void on_dbpdata(const dbp::DbpTopic* topic,const dbp::DbpData* pdata, uint32_t jumpedNum);


    AlgoContext context;
};
