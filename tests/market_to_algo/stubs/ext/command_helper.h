#pragma once
// Stub for the external include/command_helper.h.
//
// 真实的 om::TradeClient 是往 System V 共享内存队列 Utrade2TbTCommandSHM 推 TCommand。
// 测试里换成**记录器**：方法签名、参数顺序、以及构造出的 TCommand 字段全部照抄，
// 只把 "push 到共享内存" 换成 "存进 vector 供断言"。
// 这样 QuantTrade（真实的、header-only）不用改一行，出向指令却可以被逐字段检查。
//
// crypto::convert_rcmd_2_* 四个函数逐字照抄真实头。
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "data_struct.h"
#include "pubsub_protocol.h"

namespace crypto {
    inline bool convert_rcmd_2_ordertrade(const pubsub::RCommand& rcmd, pubsub::OrderResponse& orderResponse) {
        if (rcmd.cmdTypeEnum == pubsub::CMD_RPT_ORDER_RESPONSE) {
            memcpy(&orderResponse, &rcmd.body.orderResponse, sizeof(rcmd.body.orderResponse));
            return true;
        }
        return false;
    }

    inline bool convert_rcmd_2_balance(const pubsub::RCommand& rcmd, pubsub::Balance& balance) {
        if (rcmd.cmdTypeEnum == pubsub::CMD_RPT_BALANCE) {
            memcpy(&balance, &rcmd.body.balance, sizeof(rcmd.body.balance));
            return true;
        }
        return false;
    }

    inline bool convert_rcmd_2_position(const pubsub::RCommand& rcmd, pubsub::Position& position) {
        if (rcmd.cmdTypeEnum == pubsub::CMD_RPT_POSITION) {
            memcpy(&position, &rcmd.body.position, sizeof(rcmd.body.position));
            return true;
        }
        return false;
    }

    inline bool convert_rcmd_2_total_account(const pubsub::RCommand& rcmd, pubsub::TotalAccount& totalAccount) {
        if (rcmd.cmdTypeEnum == pubsub::CMD_RPT_TOTAL_ACCOUNT) {
            memcpy(&totalAccount, &rcmd.body.totalAccount, sizeof(rcmd.body.totalAccount));
            return true;
        }
        return false;
    }
}

namespace om {

// 记录器：测试通过 Sent() 取全部出向指令。
class TradeClient {
public:
    explicit TradeClient(int key) : m_key(key) {}

    void add_new_order(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId,
                       const char* instId, OffsetFlag offsetFlag, Direction direction,
                       OrderType orderType, double price, double volume, long clientOrderId,
                       bool reduceOnly = false, const char* strategyRef = "") {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_NEW_ORDER;
        tcmd.body.newOrder.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.newOrder.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.newOrder.strategyId, strategyId, STRATEGYID_SIZE);
        strncpy(tcmd.body.newOrder.instId, instId, INSTID_SIZE);
        tcmd.body.newOrder.clientOrderId = clientOrderId;
        strncpy(tcmd.body.newOrder.strategyRef, strategyRef, ORDER_SIZE);
        tcmd.body.newOrder.offsetFlag = offsetFlag;
        tcmd.body.newOrder.direction = direction;
        tcmd.body.newOrder.orderType = orderType;
        tcmd.body.newOrder.volumeTotal = volume;
        tcmd.body.newOrder.limitPrice = price;
        tcmd.body.newOrder.reduceOnly = reduceOnly;
        m_sent.push_back(tcmd);
    }

    void cancel_order(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId,
                      const char* instId, const char* orderId = "", long clientOrderId = 0) {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_CANCEL_ORDER;
        tcmd.body.cancelOrder.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.cancelOrder.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.cancelOrder.strategyId, strategyId, STRATEGYID_SIZE);
        strncpy(tcmd.body.cancelOrder.instId, instId, INSTID_SIZE);
        tcmd.body.cancelOrder.clientOrderId = clientOrderId;
        strncpy(tcmd.body.cancelOrder.orderId, orderId, ORDER_SIZE);
        m_sent.push_back(tcmd);
    }

    void query_order(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId,
                     const char* instId, const char* orderId = "", long clientOrderId = 0) {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_QUERY_ORDER;
        tcmd.body.queryOrder.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.queryOrder.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.queryOrder.strategyId, strategyId, STRATEGYID_SIZE);
        strncpy(tcmd.body.queryOrder.instId, instId, INSTID_SIZE);
        tcmd.body.queryOrder.clientOrderId = clientOrderId;
        strncpy(tcmd.body.queryOrder.orderId, orderId, ORDER_SIZE);
        m_sent.push_back(tcmd);
    }

    void query_account(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId) {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_QUERY_ACCOUNT;
        tcmd.body.queryAccount.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.queryAccount.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.queryAccount.strategyId, strategyId, STRATEGYID_SIZE);
        m_sent.push_back(tcmd);
    }

    void query_position(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId, const char* instId) {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_QUERY_POSITION;
        tcmd.body.queryPosition.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.queryPosition.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.queryPosition.strategyId, strategyId, STRATEGYID_SIZE);
        strncpy(tcmd.body.queryPosition.instId, instId, INSTID_SIZE);
        m_sent.push_back(tcmd);
    }

    void query_balance(ExchangeType exchangeTypeEnum, InstType instTypeEnum, const char* strategyId, const char* currency) {
        pubsub::TCommand tcmd;
        memset(&tcmd, 0, sizeof(pubsub::TCommand));
        tcmd.cmdTypeEnum = pubsub::CMD_QUERY_BALANCE;
        tcmd.body.queryBalance.exchangeTypeEnum = exchangeTypeEnum;
        tcmd.body.queryBalance.instTypeEnum = instTypeEnum;
        strncpy(tcmd.body.queryBalance.strategyId, strategyId, STRATEGYID_SIZE);
        strncpy(tcmd.body.queryBalance.currency, currency, INSTID_SIZE);
        m_sent.push_back(tcmd);
    }

    // ---- 测试用 ----
    std::vector<pubsub::TCommand>& Sent() { return m_sent; }
    void Clear() { m_sent.clear(); }
    int Key() const { return m_key; }

    size_t CountNewOrders() const {
        size_t n = 0;
        for (const auto& t : m_sent) if (t.cmdTypeEnum == pubsub::CMD_NEW_ORDER) ++n;
        return n;
    }
    size_t CountCancels() const {
        size_t n = 0;
        for (const auto& t : m_sent) if (t.cmdTypeEnum == pubsub::CMD_CANCEL_ORDER) ++n;
        return n;
    }
    size_t CountQueries() const {
        size_t n = 0;
        for (const auto& t : m_sent) if (t.cmdTypeEnum == pubsub::CMD_QUERY_ORDER) ++n;
        return n;
    }

private:
    int m_key{0};
    std::vector<pubsub::TCommand> m_sent;
};

} // namespace om
