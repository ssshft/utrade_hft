#include "strategy/AlgoPairStrategy.h"


AlgoPairStrategy::AlgoPairStrategy(){
    context.SetTradeClient(tradeClient);
}

AlgoPairStrategy::~AlgoPairStrategy(){

}

void AlgoPairStrategy::pre_start(Config *config){
    _init(config);
    std::string configStr = config->get_document_str();
    rapidjson::Document d;
    rapidjson::Value &configValue = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(configStr.c_str());

    for(rapidjson::SizeType i = 0; i < configValue["dbp"]["topics"].Size(); i++){
        auto channel = configValue["dbp"]["topics"][i].GetString();
        dbpreader->Subscribe(channel);
    }

    context.SetDbp(dbpreader);
    context.SetPub(redisClient);
    context.PreStart();
}

//中控aec控制命令到达
void AlgoPairStrategy::on_command(const string &cmdStr){
    //策略需要加上这一个行数
    _my_on_command(cmdStr);
    //策略这里自己处理cmd 命令
    context.OnCommand(cmdStr);
}

void AlgoPairStrategy::on_timer(const long &utcTime){
    //DEBUGLINE
    int64_t currentTime = GetCurrentTimeUs();
    context.OnTimer(currentTime);
}

//资金推送
void AlgoPairStrategy::on_balance(pubsub::Balance &balance){
    //DEBUGLINE
    int64_t currentTime = GetCurrentTimeUs();
    stra::TdBalance ba;
    ba.accountId = mAccountNameAccountId[balance.accountId];
    ba.exchangType = stra::ExchangeType(balance.exchangeTypeEnum);
    ba.instType = stra::InstType(balance.instTypeEnum);
    strncpy(ba.strategyId, balance.strategyId, stra::ID_LEN);
    strncpy(ba.currency, balance.currency, stra::ASSET_LEN);
    ba.total = balance.total;
    ba.available = balance.available;
    ba.frozen = balance.frozen;
    ba.updateTime = balance.updateTime;
    context.OnBalance(ba);
}

//仓位推送
void AlgoPairStrategy::on_position(pubsub::Position &position){
    //DEBUGLINE 
    int64_t currentTime = GetCurrentTimeUs();
    stra::TdPosition pos;
    pos.accountId = mAccountNameAccountId[position.accountId];
    pos.exchangType = stra::ExchangeType(position.exchangeTypeEnum);
    pos.instType = stra::InstType(position.instTypeEnum);
    strncpy(pos.strategyId, position.strategyId, stra::ID_LEN);
    strncpy(pos.instrument, position.instId, stra::INST_ID_LEN);
    pos.direction = stra::Direction(position.direction);
    pos.volume = position.volume;
    pos.maintMargin = position.maintMargin;
    pos.avgPrice = position.avgPrice;
    pos.unrealizedPnl = position.unrealizedPnl;
    pos.liquidPrice = position.liquidPrice;
    pos.adlQuantile = position.adlQuantile;
    pos.markPrice = position.markPrice;
    pos.updateTime = position.updateTime;
    context.OnPosition(pos);
}

//账户总览推送
void AlgoPairStrategy::on_total_account(pubsub::TotalAccount &totalAccount) {
    //DEBUGLINE
    int64_t currentTime = GetCurrentTimeUs();
    stra::TdTotalAccount ta;
    ta.accountId = mAccountNameAccountId[totalAccount.accountId];
    ta.exchangType = stra::ExchangeType(totalAccount.exchangeTypeEnum);
    ta.instType = stra::InstType(totalAccount.instTypeEnum);
    strncpy(ta.strategyId, totalAccount.strategyId, stra::ID_LEN);
    ta.totalEquity = totalAccount.totalEquity;
    ta.adjEquity = totalAccount.adjEquity;
    ta.mmr = totalAccount.mmr;
    ta.mgnRatio = totalAccount.mgnRatio;
    ta.updateTime = totalAccount.updateTime;
    context.OnTotalAccount(ta);
}

