// Stub for the external (deploy-machine) <data_struct.h>.
//
// ⚠️ 这个文件里的**枚举值顺序和 enum→string 映射表，全部逐字抄自真实的
//    include/data_struct.h**（2026-09-30 修正）。不能凭印象重写。
//
// 为什么必须逐字：
//   真实的 include/data_struct.h 还带着一大堆需要 rapidjson / fmt/format.h 的
//   struct::to_string()，本机没有 rapidjson，所以整份抄不过来。但**枚举和映射表
//   是纯数据、没有依赖**，可以原样搬。之前那版是凭印象写的，出了三个真错：
//
//     1. ExchangeType 顺序写成 BINANCE, GATEIO, OKX —— 真实是 BINANCE, OKX, GATEIO。
//        GATEIO 和 OKX 的**整数值被互换了**。
//     2. InstType 写成 USDT_SWAP, COIN_SWAP, SPOT —— 真实是
//        SPOT, MARGIN, USDT_SWAP, USDC_SWAP, BUSD_SWAP, C_SWAP, USDT_FUTURES, ...
//        不但顺序全错，还凭空造了一个不存在的 COIN_SWAP。
//     3. OrderStatus 里 OS_REJECTED 的位置错了：真实是 ...OS_FILLED, OS_REJECTED,
//        OS_CANCEL, OS_CANCELLING, OS_CANCELED, OS_UNKNOWN, OS_FAILED，
//        桩里写成了 ...OS_FILLED, OS_CANCEL, OS_CANCELLING, OS_CANCELED, OS_REJECTED,
//        OS_UNKNOWN, OS_MAX。
//
//   只要所有代码都用**符号名**比较，值错了也看不出来 —— 这正是最危险的那种桩。
//   一旦有 int(enum) 落盘、或按值 switch、或跨进程按整数传（Convert.h 里就是这么干的：
//   `r.orderStatus = int(order.orderStatus)`），两边就对不上了。
//
//   顺带补齐了真实头里存在、但旧桩漏掉的枚举量：OT_POST_ONLY / OT_FOK / OT_IOC、
//   OS_FAILED、BYBIT / HTX / BITGET、MARGIN / USDC_SWAP / BUSD_SWAP / C_SWAP /
//   USDT_FUTURES / BUSD_FUTURES / C_FUTURES / OPTION。
//   其中 OT_POST_ONLY 是**生产代码里真的在用的**（AlgoPairOrder.cpp / BaseAlgoOrder.cpp /
//   PairManager.cpp 的做市分支），旧桩里根本没有这个枚举量，那段逻辑压根编译不出来。
//
//   同时补齐了真实的四张 enum→string / string→enum 表（OrderType / OffsetFlag /
//   Direction / OrderStatus / ApiSource / md::MarketType）—— 真实的
//   quant_library/basic/DataStruct.h 里 `OrderStatusEnum2StrMap[...]` 是直接引用的。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// ---- 真实 include/data_struct.h 顶部的尺寸宏（command_helper.h / pubsub 桩要用）----
#define INSTID_SIZE 32
#define ACCOUNTID_SIZE 32
#define STRATEGYID_SIZE 32
#define ORDER_SIZE 64
#define MULTI_ORDER_SIZE 512
#define CCY_SIZE 16
#define ORIGINMSG_SIZE 256
#define UNIXTIMESTAMP int64_t
#define CLIENT_ORDER_ID_TYPE int64_t

// ===========================================================================
// ExchangeType —— 顺序照抄真实头（BINANCE, OKX, GATEIO, BYBIT, HTX, BITGET）
// ===========================================================================
enum ExchangeType {
    ExchangeType_MIN = 0,
    BINANCE,
    OKX,
    GATEIO,
    BYBIT,
    HTX,
    BITGET
};

static std::unordered_map<ExchangeType, std::string> ExchangeTypeEnum2StrMap {
    {ExchangeType_MIN, "ExchangeType_MIN"},
    {BINANCE, "BINANCE"},
    {OKX, "OKX"},
    {GATEIO, "GATEIO"},
    {BYBIT, "BYBIT"},
    {HTX, "HTX"},
    {BITGET, "BITGET"}
};

