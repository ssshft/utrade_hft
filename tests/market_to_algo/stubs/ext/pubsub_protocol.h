// Stub for the external pubsub protocol headers (include/pubsub_protocol.h).
//
// 字段集**逐字照抄**真实头，只把依赖 rapidjson / 真 fmt 的部分去掉。
// getString() 保留 —— 它只用 fmt::format 的 `{}` 顺序替换，本套件的 fmt 桩能处理，
// 保留下来可以让 LOG_DEBUG("{}", x.getString()) 这类调用点真的被编译到。
//
// 相比旧桩补齐的内容（旧桩只有 Position/Balance/TotalAccount/OrderResponse 的
// 极少数字段，导致 quant_library/basic/DataStruct.h 根本编译不过）：
//   - CommandType 枚举 + 两张映射表
//   - NewOrder / CancelOrder / QueryOrder / QueryAccount / QueryBalance / QueryPosition
//   - TCommand / RCommand（出向 TCommand 是 om::TradeClient 的载荷，执行侧测试要断言它）
//   - Position / Balance / TotalAccount / OrderResponse 的完整字段
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "data_struct.h"
#include "fmt/core.h"

namespace pubsub {

enum CommandType {
    CMD_TYPE_MIN = 0,
    CMD_NEW_ORDER,
    CMD_CANCEL_ORDER,
    CMD_QUERY_ORDER,
    CMD_QUERY_ACCOUNT,
    CMD_QUERY_BALANCE,
    CMD_QUERY_POSITION,

    CMD_RPT_NEW_ORDER,
    CMD_RPT_CANCEL_ORDER,
    CMD_RPT_QUERY_ORDER,
    CMD_RPT_TOTAL_ACCOUNT,
    CMD_RPT_BALANCE,
    CMD_RPT_POSITION,
    CMD_RPT_ORDER_RESPONSE,
    CMD_TYPE_MAX
};

static std::unordered_map<CommandType, std::string> CommandTypeEnum2StrMap {
    {CMD_TYPE_MIN, "CMD_TYPE_MIN"},
    {CMD_NEW_ORDER, "CMD_NEW_ORDER"},
    {CMD_CANCEL_ORDER, "CMD_CANCEL_ORDER"},
    {CMD_QUERY_ORDER, "CMD_QUERY_ORDER"},
    {CMD_QUERY_ACCOUNT, "CMD_QUERY_ACCOUNT"},
    {CMD_QUERY_BALANCE, "CMD_QUERY_BALANCE"},
    {CMD_QUERY_POSITION, "CMD_QUERY_POSITION"},

    {CMD_RPT_NEW_ORDER, "CMD_RPT_NEW_ORDER"},
    {CMD_RPT_CANCEL_ORDER, "CMD_RPT_CANCEL_ORDER"},
    {CMD_RPT_QUERY_ORDER, "CMD_RPT_QUERY_ORDER"},
    {CMD_RPT_TOTAL_ACCOUNT, "CMD_RPT_TOTAL_ACCOUNT"},
    {CMD_RPT_BALANCE, "CMD_RPT_BALANCE"},
    {CMD_RPT_POSITION, "CMD_RPT_POSITION"},
    {CMD_RPT_ORDER_RESPONSE, "CMD_RPT_ORDER_RESPONSE"},
    {CMD_TYPE_MAX, "CMD_TYPE_MAX"}
};

static std::unordered_map<std::string, CommandType> CommandTypeStr2EnumMap {
    {"CMD_TYPE_MIN", CMD_TYPE_MIN},
    {"CMD_NEW_ORDER", CMD_NEW_ORDER},
    {"CMD_CANCEL_ORDER", CMD_CANCEL_ORDER},
    {"CMD_QUERY_ORDER", CMD_QUERY_ORDER},
    {"CMD_QUERY_ACCOUNT", CMD_QUERY_ACCOUNT},
    {"CMD_QUERY_BALANCE", CMD_QUERY_BALANCE},
    {"CMD_QUERY_POSITION", CMD_QUERY_POSITION},

    {"CMD_RPT_NEW_ORDER", CMD_RPT_NEW_ORDER},
    {"CMD_RPT_CANCEL_ORDER", CMD_RPT_CANCEL_ORDER},
    {"CMD_RPT_QUERY_ORDER", CMD_RPT_QUERY_ORDER},
    {"CMD_RPT_TOTAL_ACCOUNT", CMD_RPT_TOTAL_ACCOUNT},
    {"CMD_RPT_BALANCE", CMD_RPT_BALANCE},
    {"CMD_RPT_POSITION", CMD_RPT_POSITION},
    {"CMD_RPT_ORDER_RESPONSE", CMD_RPT_ORDER_RESPONSE},
    {"CMD_TYPE_MAX", CMD_TYPE_MAX}
};

// ===========================================================================
// 出向（策略 -> tb）
// ===========================================================================
struct NewOrder {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];
    int64_t clientOrderId;
    char orderSysId[64];
    char strategyRef[64];
    OffsetFlag offsetFlag;
    Direction direction;
    OrderType orderType;
    double volumeTotal;
    double limitPrice;
    bool reduceOnly;

