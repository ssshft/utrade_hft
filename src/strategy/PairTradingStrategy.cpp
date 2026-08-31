#include "strategy/PairTradingStrategy.h"
#include "basic/DataStruct.h"
#include "basic/PairInfoManager.h"

#include "log_engine.h"
#include <chrono>
#include <cstring>


using namespace std::chrono;

static int64_t NowUs() {
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

PairTradingStrategy::PairTradingStrategy() {
    algoContext.SetTradeClient(tradeClient);
};

PairTradingStrategy::~PairTradingStrategy() {

};

void PairTradingStrategy::pre_start(Config* config) {
    _init(config);

    std::string cfgStr = config->get_document_str();
    rapidjson::Document d;
    rapidjson::Value& v = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(cfgStr.c_str());

    auto& op = v["op"];

    m_ptCfg.activeAccountId = std::stoi(op["activeAccountId"].GetString());
    m_ptCfg.passiveAccountId = std::stoi(op["passiveAccountId"].GetString());

    if (op.HasMember("pairKeys")) {
        for (auto& pk: op["pairKeys"].GetArray()) {
            m_ptCfg.pairKeys.emplace_back(pk.GetString());
        }
    }

    if (op.HasMember("maxPositionValue")) {
        m_ptCfg.maxPositionValue = std::stod(op["maxPositionValue"].GetString());
    }
    if (op.HasMember("maxAmount")) {
        m_ptCfg.maxAmount = std::stod(op["maxAmount"].GetString());
    }
    if (op.HasMember("targetAmount")) {
        m_ptCfg.targetAmount = std::stod(op["targetAmount"].GetString());
    }

    if (op.HasMember("csvStatePath")) {
        m_ptCfg.csvStatePath = op["csvStatePath"].GetString();
    }

    algoContext.SetDbp(dbpreader);
    algoContext.PreStart();

    ptContext.SetAlgoCommandCallback([this](const std::string& json) {
        SubmitAlgoCommand(json);
    });
    ptContext.Init(m_ptCfg);
}

void PairTradingStrategy::pre_stop() {
    if (!m_ptCfg.csvStatePath.empty()) {
        pt::PairInfoManager::Instance().SaveToCSV(m_ptCfg.csvStatePath);
    }
    BaseStrategy::pre_stop();
}

void PairTradingStrategy::SubmitAlgoCommand(const std::string& json) {
    algoContext.OnCommand(json);
}

void PairTradingStrategy::on_command(const std::string& json) {
    algoContext.OnCommand(json);
}

void PairTradingStrategy::on_timer(const int64_t& utcTime) {
    algoContext.OnTimer(utcTime);
    ptContext.OnTimer(utcTime);

    if (nowUs - m_lastScanUs >= SCAN_INTERVAL_US) {
        ScanFinishedAlgoOrders(nowUs);
        m_lastScanUs = nowUs;
    }
}

void PairTradingStrategy::on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum)
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

    // mdSpread.activeDepthDelay;
    // mdSpread.passiveDepthDelay;
    // mdSpread.exchActiveTradeDelay;
    // mdSpread.exchPassiveTradeDelay;

    int64_t currentTime = crypto::getCurrentTime();

    // algoContext.OnSpread(mdSpread, currentTime);
    // ptContext.OnSpread(mdSpread, currentTime);

    algoContext.OnSpread(topic, pdata);
    ptContext.OnSpread(topic, pdata);

}


void PairTradingStrategy::on_balance(pubsub::Balance& balance){
    //DEBUGLINE
    int64_t currentTime = crypto::getCurrentTime();
    // stra::TdBalance ba;
    // ba.accountId = mAccountNameAccountId[balance.accountId];
    // ba.exchangType = stra::ExchangeType(balance.exchangeTypeEnum);
    // ba.instType = stra::InstType(balance.instTypeEnum);
    // strncpy(ba.strategyId, balance.strategyId, stra::ID_LEN);
    // strncpy(ba.currency, balance.currency, stra::ASSET_LEN);
    // ba.total = balance.total;
    // ba.available = balance.available;
    // ba.unrealizedPnl = balance.unrealizedPnl;
    // ba.frozen = balance.frozen;
    // ba.updateTime = balance.updateTime;

    algoContext.OnBalance(balance);
    ptContext.OnBalance(balance);
}

//仓位推送
void PairTradingStrategy::on_position(pubsub::Position& position){
    //DEBUGLINE 
    int64_t currentTime = crypto::getCurrentTime();
    // stra::TdPosition pos;
    // pos.accountId = mAccountNameAccountId[position.accountId];
    // pos.exchangType = stra::ExchangeType(position.exchangeTypeEnum);
    // pos.instType = stra::InstType(position.instTypeEnum);
    // strncpy(pos.strategyId, position.strategyId, stra::ID_LEN);
    // strncpy(pos.instrument, position.instId, stra::INST_ID_LEN);
    // pos.direction = stra::Direction(position.direction);
    // pos.volume = position.volume;
    // pos.maintMargin = position.maintMargin;
    // pos.avgPrice = position.avgPrice;
    // pos.unrealizedPnl = position.unrealizedPnl;
    // pos.liquidPrice = position.liquidPrice;
    // pos.adlQuantile = position.adlQuantile;
    // pos.markPrice = position.markPrice;
    // pos.updateTime = position.updateTime;

    algoContext.OnPosition(position);
    ptContext.OnPosition(position);
}