// rest，报单撤单返回的回报
//订单成交变化
void AlgoPairStrategy::on_ordertrade(pubsub::OrderResponse& orderResponse){
    /*
    int64_t currentTime = GetCurrentTimeUs();
    stra::TdOrder order;

    vector<string> v;
    splitString(orderTrade.strategyRef, v, "_");
    if (v.size() >= 2) {
        order.algoId = stoll(v[0]);
        order.pairId = stoll(v[1]);
    }

    // int64_t sysOrdId{0};
    order.clOrdId = orderTrade.clientOrderId;
    strncpy(order.exOrdId, orderTrade.orderId, stra::ID_LEN);
    strncpy(order.instrument, orderTrade.instId, stra::INST_ID_LEN);
    order.exchangType = stra::ExchangeType(orderTrade.exchangeTypeEnum);
    order.instType = stra::InstType(orderTrade.instTypeEnum);
    order.orderStatus = stra::OrderStatus(orderTrade.orderStatus);
    order.posDirection = stra::PosDirection(orderTrade.offsetFlag);
    order.direction = stra::Direction(orderTrade.direction);
    order.orderType = stra::OrderType(orderTrade.orderType);
    order.reduceOnly = orderTrade.reduceOnly;
    order.price = orderTrade.limitPrice;
    order.volume = orderTrade.volumeTotal;
    order.avgPrice = orderTrade.tradePrice;
    order.totalPriceOnOrder = orderTrade.tradePrice;
    order.totalVolumeOnOrder = orderTrade.volumeTraded;
    // order.lastExecutedPriceOnOrder = orderTrade.tradePrice;
    order.lastExecutedVolumeOnOrder = orderTrade.tradedDiff;
    order.errorId = orderTrade.ErrorID;
    strncpy(order.originErrorMsg, orderTrade.originMsg, 128);
    order.insertTime = orderTrade.insertTime;
    order.updateTime = orderTrade.updateTime;
    order.tsSend = orderTrade.tsSent;
    order.tsNet = orderTrade.tsNet;
    order.apiSource = stra::ApiSource(orderTrade.apiSourceEnum);
    order.isMaker = orderTrade.isMaker;

    context.OnOrder(order, currentTime);
    */
}

void AlgoPairStrategy::pre_stop(){

}

