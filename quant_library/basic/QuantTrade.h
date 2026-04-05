#ifndef _QUANT_TRADE_H
#define _QUANT_TRADE_H

#include "DataStruct.h"
#include "command_helper.h"
#include "LimitManager.h"
#include "StrategyConfig.h"

class QuantTrade {
public:
    static QuantTrade& Instance() {
        static QuantTrade quantTrade;
        return quantTrade;
    }

    ~QuantTrade() {}

    void SetTradeClient(om::TradeClient* client) {
        tradeClient = client;
    }

    bool CreateOrder(const stra::QuantOrder& order) {
        LimitManager::Instance().OnOrder(order);
        string strategyId = StrategyConfig::GetInstance().GetStrategyIdByAccountId(order.strategyAccountId);
        char ref[stra::ID_LEN];
        sprintf(ref, "%ld_%ld", order.algoPairId, order.pairId);
        tradeClient->add_new_order(ExchangeType(order.exchangeType), InstType(order.instType), strategyId.c_str(), order.instrument, OffsetFlag(order.posDirection), Direction(order.direction), OrderType(order.orderType), order.price, order.volume, order.strategyOrderId, order.reduceOnly, ref);
        return true;
    }

    bool CancelOrder(const stra::QuantOrder& order) {
	    LimitManager::Instance().OnOrder(order);
        string strategyId = StrategyConfig::GetInstance().GetStrategyIdByAccountId(order.strategyAccountId);
        tradeClient->cancel_order(ExchangeType(order.exchangeType), InstType(order.instType), strategyId.c_str(), order.instrument, "", order.strategyOrderId);
        return true;
    }

    bool QueryOrder(const stra::QuantOrder& order) {
        LimitManager::Instance().OnOrder(order);
        string strategyId = StrategyConfig::GetInstance().GetStrategyIdByAccountId(order.strategyAccountId);
        tradeClient->query_order(ExchangeType(order.exchangeType), InstType(order.instType), strategyId.c_str(), order.instrument, order.exchangeOrderId, order.strategyOrderId);
        return true;
    }

    bool QueryAccount(const stra::QuantOrder& order) {
        string strategyId = StrategyConfig::GetInstance().GetStrategyIdByAccountId(order.strategyAccountId);
        tradeClient->query_account(ExchangeType(order.exchangeType), InstType(order.instType), strategyId.c_str());
        return true;
    }

private:
    QuantTrade() {
        tradeClient = nullptr;
    }
    om::TradeClient* tradeClient;
};

#endif