    std::string getString() const {
        return fmt::format("[NewOrder] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "instId:{}, clientOrderId:{}, orderSysId:{}, strategyRef:{}, offsetFlag:{}, "
                           "direction:{}, orderType:{}, volumeTotal:{}, limitPrice:{}, reduceOnly:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           instId, clientOrderId, orderSysId, strategyRef, OffsetFlagEnum2StrMap[offsetFlag],
                           DirectionEnum2StrMap[direction], OrderTypeEnum2StrMap[orderType], volumeTotal,
                           limitPrice, reduceOnly);
    }
};

struct CancelOrder {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];
    int64_t clientOrderId;
    char orderSysId[64];
    char orderId[64];

    std::string getString() const {
        return fmt::format("[CancelOrder] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "instId:{}, clientOrderId:{}, orderSysId:{}, orderId:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           instId, clientOrderId, orderSysId, orderId);
    }
};

struct QueryOrder {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];
    int64_t clientOrderId;
    char orderSysId[64];
    char orderId[64];

    std::string getString() const {
        return fmt::format("[QueryOrder] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "instId:{}, clientOrderId:{}, orderSysId:{}, orderId:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           instId, clientOrderId, orderSysId, orderId);
    }
};

struct QueryAccount {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    std::string getString() const {
        return fmt::format("[QueryAccount] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId);
    }
};

struct QueryBalance {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char currency[16];

    std::string getString() const {
        return fmt::format("[QueryBalance] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, currency:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId, currency);
    }
};

struct QueryPosition {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];

    std::string getString() const {
        return fmt::format("[QueryPosition] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, instId:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId, instId);
    }
};

// ===========================================================================
// 入向回报（tb -> 策略）
// ===========================================================================
struct OrderResponse {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];
    int64_t clientOrderId;
    char orderSysId[64];
    char orderId[64];
    char strategyRef[64];

    OffsetFlag offsetFlag;
    Direction direction;
    OrderType orderType;
    OrderStatus orderStatus;
    double volumeTotal;
    double limitPrice;
    double volumeTraded;
    double tradePrice;
    double tradeDiff;   // 本次成交量
    double fillPrice;   // 本次成交价
    bool reduceOnly;

    int errorId;
    char originMsg[128];
    int64_t updateTime;

    ApiSource apiSourceEnum;

    std::string getString() const {
        return fmt::format("[OrderResponse] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "instId:{}, clientOrderId:{}, orderSysId:{}, orderId:{}, strategyRef:{}, offsetFlag:{}, direction:{}, "
                           "orderType:{}, orderStatus:{}, volumeTotal:{}, limitPrice:{}, volumeTraded:{}, "
                           "tradePrice:{}, errorId:{}, originMsg:{}, updateTime:{}, apiSourceEnum:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           instId, clientOrderId, orderSysId, orderId, strategyRef, OffsetFlagEnum2StrMap[offsetFlag], DirectionEnum2StrMap[direction],
                           OrderTypeEnum2StrMap[orderType], OrderStatusEnum2StrMap[orderStatus], volumeTotal, limitPrice, volumeTraded,
                           tradePrice, errorId, originMsg, updateTime, ApiSourceEnum2StrMap[apiSourceEnum]);
    }
};

struct Balance {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char currency[16];
    double total;
    double available;
    double frozen;
    double borrowed;
    double unrealizedPnl;
    bool isLast;
    int64_t updateTime;
    ApiSource apiSourceEnum;

