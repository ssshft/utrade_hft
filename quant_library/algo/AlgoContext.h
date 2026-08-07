#ifndef _ALGO_CONTEXT_h
#define _ALGO_CONTEXT_h

#include "basic/AlgoOrderManager.h"
#include "basic/QuantTrade.h"
#include "basic/QuantDbp.h"
#include "basic/QuantPub.h"
#include "basic/Utility.h"
#include "command_helper.h"
#include "redis_client.h"
#include "program_util.h"



class AlgoContext {
public:
    AlgoContext();
    ~AlgoContext();
    void PreStart();
    void SetTradeClient(om::TradeClient* client);
    void SetDbp(dbp::DbpReader* dbp);
    void SetPub(RedisClient* redisClient);
    void QueryAccount();
    void OnCommand(string s);
    void OnMarketDepth();
    void OnMarketTrade();
    void OnSpread(const stra::MdSpread& spread, int64_t eventTime);
    void OnSpreadTrade(const stra::QuantSpread& quantSpread, int64_t eventTime);
    void OnTimerTrade(int64_t eventTime);
    void OnOrder(stra::TdOrder& order, int64_t eventTime);
    void OnPosition(const stra::TdPosition& position, int64_t eventTime);
    void OnKline();
    void OnFundingRate();
    void OnTimer(int64_t eventTime);
    void OnBalance(const stra::TdBalance& balance);
    void OnPosition(const stra::TdPosition& position);
    void OnTotalAccount(const stra::TdTotalAccount& totalAccount);

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
    
};

#endif