//账户总览推送
void PairTradingStrategy::on_total_account(pubsub::TotalAccount& totalAccount) {
    //DEBUGLINE
    int64_t currentTime = crypto::getCurrentTime();
    // stra::TdTotalAccount ta;
    // ta.accountId = mAccountNameAccountId[totalAccount.accountId];
    // ta.exchangType = stra::ExchangeType(totalAccount.exchangeTypeEnum);
    // ta.instType = stra::InstType(totalAccount.instTypeEnum);
    // strncpy(ta.strategyId, totalAccount.strategyId, stra::ID_LEN);
    // ta.totalEquity = totalAccount.totalEquity;
    // ta.adjEquity = totalAccount.adjEquity;
    // ta.mmr = totalAccount.mmr;
    // ta.mgnRatio = totalAccount.mgnRatio;
    // ta.updateTime = totalAccount.updateTime;

    algoContext.OnTotalAccount(totalAccount);
    ptContext.OnTotalAccount(totalAccount);
}

// rest，报单撤单返回的回报
//订单成交变化
void PairTradingStrategy::on_ordertrade(pubsub::OrderResponse& orderResponse) {
    //DEBUGLINE
    int64_t currentTime = crypto::getCurrentTime();
    // stra::TdOrder order;

    // vector<string> v;
    // splitString(orderTrade.strategyRef, v, "_");
    // if (v.size() >= 2) {
    //     order.algoId = stoll(v[0]);
    //     order.pairId = stoll(v[1]);
    // }

    // // int64_t sysOrdId{0};
    // order.clOrdId = orderTrade.clientOrderId;
    // strncpy(order.exOrdId, orderTrade.orderId, stra::ID_LEN);
    // strncpy(order.instrument, orderTrade.instId, stra::INST_ID_LEN);
    // order.exchangType = stra::ExchangeType(orderTrade.exchangeTypeEnum);
    // order.instType = stra::InstType(orderTrade.instTypeEnum);
    // order.orderStatus = stra::OrderStatus(orderTrade.orderStatus);
    // order.posDirection = stra::PosDirection(orderTrade.offsetFlag);
    // order.direction = stra::Direction(orderTrade.direction);
    // order.orderType = stra::OrderType(orderTrade.orderType);
    // order.reduceOnly = orderTrade.reduceOnly;
    // order.price = orderTrade.limitPrice;
    // order.volume = orderTrade.volumeTotal;
    // order.avgPrice = orderTrade.tradePrice;
    // order.totalPriceOnOrder = orderTrade.tradePrice;
    // order.totalVolumeOnOrder = orderTrade.volumeTraded;
    // // order.lastExecutedPriceOnOrder = orderTrade.tradePrice;
    // order.lastExecutedVolumeOnOrder = orderTrade.tradedDiff;
    // order.errorId = orderTrade.ErrorID;
    // strncpy(order.originErrorMsg, orderTrade.originMsg, 128);
    // order.insertTime = orderTrade.insertTime;
    // order.updateTime = orderTrade.updateTime;
    // order.tsSend = orderTrade.tsSent;
    // order.tsNet = orderTrade.tsNet;
    // order.apiSource = stra::ApiSource(orderTrade.apiSourceEnum);
    // order.isMaker = orderTrade.isMaker;

    algoContext.OnOrder(orderResponse, currentTime);
}

void PairTradingStrategy::ScanFinishedAlgoOrders(int64_t nowUs) {
    auto& pim = pt::PairInfoManager::Instance();

    for (pt::PairInfo* pi : pim.GetAllPairInfos()) {
        if (!pi->hasActiveAlgoOrder) {
            continue;
        }

        int64_t algoOrderIdInt = std::stoll(pi->currentAlgoOrderId);
        BaseAlgoOrder* order = algoContext.GetAlgoOrder(algoOrderIdInt);

        if (!order) {
            double volFilled = order ? order->pairTotalVolume - pi->pairTotalVolume : 0.0;
            bool fullyFlat = !pi->HasPosition();

            ptContext.OnAlgoOrderUpdate(pi->pairInstrumentKey, pi->currentAlgoOrderId, volFilled, 0.0, 0.0, true, fullyFlat);
        }
        else {
            if (order->algoOrderStatus == stra::OrderStatus_FILLED || order->algoOrderStatus == stra::OrderStatus_CANCELED || order->algoOrderStatus == stra::OrderStatus_ERRORCANCELED) {
                double volFilled = order->pairTotalVolume - pi->pairTotalVolume;
                bool fullyFlat = !pi->HasPosition();

                ptContext.OnAlgoOrderUpdate(pi->pairInstrumentKey, pi->currentAlgoOrderId, volFilled, order->pairActiveTotalPrice, order->pairPassiveTotalPrice, true, fullyFlat);
            }
        }
    }
}