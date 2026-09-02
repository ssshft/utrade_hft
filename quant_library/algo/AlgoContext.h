#ifndef _ALGO_CONTEXT_h
#define _ALGO_CONTEXT_h

#include "basic/AlgoOrderManager.h"
#include "basic/QuantTrade.h"
#include "basic/QuantDbp.h"
#include "basic/QuantPub.h"
#include "basic/Utility.h"
#include "command_helper.h"
#include "securitymanager.h"
#include "program_util.h"
#include "basic/DataStruct.h"



class AlgoContext {
public:
    AlgoContext();
    ~AlgoContext();
    void PreStart();
    void Init(sm::SecurityManager* s);
    void SetTradeClient(om::TradeClient* client);
    void SetDbp(dbp::DbpReader* dbp);
    //void SetPub(RedisClient* redisClient);
    void QueryAccount();
    void OnCommand(string s);
    void OnMarketDepth();
    void OnMarketTrade();

    void OnKline();
    void OnFundingRate();

    void OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata);
    void OnTimer(int64_t eventTime);
    void OnBalance(const pubsub::Balance& balance);
    void OnPosition(const pubsub::Position& position);
    void OnTotalAccount(const pubsub::TotalAccount& totalAccount);
    void OnOrder(const pubsub::OrderResponse& orderResponse);

    BaseAlgoOrder* GetAlgoOrder(int64_t algoOrderId);

private:
    AlgoOrderManager alogOrderManager;
    double pendToPendingTimeSpan; // 根据on_order进行更新
    double PendingToNewTimeSpan; // 根据on_order进行更新
    double cancelToCancellingTimeSpan; // 根据on_order进行更新
    double CancellingtoCanceledTimeSpan; // 根据on_order进行更新
    double realLeverage; //根据accountMgr更新
    bool isreal; // 是否实盘
    
    int rebalanceCount; // rebalanceFlag计数 
    int delayCount;
    int slippageCount;
    int spreadCount;
    int stuckOrderReportCount;
    int errorOrderReportCount;
    int spreadReportCount;
    int infoReportCount;
    int algoOrderReportCount;
    int queryAccountCount;
    int fundVerifyCount;

    unordered_map<string, int> mSpreadReportCount;
    
    string utrade2SccChannel;

    int64_t curSpreadDelay;
    int64_t curSpreadDepthDelay;
    int64_t curSpreadTradesDelay;
    int64_t lastAlgoUpdateTime;
    int64_t tradesDelayThreshold;

    int onTimerTrade;

    sm::SecurityManager* smc{nullptr};
    
};

#endif