    std::string getString() const {
        return fmt::format("[Balance] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "currency:{}, total:{}, available:{}, frozen:{}, borrowed:{}, isLast:{}, apiSourceEnum:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           currency, total, available, frozen, borrowed, isLast, ApiSourceEnum2StrMap[apiSourceEnum]);
    }
};

struct Position {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    char instId[32];
    Direction direction;
    double volume;
    double maintMargin;
    double avgPrice;
    double unrealizedPnl;
    double liquidPrice;
    double markPrice;
    double adlQuantile;
    bool isLast;
    int64_t updateTime;
    ApiSource apiSourceEnum;

    std::string getString() const {
        return fmt::format("[Position] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "instId:{}, direction:{}, volume:{}, maintMargin:{}, avgPrice:{}, unrealizedPnl:{}, "
                           "liquidPrice:{}, markPrice:{}, adlQuantile:{}, isLast:{}, apiSourceEnum:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           instId, DirectionEnum2StrMap[direction], volume, maintMargin, avgPrice, unrealizedPnl,
                           liquidPrice, markPrice, adlQuantile, isLast, ApiSourceEnum2StrMap[apiSourceEnum]);
    }
};

struct TotalAccount {
    ExchangeType exchangeTypeEnum;
    InstType instTypeEnum;
    int accountId;
    char accountName[32];
    char strategyId[32];

    double totalEquity;
    double adjEquity;
    double mmr;
    double mgnRatio;
    int64_t updateTime;
    ApiSource apiSourceEnum;

    std::string getString() const {
        return fmt::format("[TotalAccount] exchangeTypeEnum:{}, instTypeEnum:{}, accountId:{}, accountName:{}, strategyId:{}, "
                           "totalEquity:{}, adjEquity:{}, mmr:{}, mgnRatio:{}, apiSourceEnum:{}",
                           ExchangeTypeEnum2StrMap[exchangeTypeEnum], InstTypeEnum2StrMap[instTypeEnum], accountId, accountName, strategyId,
                           totalEquity, adjEquity, mmr, mgnRatio, ApiSourceEnum2StrMap[apiSourceEnum]);
    }
};

// ===========================================================================
// 信封
// ===========================================================================
struct TCommand {
    CommandType cmdTypeEnum;

    union CommandBody {
        NewOrder newOrder;
        CancelOrder cancelOrder;
        QueryOrder queryOrder;
        QueryAccount queryAccount;
        QueryBalance queryBalance;
        QueryPosition queryPosition;
    };
    CommandBody body;

    std::string getString() const {
        std::string ret = fmt::format("[{}]", CommandTypeEnum2StrMap[cmdTypeEnum]);
        if (cmdTypeEnum == CMD_NEW_ORDER) {
            ret.append(body.newOrder.getString());
        } else if (cmdTypeEnum == CMD_CANCEL_ORDER) {
            ret.append(body.cancelOrder.getString());
        } else if (cmdTypeEnum == CMD_QUERY_ORDER) {
            ret.append(body.queryOrder.getString());
        } else if (cmdTypeEnum == CMD_QUERY_ACCOUNT) {
            ret.append(body.queryAccount.getString());
        } else if (cmdTypeEnum == CMD_QUERY_BALANCE) {
            ret.append(body.queryBalance.getString());
        } else if (cmdTypeEnum == CMD_QUERY_POSITION) {
            ret.append(body.queryPosition.getString());
        }
        return ret;
    }
};

struct RCommand {
    CommandType cmdTypeEnum;

    union CommandBody {
        OrderResponse orderResponse;
        Balance balance;
        Position position;
        TotalAccount totalAccount;
    };
    CommandBody body;

    std::string getString() const {
        std::string ret = fmt::format("[{}]", CommandTypeEnum2StrMap[cmdTypeEnum]);
        if (cmdTypeEnum == CMD_RPT_ORDER_RESPONSE || cmdTypeEnum == CMD_RPT_NEW_ORDER ||
            cmdTypeEnum == CMD_RPT_CANCEL_ORDER || cmdTypeEnum == CMD_RPT_QUERY_ORDER) {
            ret.append(body.orderResponse.getString());
        } else if (cmdTypeEnum == CMD_RPT_BALANCE) {
            ret.append(body.balance.getString());
        } else if (cmdTypeEnum == CMD_RPT_POSITION) {
            ret.append(body.position.getString());
        } else if (cmdTypeEnum == CMD_RPT_TOTAL_ACCOUNT) {
            ret.append(body.totalAccount.getString());
        }
        return ret;
    }
};

} // namespace pubsub