void AlgoPairStrategy::on_dbpdata(const dbp::DbpTopic* topic,const dbp::DbpData* pdata,uint32_t jumpedNum)
{
    // std::cout << "pairInstrumentKey:  " << topic->__name << std::endl;
    // return;
    static uint64_t count = 0;
    auto& data = *pdata;

    //std::cout << topic->__name << "spreadBidAsk(openLong):" << data.spreadBidAsk << "  spreadAskBid(openShort):" << data.spreadAskBid << "passiveAskPrice1:" << data.passiveAskPrice[0] << " passiveBidPrice1:" << data.passiveBidPrice[0] << " activeAskPrice1:" << data.activeAskPrice[0] << " activeBidPrice1:" << data.activeBidPrice[0] << std::endl;
    //return;

    stra::MdSpread mdSpread;
    mdSpread.spreadDrive = stra::SpreadDrive(topic->spreadDrive);  // 价差驱动
    mdSpread.spreadType = stra::SpreadType(topic->spreadType); // 价差类型
    mdSpread.spreadEffective = data.spreadEffective; // 价差是否有效
    mdSpread.statEffective = data.statEffective; // 统计量是否有效

    vector<string> v;
    splitString(topic->__name, v, "|");
    strncpy(mdSpread.pairInstrumentKey, topic->__name, stra::INST_KEY_LEN);
    strncpy(mdSpread.activeInstumentKey, v[0].c_str(), stra::INST_KEY_LEN);
    strncpy(mdSpread.passiveInstrumentKey, v[1].c_str(), stra::INST_KEY_LEN);


    mdSpread.spreadBidAsk = data.spreadBidAsk;  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
    mdSpread.spreadBidBid = data.spreadBidBid;   // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
    mdSpread.spreadAskBid = data.spreadAskBid;   // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
    mdSpread.spreadAskAsk = data.spreadAskAsk;   // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

    mdSpread.spreadBidAskTema = data.spreadBidAskTema;  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
    mdSpread.spreadBidBidTema = data.spreadBidBidTema;  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
    mdSpread.spreadAskBidTema = data.spreadAskBidTema;  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
    mdSpread.spreadAskAskTema = data.spreadAskAskTema;  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

    mdSpread.spreadBidAskMax = data.spreadBidAskMax;  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
    mdSpread.spreadBidBidMax = data.spreadBidBidMax;  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
    mdSpread.spreadAskBidMax = data.spreadAskBidMax;  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
    mdSpread.spreadAskAskMax = data.spreadAskAskMax;  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

    mdSpread.spreadBidAskMin = data.spreadBidAskMin; // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
    mdSpread.spreadBidBidMin = data.spreadBidBidMin;  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
    mdSpread.spreadAskBidMin = data.spreadAskBidMin; // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
    mdSpread.spreadAskAskMin = data.spreadAskAskMin; // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

    mdSpread.activePriceTema = data.activePriceTema;  // 主动腿Tema价格
    mdSpread.passivePriceTema = data.passivePriceTema;   // 被动腿Tema价格

    mdSpread.activeFundingRate = data.activeFundingRate;
    mdSpread.passiveFundingRate = data.passiveFundingRate;
    // mdSpread.activeMultiply = 2;  // 主动腿调整系数(适用于1000shib与shib的情况)
    // mdSpread.passiveMultiply = 3;  //  被动腿调整系数

    mdSpread.passiveAskPrice1 = data.passiveAskPrice[0];
    mdSpread.passiveAskVolume1 = data.passiveAskVolume[0];
    mdSpread.passiveAskPrice2 = data.passiveAskPrice[1];
    mdSpread.passiveAskVolume2 = data.passiveAskVolume[1];
    mdSpread.passiveAskPrice3 = data.passiveAskPrice[2];
    mdSpread.passiveAskVolume3 = data.passiveAskVolume[2];
    mdSpread.passiveAskPrice4 = data.passiveAskPrice[3];
    mdSpread.passiveAskVolume4 = data.passiveAskVolume[3];
    mdSpread.passiveAskPrice5 = data.passiveAskPrice[4];
    mdSpread.passiveAskVolume5 = data.passiveAskVolume[4];

    mdSpread.passiveBidPrice1 = data.passiveBidPrice[0];
    mdSpread.passiveBidVolume1 = data.passiveBidVolume[0];
    mdSpread.passiveBidPrice2 = data.passiveBidPrice[1];
    mdSpread.passiveBidVolume2 = data.passiveBidVolume[1];
    mdSpread.passiveBidPrice3 = data.passiveBidPrice[2];
    mdSpread.passiveBidVolume3 = data.passiveBidVolume[2];
    mdSpread.passiveBidPrice4 = data.passiveBidPrice[3];
    mdSpread.passiveBidVolume4 = data.passiveBidVolume[3];
    mdSpread.passiveBidPrice5 = data.passiveBidPrice[4];
    mdSpread.passiveBidVolume5 = data.passiveBidVolume[4];

    mdSpread.activeAskPrice1 = data.activeAskPrice[0];
    mdSpread.activeAskVolume1 = data.activeAskVolume[0];
    mdSpread.activeAskPrice2 = data.activeAskPrice[1];
    mdSpread.activeAskVolume2 = data.activeAskVolume[1];
    mdSpread.activeAskPrice3 = data.activeAskPrice[2];
    mdSpread.activeAskVolume3 = data.activeAskVolume[2];
    mdSpread.activeAskPrice4 = data.activeAskPrice[3];
    mdSpread.activeAskVolume4 = data.activeAskVolume[3];
    mdSpread.activeAskPrice5 = data.activeAskPrice[4];
    mdSpread.activeAskVolume5 = data.activeAskVolume[4];

    mdSpread.activeBidPrice1 = data.activeBidPrice[0];
    mdSpread.activeBidVolume1 = data.activeBidVolume[0];
    mdSpread.activeBidPrice2 = data.activeBidPrice[1];
    mdSpread.activeBidVolume2 = data.activeBidVolume[1];
    mdSpread.activeBidPrice3 = data.activeBidPrice[2];
    mdSpread.activeBidVolume3 = data.activeBidVolume[2];
    mdSpread.activeBidPrice4 = data.activeBidPrice[3];
    mdSpread.activeBidVolume4 = data.activeBidVolume[3];
    mdSpread.activeBidPrice5 = data.activeBidPrice[4];
    mdSpread.activeBidVolume5 = data.activeBidVolume[4];

    mdSpread.activeFundingTs = data.activeFundingTs;  // 主动腿funding收取时间
    mdSpread.passiveFundingTs = data.passiveFundingTs; // 被动退funding收取时间
    mdSpread.activeDepthTs = data.activeDepthTs;  // 主动腿depth时间
    mdSpread.passiveDepthTs = data.passiveDepthTs;  // 被动退depth时间
    mdSpread.generateTs = data.generateTs;  // 价差生成时间
    mdSpread.diffTs = data.diffTs; // 主动退被动腿时间差

    mdSpread.activeDepthDelay = data.activeDepthDelay;
    mdSpread.passiveDepthDelay = data.passiveDepthDelay;

    mdSpread.exchActiveTradeDelay = data.exchActiveTradeDelay;
    mdSpread.exchPassiveTradeDelay = data.exchPassiveTradeDelay;

    int64_t currentTime = GetCurrentTimeUs();
    context.OnSpread(mdSpread, currentTime);
}
