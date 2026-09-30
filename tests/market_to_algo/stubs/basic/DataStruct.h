// Stub for quant_library/basic/DataStruct.h.
// The real one pulls fmt/core.h, dbp/include.h, pubsub_protocol.h, time_util.h and
// log_engine.h. The strategy-side .cpp files only need the stra:: constants/enums,
// stra::AlgoOrderModify, the LOG_* macros and crypto::getCurrentTime.
//
// Everything below is copied verbatim from the real header where it matters
// (constants, enum value ORDER, AlgoOrderModify field set) so that assertions in the
// test suite are asserting against the production contract, not against a guess.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "StraException.h"
#include "crypto_errors.h"
#include "data_struct.h"
#include "dbp/include.h"
#include "pubsub_protocol.h"
#include "time_util.h"
#include "log_engine.h"

using namespace std;

// Swallow fmt-style arguments; proves the call sites compile without a formatter.
template <typename... A> inline void log_sink(const char*, A&&...) {}
#define LOG_DEBUG(...) log_sink(__VA_ARGS__)
#define LOG_INFO(...)  log_sink(__VA_ARGS__)
#define LOG_WARN(...)  log_sink(__VA_ARGS__)
#define LOG_ERROR(...) log_sink(__VA_ARGS__)

namespace stra {
    const double MIN_FLOAT = 0.0000000001;
    const int ID_LEN = 128;
    const int INST_ID_LEN = 64;
    const int ASSET_LEN = 32;
    const int INST_TYPE_LEN = 32;
    const int EXCH_ID_LEN = 16;
    const int INST_KEY_LEN = 128;
    const int ASSET_AMOUNT_LEN = 4;
    const int TIME_STATUS_LEN = 16;
    const int NAME_LEN = 32;
    const int MARKET_TYPE_LEN = 16;
    const int DIRECTION_LEN = 16;
    const int OFFSET_FLAG_LEN = 32;
    const int ORDER_TYPE_LEN = 32;
    const int ORDER_ID_LEN = 32;
    const int ORDER_STATUS_LEN = 32;
    const int MSG_LEN = 512;
    const int STR_LEN = 1024;

    enum CommandType {
        CommandType_MIN = 0,
        CommandType_NEW,
        CommandType_PendingNew,
        CommandType_TRADING,
        CommandType_CANCEL,
        CommandType_CANCELLING,
        CommandType_UCANCELLING,
        CommandType_CANCELED,
        CommandType_MODIFY,
        CommandType_MODIFYING,
        CommandType_MODIFIED,
        CommandType_QUERY,
        CommandType_QUERYING,
        CommandType_QUERIED,
        CommandType_UPDATE,
        CommandType_ERROR,
        CommandType_FINISHED,
        CommandType_MAX
    };

    enum AlgoOrderStatus {
        ALGO_OS_MIN = 0,
        ALGO_OS_PEND,
        ALGO_OS_PENDING_NEW,
        ALGO_OS_NEW,
        ALGO_OS_PARTFILLED,
        ALGO_OS_FILLED,
        ALGO_OS_REJECTED,
        ALGO_OS_CANCEL,
        ALGO_OS_CANCELLING,
        ALGO_OS_CANCELED,
        ALGO_OS_ERRORCANCELLING,
        ALGO_OS_ERRORCANCELED,
        ALGO_OS_UNKNOWN,
        ALGO_OS_FAILED,
        ALGO_OS_MAX
    };

    // Verbatim copy of the real struct (DataStruct.h:146-194).
    struct AlgoOrderModify {
        bool profitSwitch{false};
        double profitPct{0.0};

        bool ttOLSwitch{false};
        bool ttOSSwitch{false};
        bool ttCLSwitch{false};
        bool ttCSSwitch{false};
        bool mtOLSwitch{false};
        bool mtOSSwitch{false};
        bool mtCLSwitch{false};
        bool mtCSSwitch{false};

        double ttOLStartSpread{0.0};
        double ttOLEndSpread{0.0};
        double ttOLStartVolume{0.0};
        double ttOLEndVolume{0.0};
        double ttCLStartSpread{0.0};
        double ttCLEndSpread{0.0};
        double ttCLStartVolume{0.0};
        double ttCLEndVolume{0.0};
        double ttOSStartSpread{0.0};
        double ttOSEndSpread{0.0};
        double ttOSStartVolume{0.0};
        double ttOSEndVolume{0.0};
        double ttCSStartSpread{0.0};
        double ttCSEndSpread{0.0};
        double ttCSStartVolume{0.0};
        double ttCSEndVolume{0.0};

        double mtOLStartSpread{0.0};
        double mtOLEndSpread{0.0};
        double mtOLStartVolume{0.0};
        double mtOLEndVolume{0.0};
        double mtCLStartSpread{0.0};
        double mtCLEndSpread{0.0};
        double mtCLStartVolume{0.0};
        double mtCLEndVolume{0.0};
        double mtOSStartSpread{0.0};
        double mtOSEndSpread{0.0};
        double mtOSStartVolume{0.0};
        double mtOSEndVolume{0.0};
        double mtCSStartSpread{0.0};
        double mtCSEndSpread{0.0};
        double mtCSStartVolume{0.0};
        double mtCSEndVolume{0.0};
    };

    enum DriveType {
        DriveType_MIN = 0,
        DriveType_ACTIVE,
        DriveType_PASSIVE,
        DriveType_BOTH,
        DriveType_MAX
    };

    enum CheckType {
        CheckType_MIN = 0,
        CheckType_GE_VOLUME,
        CheckType_GE_AMOUNT,
        CheckType_LT_VOLUME,
        CheckType_LT_AMOUNT,
        CheckType_MAX
    };

    enum AlgoType {
        AlgoType_MIN = 0,
        AlgoType_Basic,
        AlgoType_PairTrading,
        AlgoType_FishingTrading,
        AlgoType_Rebalance,
        AlgoType_MAX
    };

    enum PriceType {
        PriceType_MIN = 0,
        PriceType_LIMIT,
        PriceType_MARKET,
        PriceType_MAX
    };

    enum TargetSpredPrice {
        TargetSpredPrice_MIN,
        TargetSpredPrice_NOW,
        TargetSpredPrice_NOW_MEAN,
        TargetSpredPrice_MAX
    };

    enum ActiveVolumeCalcualteType {
        ActiveVolumeCalcualteType_MIN,
        ActiveVolumeCalcualteType_PassiveVolumePct,
        ActiveVolumeCalcualteType_MAX
    };

    enum TradingType {
        TradingType_MIN = 0,
        MAKER_TAKER,
        TAKER_TAKER,
        OPEN_SHORT,
        OPEN_LONG,
        CLOSE_SHORT,
        CLOSE_LONG,
        TradingType_MAX
    };

    // Only the fields the compiled translation units actually reference.
    struct QuantOrder {
        int64_t orderId{0};
        int64_t strategyOrderId{0};
        OrderStatus orderStatus{OS_MIN};
        Direction direction{DT_MIN};
        double price{0.0};
        double volume{0.0};
    };
}