static std::unordered_map<std::string, ExchangeType> ExchangeTypeStr2EnumMap {
    {"ExchangeType_MIN", ExchangeType_MIN},
    {"BINANCE", BINANCE},
    {"OKX", OKX},
    {"GATEIO", GATEIO},
    {"BYBIT", BYBIT},
    {"HTX", HTX},
    {"BITGET", BITGET}
};

// ===========================================================================
// InstType —— 顺序照抄真实头
// ===========================================================================
enum InstType {
    InstType_MIN = 0,
    SPOT,
    MARGIN,
    USDT_SWAP,
    USDC_SWAP,
    BUSD_SWAP,
    C_SWAP,
    USDT_FUTURES,
    BUSD_FUTURES,
    C_FUTURES,
    OPTION
};

static std::unordered_map<InstType, std::string> InstTypeEnum2StrMap {
    {InstType_MIN, "InstType_MIN"},
    {SPOT, "SPOT"},
    {MARGIN, "MARGIN"},
    {USDT_SWAP, "USDT_SWAP"},
    {USDC_SWAP, "USDC_SWAP"},
    {BUSD_SWAP, "BUSD_SWAP"},
    {C_SWAP, "C_SWAP"},
    {USDT_FUTURES, "USDT_FUTURES"},
    {BUSD_FUTURES, "BUSD_FUTURES"},
    {C_FUTURES, "C_FUTURES"},
    {OPTION, "OPTION"}
};

static std::unordered_map<std::string, InstType> InstTypeStr2EnumMap {
    {"InstType_MIN", InstType_MIN},
    {"SPOT", SPOT},
    {"MARGIN", MARGIN},
    {"USDT_SWAP", USDT_SWAP},
    {"USDC_SWAP", USDC_SWAP},
    {"BUSD_SWAP", BUSD_SWAP},
    {"C_SWAP", C_SWAP},
    {"USDT_FUTURES", USDT_FUTURES},
    {"BUSD_FUTURES", BUSD_FUTURES},
    {"C_FUTURES", C_FUTURES},
    {"OPTION", OPTION}
};

// ===========================================================================
// OrderType —— 真实头里有 POST_ONLY / FOK / IOC，旧桩漏了
// ===========================================================================
enum OrderType {
    OT_MIN = 0,
    OT_LIMIT,
    OT_MARKET,
    OT_POST_ONLY,
    OT_FOK,
    OT_IOC
};

static std::unordered_map<OrderType, std::string> OrderTypeEnum2StrMap {
    {OT_MIN, "OT_MIN"},
    {OT_LIMIT, "OT_LIMIT"},
    {OT_MARKET, "OT_MARKET"},
    {OT_POST_ONLY, "OT_POST_ONLY"},
    {OT_FOK, "OT_FOK"},
    {OT_IOC, "OT_IOC"}
};

static std::unordered_map<std::string, OrderType> OrderTypeStr2EnumMap {
    {"OT_MIN", OT_MIN},
    {"OT_LIMIT", OT_LIMIT},
    {"OT_MARKET", OT_MARKET},
    {"OT_POST_ONLY", OT_POST_ONLY},
    {"OT_FOK", OT_FOK},
    {"OT_IOC", OT_IOC}
};

// ===========================================================================
// OffsetFlag —— 真实头**没有** OF_MAX
// ===========================================================================
enum OffsetFlag {
    OF_MIN = 0,
    OF_OPEN,
    OF_CLOSE
};

static std::unordered_map<OffsetFlag, std::string> OffsetFlagEnum2StrMap {
    {OF_MIN, "OF_MIN"},
    {OF_OPEN, "OF_OPEN"},
    {OF_CLOSE, "OF_CLOSE"}
};

static std::unordered_map<std::string, OffsetFlag> OffsetFlagStr2EnumMap {
    {"OF_MIN", OF_MIN},
    {"OF_OPEN", OF_OPEN},
    {"OF_CLOSE", OF_CLOSE}
};

