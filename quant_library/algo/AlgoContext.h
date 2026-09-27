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

    // 直接注册策略层创建好的算法单对象（不再走 JSON 字符串）：Init / 插入 algoOrderManager / 落库 / 订阅价差
    void SubmitAlgoOrder(BaseAlgoOrder* pAlgoOrder);

    // 算法单变更：按 algoOrderId 定位"已经在册"的算法单，就地改状态 / 参数。
    // 与上面那个重载的本质区别：不新建、不插入 algoOrderManager、不 Init、不订阅。
    //   CommandType_CANCEL : 置 ALGO_OS_CANCELLING —— 这是撤单链路唯一的触发点。
    //       之后 OnSpread 会停止为该单报新的子单，并由 BaseAlgoOrder::CancelOrderOnSpread 的
    //       CANCELLING 分支撤掉主动腿在途子单（被动腿刻意留着等成交，语义见该分支的注释），
    //       子单清零后 OnTimer 推进到 ALGO_OS_CANCELED，再由 NotifyAlgoOrderUpdate
    //       回传策略层释放对子。
    //   CommandType_MODIFY : 用 modify 的整份快照覆盖算法单参数（祖先 create_modify_dict 语义）。
    // 幂等：已在撤单流程或已终结的算法单会被跳过。
    void SubmitAlgoOrder(int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify = nullptr);

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