// ===========================================================================
// Direction —— 真实头**没有** DT_MAX
// ===========================================================================
enum Direction {
    DT_MIN = 0,
    DT_LONG,
    DT_SHORT
};

static std::unordered_map<Direction, std::string> DirectionEnum2StrMap {
    {DT_MIN, "DT_MIN"},
    {DT_LONG, "DT_LONG"},
    {DT_SHORT, "DT_SHORT"}
};

static std::unordered_map<std::string, Direction> DirectionStr2EnumMap {
    {"DT_MIN", DT_MIN},
    {"DT_LONG", DT_LONG},
    {"DT_SHORT", DT_SHORT}
};

// ===========================================================================
// OrderStatus —— ⚠️ OS_REJECTED 在 OS_FILLED 之后、OS_CANCEL 之前
// ===========================================================================
enum OrderStatus {
    OS_MIN = 0,
    OS_PEND,
    OS_PENDING_NEW,
    OS_NEW,
    OS_PARTFILLED,
    OS_FILLED,
    OS_REJECTED,
    OS_CANCEL,
    OS_CANCELLING,
    OS_CANCELED,
    OS_UNKNOWN,
    OS_FAILED,
    OrderStatus_MAX
};

static std::unordered_map<OrderStatus, std::string> OrderStatusEnum2StrMap {
    {OS_MIN, "OS_MIN"},
    {OS_PEND, "OS_PEND"},
    {OS_PENDING_NEW, "OS_PENDING_NEW"},
    {OS_NEW, "OS_NEW"},
    {OS_PARTFILLED, "OS_PARTFILLED"},
    {OS_FILLED, "OS_FILLED"},
    {OS_REJECTED, "OS_REJECTED"},
    {OS_CANCEL, "OS_CANCEL"},
    {OS_CANCELLING, "OS_CANCELLING"},
    {OS_CANCELED, "OS_CANCELED"},
    {OS_UNKNOWN, "OS_UNKNOWN"},
    {OS_FAILED, "OS_FAILED"}
};

static std::unordered_map<std::string, OrderStatus> OrderStatusStr2EnumMap {
    {"OS_MIN", OS_MIN},
    {"OS_PEND", OS_PEND},
    {"OS_PENDING_NEW", OS_PENDING_NEW},
    {"OS_NEW", OS_NEW},
    {"OS_PARTFILLED", OS_PARTFILLED},
    {"OS_FILLED", OS_FILLED},
    {"OS_REJECTED", OS_REJECTED},
    {"OS_CANCEL", OS_CANCEL},
    {"OS_CANCELLING", OS_CANCELLING},
    {"OS_CANCELED", OS_CANCELED},
    {"OS_UNKNOWN", OS_UNKNOWN},
    {"OS_FAILED", OS_FAILED}
};

// ===========================================================================
// ApiSource —— 报单回报的来源（tb 本地合成 vs 交易所真实回报）
// ===========================================================================
enum ApiSource {
    AS_MIN = 0,
    AS_ADD_NEW_ORDER,
    AS_CANCEL_ORDER,
    AS_QUERY_ORDER,
    AS_REST,
    AS_WEBSOCKET
};

static std::unordered_map<ApiSource, std::string> ApiSourceEnum2StrMap {
    {AS_MIN, "AS_MIN"},
    {AS_ADD_NEW_ORDER, "AS_ADD_NEW_ORDER"},
    {AS_CANCEL_ORDER, "AS_CANCEL_ORDER"},
    {AS_QUERY_ORDER, "AS_QUERY_ORDER"},
    {AS_REST, "AS_REST"},
    {AS_WEBSOCKET, "AS_WEBSOCKET"}
};

static std::unordered_map<std::string, ApiSource> ApiSourceStr2EnumMap {
    {"AS_MIN", AS_MIN},
    {"AS_ADD_NEW_ORDER", AS_ADD_NEW_ORDER},
    {"AS_CANCEL_ORDER", AS_CANCEL_ORDER},
    {"AS_QUERY_ORDER", AS_QUERY_ORDER},
    {"AS_REST", AS_REST},
    {"AS_WEBSOCKET", AS_WEBSOCKET}
};

// ===========================================================================
// md::MarketType
// ===========================================================================
namespace md {

// ---------------------------------------------------------------------------
// InstrumentInfo —— 逐字照抄真实头 include/data_struct.h:286-330。
//
// ⚠️ 字段顺序很重要：旧桩把这个结构体放在 securitymanager.h 里，只留了 4 个字段
//    （value / tickSize / minSize / calcType），于是测试里写的是
//        md::InstrumentInfo{1.0, 0.0001, 0.1, 0}
//    换成真实字段顺序（前 7 个是枚举/字符串）之后这种聚合初始化就错位了，
//    所以 suite.cpp 里改成按字段名赋值。
//    另外真实的 quant_library/basic/DataStruct.h 里 `md::InstrumentInfo` 是直接
//    引用的（字段 activeInfo/passiveInfo），所以这个类型必须在这里就可见。
// ---------------------------------------------------------------------------
struct InstrumentInfo {
    ExchangeType exchangeTypeEnum{ExchangeType_MIN};
    InstType instTypeEnum{InstType_MIN};
    char instId[32]{""};
    char originInstId[32]{""};
    char base[16]{""};
    char quote[16]{""};
    char margin[16]{""};
    double value{1.0};          // 合约面值
    double tickSize{0.0};       // 价格精度，比如0.001
    double lotSize{0.0};        // 下单数量精度，比如0.00001
    int priceDigits{0};         // 价格小数位数（去尾零后）
    int sizeDigits{0};          // 数量小数位数（去尾零后）
    int64_t pricePow10{0};      // 10^priceDigits
    int64_t sizePow10{0};       // 10^sizeDigits
    int64_t tickSizeInt{0};     // tickSize × pricePow10
    int64_t lotSizeInt{0};      // lotSize  × sizePow10
    double minSize{0.0};        // 下单最小数量
    double maxSize{0.0};        // 最大下单数量
    double minAmount{0.0};      // 最小下单金额
    double magnifyNumber{1.0};  // 放大倍数
    double reduceNumber{1.0};   // magnifyNumber 倒数
    int calcType{0};            // 现货/usdt本位是0，币本位是1
    int64_t instIdCode{0};      // okx code

    std::string getString() const {
        return std::string("InstrumentInfo");
    }
};

enum MarketType {
    MarketType_MIN = 0,
    TRADES,
    FUNDING_RATE,
    DEPTH1,
    DEPTH5,
    DEPTH10,
    DEPTH20,
    KLINE_1m,
    KLINE_1h,
    KLINE_2h,
    KLINE_4h,
    KLINE_8h,
    MarketType_MAX
};

static std::unordered_map<MarketType, std::string> MarketTypeEnum2StrMap {
    {MarketType_MIN, "MarketType_MIN"},
    {TRADES, "TRADES"},
    {FUNDING_RATE, "FUNDING_RATE"},
    {DEPTH1, "DEPTH1"},
    {DEPTH5, "DEPTH5"},
    {DEPTH10, "DEPTH10"},
    {DEPTH20, "DEPTH20"},
    {KLINE_1m, "KLINE_1m"},
    {KLINE_1h, "KLINE_1h"},
    {KLINE_2h, "KLINE_2h"},
    {KLINE_4h, "KLINE_4h"},
    {KLINE_8h, "KLINE_8h"}
};

static std::unordered_map<std::string, MarketType> MarketTypeStr2EnumMap {
    {"MarketType_MIN", MarketType_MIN},
    {"TRADES", TRADES},
    {"FUNDING_RATE", FUNDING_RATE},
    {"DEPTH1", DEPTH1},
    {"DEPTH5", DEPTH5},
    {"DEPTH10", DEPTH10},
    {"DEPTH20", DEPTH20},
    {"KLINE_1m", KLINE_1m},
    {"KLINE_1h", KLINE_1h},
    {"KLINE_2h", KLINE_2h},
    {"KLINE_4h", KLINE_4h},
    {"KLINE_8h", KLINE_8h}
};

} // namespace md
