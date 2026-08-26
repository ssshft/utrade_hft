#ifndef _DATASTRUCT_H
#define _DATASTRUCT_H

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <unordered_map>
#include <cstring>
#include "BasicInfoMgr.h"
#include "Utility.h"
#include "StraException.h"
#include "crypto_errors.h"
#include "json/nlohmann/json.hpp"
#include "log_engine.h"

using namespace std;
using json = nlohmann::json;


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

    static unordered_map<CommandType, string> CommandTypeEnum2Str {
	    {CommandType_MIN, "CommandType_MIN"},
        {CommandType_NEW, "CommandType_NEW"},
        {CommandType_PendingNew, "CommandType_PendingNew"},
        {CommandType_CANCEL, "CommandType_CANCEL"},
        {CommandType_CANCELLING, "CommandType_CANCELLING"},
        {CommandType_MODIFY, "CommandType_MODIFY"},
        {CommandType_MODIFYING, "CommandType_MODIFYING"},
        {CommandType_QUERY, "CommandType_QUERY"},
        {CommandType_QUERYING, "CommandType_QUERYING"},
	    {CommandType_MAX, "CommandType_MAX"}
    };

    static unordered_map<string, CommandType> CommandTypeStr2Enum {
	    {"CommandType_MIN", CommandType_MIN},
        {"CommandType_NEW", CommandType_NEW},
        {"CommandType_PendingNew", CommandType_PendingNew},
        {"CommandType_CANCEL", CommandType_CANCEL},
        {"CommandType_CANCELLING", CommandType_CANCELLING},
        {"CommandType_MODIFY", CommandType_MODIFY},
        {"CommandType_MODIFYING", CommandType_MODIFYING},
        {"CommandType_QUERY", CommandType_QUERY},
        {"CommandType_QUERYING", CommandType_QUERYING},
        {"CommandType_MAX", CommandType_MAX}
    };

    enum PriceType {
        PriceType_MIN = 0,
        PriceType_LIMIT,
        PriceType_MARKET,
        PriceType_MAX
    };

    static unordered_map<PriceType, string> PriceTypeEnum2Str {
        {PriceType_MIN, "PriceType_MIN"},
        {PriceType_LIMIT, "PriceType_LIMIT"},
        {PriceType_MARKET, "PriceType_MARKET"},
        {PriceType_MAX, "PriceType_MAX"}
    };

    static unordered_map<string, PriceType> PriceTypeStr2Enum {
        {"PriceType_MIN", PriceType_MIN},
        {"PriceType_LIMIT", PriceType_LIMIT},
        {"PriceType_MARKET", PriceType_MARKET},
        {"PriceType_MAX", PriceType_MAX}
    };

    enum DriveType {
        DriveType_MIN = 0,
        DriveType_ACTIVE,
        DriveType_PASSIVE,
        DriveType_BOTH,
        DriveType_MAX
    };

    static unordered_map<DriveType, string> DriveTypeEnum2Str {
        {DriveType_MIN, "DriveType_MIN"},
        {DriveType_ACTIVE, "DriveType_ACTIVE"},
        {DriveType_PASSIVE, "DriveType_PASSIVE"},
        {DriveType_BOTH, "DriveType_BOTH"},
        {DriveType_MAX, "DriveType_MAX"}
    };

    static unordered_map<string, DriveType> DriveTypeStr2Enum {
        {"DriveType_MIN", DriveType_MIN},
        {"DriveType_ACTIVE", DriveType_ACTIVE},
        {"DriveType_PASSIVE", DriveType_PASSIVE},
        {"DriveType_BOTH", DriveType_BOTH},
        {"DriveType_MAX", DriveType_MAX}
    };

    enum CheckType {
        CheckType_MIN = 0,
        CheckType_GE_VOLUME,
        CheckType_GE_AMOUNT,
        CheckType_LT_VOLUME,
        CheckType_LT_AMOUNT,
        CheckType_MAX
    };

    static unordered_map<CheckType, string> CheckTypeEnum2Str {
        {CheckType_MIN, "CheckType_MIN"},
        {CheckType_GE_VOLUME, "CheckType_GE_VOLUME"},
        {CheckType_GE_AMOUNT, "CheckType_GE_AMOUNT"},
        {CheckType_LT_VOLUME, "CheckType_LT_VOLUME"},
        {CheckType_LT_AMOUNT, "CheckType_LT_AMOUNT"},
        {CheckType_MAX, "CheckType_MAX"}
    };

    static unordered_map<string, CheckType> CheckTypeStr2Enum {
        {"CheckType_MIN", CheckType_MIN},
        {"CheckType_GE_VOLUME", CheckType_GE_VOLUME},
        {"CheckType_GE_AMOUNT", CheckType_GE_AMOUNT},
        {"CheckType_LT_VOLUME", CheckType_LT_VOLUME},
        {"CheckType_LT_AMOUNT", CheckType_LT_AMOUNT},
        {"CheckType_MAX", CheckType_MAX}
    };

    enum OrderEffectMethod {
        OEM_MIN,
        OEM_GTC,
        OEM_IOC,
        OEM_FOK,
        OEM_GTX,
        OEM_MAX
    };

    static unordered_map<OrderEffectMethod, string> OrderEffectMethodEnum2Str {
        {OEM_MIN, "OEM_MIN"},
        {OEM_GTC, "OEM_GTC"},
        {OEM_IOC, "OEM_IOC"},
        {OEM_FOK, "OEM_FOK"},
        {OEM_GTX, "OEM_GTX"},
        {OEM_MAX, "OEM_MAX"}
    };

    static unordered_map<string, OrderEffectMethod> OrderEffectMethodStr2Enum {
        {"OEM_MIN", OEM_MIN},
        {"OEM_GTC", OEM_GTC},
        {"OEM_IOC", OEM_IOC},
        {"OEM_FOK", OEM_FOK},
        {"OEM_GTX", OEM_GTX},
        {"OEM_MAX", OEM_MAX}
    };

    enum AccountType {
        AT_MIN = 0,
        AT_FUND,
        AT_SPOT,
        AT_UFUTURES,
        AT_CFUTURES,
        AT_MARGIN,
        AT_SWAP,
        AT_DELIVERY,
        AT_PERPETUAL,
        AT_VIRTUAL,
        AT_UNIFIED,
        AT_CLASSIC,
        AT_MAX
    };

    static unordered_map<AccountType, string> AccountTypeEnum2Str {
        {AT_MIN, "AT_MIN"},
        {AT_FUND, "AT_FUND"},
        {AT_SPOT, "AT_SPOT"},
        {AT_UFUTURES, "AT_UFUTURES"},
        {AT_CFUTURES, "AT_CFUTURES"},
        {AT_MARGIN, "AT_MARGIN"},
        {AT_SWAP, "AT_SWAP"},
        {AT_DELIVERY, "AT_DELIVERY"},
        {AT_PERPETUAL, "AT_PERPETUAL"},
        {AT_VIRTUAL, "AT_VIRTUAL"},
        {AT_UNIFIED, "AT_UNIFIED"},
        {AT_CLASSIC, "AT_CLASSIC"},
        {AT_MAX, "AT_MAX"}
    };

    static unordered_map<string, AccountType> AccountTypeStr2Enum {
        {"AT_MIN", AT_MIN},
        {"AT_FUND", AT_FUND},
        {"AT_SPOT", AT_SPOT},
        {"AT_UFUTURES", AT_UFUTURES},
        {"AT_CFUTURES", AT_CFUTURES},
        {"AT_MARGIN", AT_MARGIN},
        {"AT_SWAP", AT_SWAP},
        {"AT_DELIVERY", AT_DELIVERY},
        {"AT_PERPETUAL", AT_PERPETUAL},
        {"AT_VIRTUAL", AT_VIRTUAL},
        {"AT_UNIFIED", AT_UNIFIED},
        {"AT_CLASSIC", AT_CLASSIC},
        {"AT_MAX", AT_MAX}
    };

    enum MarginType {
        MarginType_MIN = 0,
        MarginType_NORMAL,
        MarginType_BORROW,
        MarginType_REPAY,
        MarginType_MAX
    };

    static unordered_map<MarginType, string> MarginTypeEnum2Str {
        {MarginType_MIN, "MarginType_MIN"},
        {MarginType_NORMAL, "MarginType_NORMAL"},
        {MarginType_BORROW, "MarginType_BORROW"},
        {MarginType_REPAY, "MarginType_REPAY"},
        {MarginType_MAX, "MarginType_MAX"}
    };

    static unordered_map<string, MarginType> MarginTypeStr2Enum {
        {"MarginType_MIN", MarginType_MIN},
        {"MarginType_NORMAL", MarginType_NORMAL},
        {"MarginType_BORROW", MarginType_BORROW},
        {"MarginType_REPAY", MarginType_REPAY},
        {"MarginType_MAX", MarginType_MAX}
    };

    enum AccountMarginType {
        AccountMarginType_MIN = 0,
        AccountMarginType_ISOLATED,
        AccountMarginType_CROSSED,
        AccountMarginType_MAX
    };

    static unordered_map<AccountMarginType, string> AccountMarginTypeEnum2Str {
        {AccountMarginType_MIN, "AccountMarginType_MIN"},
        {AccountMarginType_ISOLATED, "AccountMarginType_ISOLATED"},
        {AccountMarginType_CROSSED, "AccountMarginType_CROSSED"},
        {AccountMarginType_MAX, "AccountMarginType_MAX"}
    };

    static unordered_map<string, AccountMarginType> AccountMarginTypeStr2Enum {
        {"AccountMarginType_MIN", AccountMarginType_MIN},
        {"AccountMarginType_ISOLATED", AccountMarginType_ISOLATED},
        {"AccountMarginType_CROSSED", AccountMarginType_CROSSED},
        {"AccountMarginType_MAX", AccountMarginType_MAX}
    };

    enum LendingType {
        LendingType_MIN = 0,
        LendingType_BORROW,
        LendingType_REPAY,
        LendingType_MAX
    };

    static unordered_map<LendingType, string> LendingTypeEnum2Str {
        {LendingType_MIN, "LendingType_MIN"},
        {LendingType_BORROW, "LendingType_BORROW"},
        {LendingType_REPAY, "LendingType_REPAY"},
        {LendingType_MAX, "LendingType_MAX"}
    };

    static unordered_map<string, LendingType> LendingTypeStr2Enum {
        {"LendingType_MIN", LendingType_MIN},
        {"LendingType_BORROW", LendingType_BORROW},
        {"LendingType_REPAY", LendingType_REPAY},
        {"LendingType_MAX", LendingType_MAX}
    };

    enum OffsetFlag {
        OffsetFlag_MIN = 0,
        OffsetFlag_OPEN,
        OffsetFlag_CLOSE,
        OffsetFlag_CANCEL,
        OffsetFlag_MAX
    };

    static unordered_map<OffsetFlag, string> OffsetFlagEnum2Str {
        {OffsetFlag_MIN, "OffsetFlag_MIN"},
        {OffsetFlag_OPEN, "OffsetFlag_OPEN"},
        {OffsetFlag_CLOSE, "OffsetFlag_CLOSE"},
        {OffsetFlag_CANCEL, "OffsetFlag_CANCEL"},
        {OffsetFlag_MAX, "OffsetFlag_MAX"}
    };

    static unordered_map<std::string, OffsetFlag> OffsetFlagStr2Enum {
        {"OffsetFlag_MIN", OffsetFlag_MIN},
        {"OffsetFlag_OPEN", OffsetFlag_OPEN},
        {"OffsetFlag_CLOSE", OffsetFlag_CLOSE},
        {"OffsetFlag_CANCEL", OffsetFlag_CANCEL},
        {"OffsetFlag_MAX", OffsetFlag_MAX}
    };

    enum PosDirection {
        PosDirection_MIN = 0,
        PosDirection_OPEN,
        PosDirection_CLOSE,
        PosDirection_NET,
        PosDirection_MAX
    };

    static unordered_map<PosDirection, string> PosDirectionEnum2Str {
        {PosDirection_MIN, "PosDirection_MIN"},
        {PosDirection_OPEN, "PosDirection_OPEN"},
        {PosDirection_CLOSE, "PosDirection_CLOSE"},
        {PosDirection_NET, "PosDirection_NET"},
        {PosDirection_MAX, "PosDirection_MAX"}
    };

    static unordered_map<string, PosDirection> PosDirectionStr2Enum {
        {"PosDirection_MIN", PosDirection_MIN},
        {"PosDirection_OPEN", PosDirection_OPEN},
        {"PosDirection_CLOSE", PosDirection_CLOSE},
        {"PosDirection_NET", PosDirection_NET},
        {"PosDirection_MAX", PosDirection_MAX}
    };

    enum AlgoType {
        AlgoType_MIN = 0,
        AlgoType_Basic,
        AlgoType_PairTrading,
        AlgoType_FishingTrading,
        AlgoType_Rebalance,
        AlgoType_MAX
    };

    static unordered_map<AlgoType, string> AlgoTypeEnum2Str {
        {AlgoType_MIN, "AlgoType_MIN"},
        {AlgoType_Basic, "AlgoType_Basic"},
        {AlgoType_PairTrading, "AlgoType_PairTrading"},
        {AlgoType_FishingTrading, "AlgoType_FishingTrading"},
        {AlgoType_Rebalance, "AlgoType_Rebalance"},
        {AlgoType_MAX, "AlgoType_MAX"}
    };

    static unordered_map<string, AlgoType> AlgoTypeStr2Enum {
        {"AlgoType_MIN", AlgoType_MIN},
        {"AlgoType_Basic", AlgoType_Basic},
        {"AlgoType_PairTrading", AlgoType_PairTrading},
        {"AlgoType_FishingTrading", AlgoType_FishingTrading},
        {"AlgoType_Rebalance", AlgoType_Rebalance},
        {"AlgoType_MAX", AlgoType_MAX}
    };

    // 行情字段定义
    // spread 部分
    enum SpreadDrive{
        SpreadDrive_MIN = 0,
        SpreadDrive_Active,
        SpreadDrive_Passive,
        SpreadDrive_MAX
    };

    static unordered_map<SpreadDrive, string> SpreadDriveEnum2Str {
        {SpreadDrive_MIN, "SpreadDrive_MIN"},
        {SpreadDrive_Active, "SpreadDrive_Active"},
        {SpreadDrive_Passive, "SpreadDrive_Passive"},
        {SpreadDrive_MAX, "SpreadDrive_MAX"}
    };

    static unordered_map<string, SpreadDrive> SpreadDriveStr2Enum {
        {"SpreadDrive_MIN", SpreadDrive_MIN},
        {"SpreadDrive_Active", SpreadDrive_Active},
        {"SpreadDrive_Passive", SpreadDrive_Passive},
        {"SpreadDrive_MAX", SpreadDrive_MAX}
    };

    enum SpreadType{
        SpreadType_MIN = 0,
        SpreadType_AMPDA,  // (A*a-P*p)/A
        SpreadType_PMADA,  // (P*p-A*a)/A for pair_algo_manager
        SpreadType_AMP,  // A*a-P*p
        SpreadType_PMA,  // P*p-A*a
        SpreadType_MAX
    };

    static unordered_map<SpreadType, string> SpreadTypeEnum2Str {
        {SpreadType_MIN, "SpreadType_MIN"},
        {SpreadType_AMPDA, "SpreadType_AMPDA"},
        {SpreadType_PMADA, "SpreadType_PMADA"},
        {SpreadType_AMP, "SpreadType_AMP"},
        {SpreadType_PMA, "SpreadType_PMA"},
        {SpreadType_MAX, "SpreadType_MAX"}
    };

    static unordered_map<string, SpreadType> SpreadTypeStr2Enum {
        {"SpreadType_MIN", SpreadType_MIN},
        {"SpreadType_AMPDA", SpreadType_AMPDA},
        {"SpreadType_PMADA", SpreadType_PMADA},
        {"SpreadType_AMP", SpreadType_AMP},
        {"SpreadType_PMA", SpreadType_PMA},
        {"SpreadType_MAX", SpreadType_MAX}
    };

    enum TargetSpredPrice {
        TargetSpredPrice_MIN,
        TargetSpredPrice_NOW,
        TargetSpredPrice_NOW_MEAN,
        TargetSpredPrice_MAX
    };

    static unordered_map<TargetSpredPrice, string> TargetSpredPriceEnum2Str {
        {TargetSpredPrice_MIN, "TargetSpredPrice_MIN"},
        {TargetSpredPrice_NOW, "TargetSpredPrice_NOW"},
        {TargetSpredPrice_NOW_MEAN, "TargetSpredPrice_NOW_MEAN"},
        {TargetSpredPrice_MAX, "TargetSpredPrice_MAX"}
    };

    static unordered_map<string, TargetSpredPrice> TargetSpredPriceStr2Enum {
        {"TargetSpredPrice_MIN", TargetSpredPrice_MIN},
        {"TargetSpredPrice_NOW", TargetSpredPrice_NOW},
        {"TargetSpredPrice_NOW_MEAN", TargetSpredPrice_NOW_MEAN},
        {"TargetSpredPrice_MAX", TargetSpredPrice_MAX}
    };

    enum ActiveVolumeCalcualteType {
        ActiveVolumeCalcualteType_MIN,
        ActiveVolumeCalcualteType_PassiveVolumePct,
        ActiveVolumeCalcualteType_MAX
    };

    static unordered_map<ActiveVolumeCalcualteType, string> ActiveVolumeCalcualteTypeEnum2Str {
        {ActiveVolumeCalcualteType_MIN, "ActiveVolumeCalcualteType_MIN"},
        {ActiveVolumeCalcualteType_PassiveVolumePct, "ActiveVolumeCalcualteType_PassiveVolumePct"},
        {ActiveVolumeCalcualteType_MAX, "ActiveVolumeCalcualteType_MAX"}
    };

    static unordered_map<string, ActiveVolumeCalcualteType> ActiveVolumeCalcualteTypeStr2Enum {
        {"ActiveVolumeCalcualteType_MIN", ActiveVolumeCalcualteType_MIN},
        {"ActiveVolumeCalcualteType_PassiveVolumePct", ActiveVolumeCalcualteType_PassiveVolumePct},
        {"ActiveVolumeCalcualteType_MAX", ActiveVolumeCalcualteType_MAX}
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

    static unordered_map<TradingType, string> TradingTypeEnum2Str {
        {TradingType_MIN, "TradingType_MIN"},
        {MAKER_TAKER, "MAKER_TAKER"},
        {TAKER_TAKER, "TAKER_TAKER"},
        {OPEN_SHORT, "OPEN_SHORT"},
        {OPEN_LONG, "OPEN_LONG"},
        {CLOSE_SHORT, "CLOSE_SHORT"},
        {CLOSE_LONG, "CLOSE_LONG"},
        {TradingType_MAX, "TradingType_MAX"}
    };

    static unordered_map<string, TradingType> TradingTypeStr2Enum {
        {"TradingType_MIN", TradingType_MIN},
        {"MAKER_TAKER", MAKER_TAKER},
        {"TAKER_TAKER", TAKER_TAKER},
        {"OPEN_SHORT", OPEN_SHORT},
        {"OPEN_LONG", OPEN_LONG},
        {"CLOSE_SHORT", CLOSE_SHORT},
        {"CLOSE_LONG", CLOSE_LONG},
        {"TradingType_MAX", TradingType_MAX}
    };

    struct TimeStatusDetail {
        int64_t updateTime{0};
        OrderStatus orderStatus{OrderStatus_MIN};
        
        string GetStr() const {
            char s[MSG_LEN];
            sprintf(s, "%ld-%s", updateTime, OrderStatusEnum2Str[orderStatus].c_str());
            return string(s);
        }

        string GetEnumStr() const {
            char s[MSG_LEN];
            sprintf(s, "%ld-%d", updateTime, orderStatus);
            return string(s);
        }
    };

    struct TimeStatus {
        int size{0};
        TimeStatusDetail detail[TIME_STATUS_LEN];

        string GetStr() const {
            string ss = "";
            for (int i = 0; i < size; ++i) {
                char s[MSG_LEN];
                sprintf(s, "%s|", detail[i].GetStr().c_str());
                ss += s;
            }
            return ss;
        }

        string GetEnumStr() const {
            string ss = "";
            for (int i = 0; i < size; ++i) {
                char s[MSG_LEN];
                sprintf(s, "%s|", detail[i].GetEnumStr().c_str());
                ss += s;
            }
            return ss;
        }
    };

    struct AssetAmountDetail {
        char asset[ASSET_LEN]{0};
        double amount{0.0};

        string GetStr() const {
            char s[MSG_LEN];
            sprintf(s, "%s-%f", asset, amount);
            return string(s);
        }
    };

    struct AssetAmount {
        int size{0};
        AssetAmountDetail detail[ASSET_AMOUNT_LEN];

        string GetStr() const {
            string ss = "";
            for (int i = 0; i < size; ++i) {
                char s[MSG_LEN];
                sprintf(s, "%s|", detail[i].GetStr().c_str());
                ss += s;
            }
            return ss;
        }
    };

    struct QuantSpread {
        SpreadDrive spreadDrive;  // 价差驱动
        SpreadType spreadType; // 价差类型

        int spreadEffective; // 价差是否有效
        int statEffective; // 统计量是否有效

        char pairInstrumentKey[INST_KEY_LEN];
        char activeInstumentKey[INST_KEY_LEN];
        char passiveInstrumentKey[INST_KEY_LEN];

        double spreadBidAsk{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBid{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBid{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAsk{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double activePriceTema{0.0};  // 主动腿Tema价格
        double passivePriceTema{0.0};  // 被动腿Tema价格

        double activeFundingRate{0.0};
        double passiveFundingRate{0.0};
        double activeMultiply;  // 主动腿调整系数(适用于1000shib与shib的情况)
        double passiveMultiply;  //  被动腿调整系数

        double passiveAskPrice1{0.0};
        double passiveAskVolume1{0.0};
        double passiveAskPrice2{0.0};
        double passiveAskVolume2{0.0};
        double passiveAskPrice3{0.0};
        double passiveAskVolume3{0.0};
        double passiveAskPrice4{0.0};
        double passiveAskVolume4{0.0};
        double passiveAskPrice5{0.0};
        double passiveAskVolume5{0.0};

        double passiveBidPrice1{0.0};
        double passiveBidVolume1{0.0};
        double passiveBidPrice2{0.0};
        double passiveBidVolume2{0.0};
        double passiveBidPrice3{0.0};
        double passiveBidVolume3{0.0};
        double passiveBidPrice4{0.0};
        double passiveBidVolume4{0.0};
        double passiveBidPrice5{0.0};
        double passiveBidVolume5{0.0};

        double activeAskPrice1{0.0};
        double activeAskVolume1{0.0};
        double activeAskPrice2{0.0};
        double activeAskVolume2{0.0};
        double activeAskPrice3{0.0};
        double activeAskVolume3{0.0};
        double activeAskPrice4{0.0};
        double activeAskVolume4{0.0};
        double activeAskPrice5{0.0};
        double activeAskVolume5{0.0};

        double activeBidPrice1{0.0};
        double activeBidVolume1{0.0};
        double activeBidPrice2{0.0};
        double activeBidVolume2{0.0};
        double activeBidPrice3{0.0};
        double activeBidVolume3{0.0};
        double activeBidPrice4{0.0};
        double activeBidVolume4{0.0};
        double activeBidPrice5{0.0};
        double activeBidVolume5{0.0};

        int64_t activeFundingTs{0};  // 主动腿funding收取时间
        int64_t passiveFundingTs{0};  // 被动退funding收取时间
        int64_t activeDepthTs{0};  // 主动腿depth时间
        int64_t passiveDepthTs{0};  // 被动退depth时间
        int64_t generateTs{0};  // 价差生成时间
        int64_t diffTs{0};  // 主动退被动腿时间差

        int64_t activeDepthDelay{0};
        int64_t passiveDepthDelay{0};

        int64_t exchActiveTradeDelay{0};
        int64_t exchPassiveTradeDelay{0};
    };

    struct MdSpread {
        SpreadDrive spreadDrive;  // 价差驱动
        SpreadType spreadType; // 价差类型

        int spreadEffective; // 价差是否有效
        int statEffective; // 统计量是否有效

        char pairInstrumentKey[INST_KEY_LEN];
        char activeInstumentKey[INST_KEY_LEN];
        char passiveInstrumentKey[INST_KEY_LEN];

        double spreadBidAsk{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBid{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBid{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAsk{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidTema{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskTema{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMax{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMax{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double spreadBidAskMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用ask1, 对应了maker_long_active,maker_short_passive或者taker_short_active,taker_long_passive
        double spreadBidBidMin{0.0};  // 先主动腿价格使用bid1, 被动腿价格使用bid1, 对应了maker_long_active,taker_short_passive或者taker_short_active,maker_long_passive
        double spreadAskBidMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用bid1, 对应了taker_long_active,taker_short_passive或者maker_short_active,maker_long_passive
        double spreadAskAskMin{0.0};  // 先主动腿价格使用ask1, 被动腿价格使用ask1, 对应了taker_long_active,maker_short_passive或者maker_short_active,taker_long_passive

        double activePriceTema{0.0};  // 主动腿Tema价格
        double passivePriceTema{0.0};  // 被动腿Tema价格

        double activeFundingRate{0.0};
        double passiveFundingRate{0.0};
        double activeMultiply;  // 主动腿调整系数(适用于1000shib与shib的情况)
        double passiveMultiply;  //  被动腿调整系数

        double passiveAskPrice1{0.0};
        double passiveAskVolume1{0.0};
        double passiveAskPrice2{0.0};
        double passiveAskVolume2{0.0};
        double passiveAskPrice3{0.0};
        double passiveAskVolume3{0.0};
        double passiveAskPrice4{0.0};
        double passiveAskVolume4{0.0};
        double passiveAskPrice5{0.0};
        double passiveAskVolume5{0.0};

        double passiveBidPrice1{0.0};
        double passiveBidVolume1{0.0};
        double passiveBidPrice2{0.0};
        double passiveBidVolume2{0.0};
        double passiveBidPrice3{0.0};
        double passiveBidVolume3{0.0};
        double passiveBidPrice4{0.0};
        double passiveBidVolume4{0.0};
        double passiveBidPrice5{0.0};
        double passiveBidVolume5{0.0};

        double activeAskPrice1{0.0};
        double activeAskVolume1{0.0};
        double activeAskPrice2{0.0};
        double activeAskVolume2{0.0};
        double activeAskPrice3{0.0};
        double activeAskVolume3{0.0};
        double activeAskPrice4{0.0};
        double activeAskVolume4{0.0};
        double activeAskPrice5{0.0};
        double activeAskVolume5{0.0};

        double activeBidPrice1{0.0};
        double activeBidVolume1{0.0};
        double activeBidPrice2{0.0};
        double activeBidVolume2{0.0};
        double activeBidPrice3{0.0};
        double activeBidVolume3{0.0};
        double activeBidPrice4{0.0};
        double activeBidVolume4{0.0};
        double activeBidPrice5{0.0};
        double activeBidVolume5{0.0};

        int64_t activeFundingTs{0};  // 主动腿funding收取时间
        int64_t passiveFundingTs{0};  // 被动退funding收取时间
        int64_t activeDepthTs{0};  // 主动腿depth时间
        int64_t passiveDepthTs{0};  // 被动退depth时间
        int64_t generateTs{0};  // 价差生成时间
        int64_t diffTs{0};  // 主动退被动腿时间差

        int64_t activeDepthDelay{0};
        int64_t passiveDepthDelay{0};

        int64_t exchActiveTradeDelay{0};
        int64_t exchPassiveTradeDelay{0};
    };

    struct MdDepth {
        ExchangeType exchangeType;
        InstType instType;
        MarketType marketType;
        string exchangeId{""};
        string instTypeStr{""};
        string marketTypeStr{""};
        string originInstId{""};
        string instId{""};
        string base{""};
        string quote{""};
        string margin{""};
        vector<double> vAskPrice;
        vector<double> vAskVolume;
        vector<double> vBidPrice;
        vector<double> vBidVolume;
        int64_t ts{0};
        int64_t tsNet{0};
        int64_t tsParse{0};
    };

    struct MdKline {
        ExchangeType exchangeType;
        InstType instType;
        MarketType marketType;
        char instrument[INST_ID_LEN];
        double highPrice{0.0};
        double lowPrice{0.0};
        double openPrice{0.0};
        double closePrice{0.0};
        double avgPrice{0.0};
        double totalVolume{0.0};
        double totalAmount{0.0};
        double takerLongVolume{0.0};
        double takerLongAmount{0.0};
        double takerShortVolume{0.0};
        double takerShortAmount{0.0};
        int numOfTrade{0};
        string isFinished{""};
        int64_t ts{0};
        int64_t tsNet{0};
        int64_t tsParse{0};
    };

    struct MdFrate {
        ExchangeType exchangeType;
        InstType instType;
        MarketType marketType;
        double fundingRate{0.0};
        double nextFundingRate{0.0};
        int64_t fundingTime{0};
        int64_t ts{0};
        int64_t tsNet{0};
        int64_t tsParse{0};
    };

    struct MdTrade {
        ExchangeType exchangeType;
        InstType instType;
        MarketType marketType;
        double px{0.0};
        double size{0.0};
        string side{""};
        int64_t ts{0};
        int64_t tsNet{0};
        int64_t tsParse{0};
    };

    struct TdOrder {
        int64_t algoId{0};
        int64_t pairId{0};
        int64_t clOrdId{0};
        int64_t sysOrdId{0};
        char exOrdId[ID_LEN];
        char instrument[INST_ID_LEN]{0};
        ExchangeType exchangType{ET_MIN};
        InstType instType{InstType_MIN};
        OrderStatus orderStatus{OrderStatus_MIN};
        PosDirection posDirection{PosDirection_NET};
        Direction direction{Direction_MIN};
        OrderType orderType{OrderType_MIN};
        bool reduceOnly{false};
        double price{-1.0};
        double volume{0.0};
        double avgPrice{0.0};
        double totalPriceOnOrder{0.0};
        double totalVolumeOnOrder{0.0};
        double lastExecutedPriceOnOrder{-1.0};
        double lastExecutedVolumeOnOrder{0.0};
        double lastExecutedTradeFee{0.0};
        char lastExecutedTradeFeeCurrency[ASSET_LEN]{0};
        int errorId{0};
        char originErrorMsg[MSG_LEN]{""};
        int64_t insertTime{0};
        int64_t updateTime{0};
        int64_t tsSend{0};
        int64_t tsNet{0};
        ApiSource apiSource;
        bool isMaker;

        string GetStr() const {
            char s[STR_LEN];
            fmt::format_to(s, "algoId:{} pairId:{} clOrdId:{} sysOrdId:{} exOrdId:{} instrument:{} exchangType:{} instType:{} orderStatus:{} "
                            "posDirection:{} direction:{} orderType:{} reduceOnly:{} price:{} volume:{} avgPrice:{} totalPriceOnOrder:{}"
                            "totalVolumeOnOrder:{} lastExecutedPriceOnOrder:{} lastExecutedVolumeOnOrder:{} lastExecutedTradeFee:{}"
                            "lastExecutedTradeFeeCurrency:{} errorId:{} originErrorMsg:{} insertTime:{} updateTime:{} tsSend:{} tsNet:{}", 
                            algoId, pairId, clOrdId, sysOrdId, exOrdId, instrument, ExchangeTypeEnum2Str[exchangType], InstTypeEnum2Str[instType], 
                            OrderStatusEnum2Str[orderStatus], PosDirectionEnum2Str[posDirection], DirectionEnum2Str[direction], OrderTypeEnum2Str[orderType], reduceOnly, 
                            price, volume, avgPrice, totalPriceOnOrder, totalVolumeOnOrder, lastExecutedPriceOnOrder, lastExecutedVolumeOnOrder, 
                            lastExecutedTradeFee, lastExecutedTradeFeeCurrency, errorId, originErrorMsg, insertTime, updateTime, tsSend, tsNet);
            return string(s);
        }
    };

    struct TdPosition {
        int accountId{0};
        ExchangeType exchangType{ET_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        char instrument[INST_ID_LEN]{0};
        Direction direction{Direction_MIN};
        double volume{0.0};
        double maintMargin{0.0};
        double avgPrice{0.0};
        double unrealizedPnl{0.0};
        double liquidPrice{0.0};
        double adlQuantile{0.0};
        double markPrice{0.0};
        int64_t updateTime{0};

        string GetStr() const {
            string s = fmt::format("accountId:{},exchangType:{},instType:{},strategyId:{},instrument:{},direction:{},volume:{},maintMargin:{},avgPrice:{},unrealizedPnl:{},liquidPrice:{},adlQuantile:{},markPrice:{},updateTime", 
                                    accountId, ExchangeTypeEnum2Str[exchangType], InstTypeEnum2Str[instType], strategyId, instrument, DirectionEnum2Str[direction], volume, maintMargin, avgPrice, unrealizedPnl, liquidPrice, adlQuantile, markPrice, updateTime);
            return s;
        }
    };

    struct TdBalance {
        int accountId{0};
        ExchangeType exchangType{ET_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        char currency[ASSET_LEN]{0};
        double total{0.0};
        double available{0.0};
        double unrealizedPnl{0.0};
        double frozen{0.0};
        int64_t updateTime{0};

        string GetStr() const {
            string s = fmt::format("accountId:{},exchangType:{},instType:{},strategyId:{},currency:{},total:{},available:{},unrealizedPnl:{},frozen:{},updateTime", 
                                    accountId, ExchangeTypeEnum2Str[exchangType], InstTypeEnum2Str[instType], strategyId, currency, total, available, unrealizedPnl, frozen, updateTime);
            return s;
        }
    };

    struct TdTotalAccount {
        int accountId{0};
        ExchangeType exchangType{ET_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        double totalEquity{0.0};
        double adjEquity{0.0};
        double mmr{0.0};
        double mgnRatio{0.0};
        int64_t updateTime{0};

        string GetStr() const {
            string s = fmt::format("accountId:{},exchangType:{},instType:{},strategyId:{},totalEquity:{},adjEquity:{},mmr:{},mgnRatio:{},updateTime",
                                    accountId, ExchangeTypeEnum2Str[exchangType], InstTypeEnum2Str[instType], strategyId, totalEquity, adjEquity, mmr, mgnRatio, updateTime);
            return s;
        }
    };

    struct TdTransfer {
        int64_t clOrdId{0};
        int64_t sysOrdId{0};
        char exOrdId[ID_LEN];
        OrderStatus orderStatus{OrderStatus_MIN};

        string GetStr() const {
            char s[MSG_LEN];
            fmt::format_to(s, "clOrdId:{} sysOrdId:{} exOrdId:{} orderStatus:{}", 
                            clOrdId, sysOrdId, exOrdId, OrderStatusEnum2Str[orderStatus]);
            return string(s);
        }
    };

    struct TdLending {
        int64_t clOrdId{0};
        int64_t sysOrdId{0};
        char exOrdId[ID_LEN];
        OrderStatus orderStatus{OrderStatus_MIN};

        string GetStr() const {
            char s[MSG_LEN];
            fmt::format_to(s, "clOrdId:{} sysOrdId:{} exOrdId:{} orderStatus:{}", 
                            clOrdId, sysOrdId, exOrdId, OrderStatusEnum2Str[orderStatus]);
            return string(s);
        }
    };

    struct TradeInfo {
        int tradeInfoId;
        int accountId;
        double totalAmount;
        double limitPrice;
        char instrument[INST_ID_LEN];
        char instrumentKey[INST_KEY_LEN];
        ExchangeType exchangeType;
        InstType instType;
        Direction direction;
        MarginType marginType;
        double frozenAmount;
        double filledAmount;
        double filledPrice;
        double filledVolume;
        double publicFilledPrice;
        double publicFilledVolume;
        double privateFilledPrice;
        double privateFilledVolume;
        double filledPct;
        double expectFilledPct;
        bool isFinished;
        AssetAmount totalFee;
    };

    struct QuantOrder {
        int64_t strategyOrderId{-1};
        int64_t systemOrderId{-1};
        char exchangeOrderId[ID_LEN]{""};
        int strategyAccountId{-1};
        int64_t pairId{-1};
        int64_t algoPairId{-1};
        char instrument[INST_ID_LEN]{0};
        char instrumentKey[INST_KEY_LEN]{0};
        char pairInstrumentKey[INST_KEY_LEN]{0};
        ExchangeType exchangeType{ET_MIN};
        InstType instType{InstType_MIN};
        OrderType orderType{OrderType_MIN};
        PosDirection posDirection{PosDirection_MIN};
        Direction direction{Direction_MIN};
        MarginType marginType{MarginType_MIN};
        OrderStatus orderStatus{OrderStatus_MIN};
        TradingType tradingType{TradingType_MIN};
        TradingType tradingTypeOffset{TradingType_MIN};
        double price{0.0};
        double volume{0.0};
        double targetPrice{0.0};
        double volumeFront{0.0};
        double volumeAfter{0.0};
        double totalPriceOnOrder{-1.0};
        double totalVolumeOnOrder{0.0};
        double lastTotalPriceOnOrder{-1.0};
        double lastTotalVolumeOnOrder{0.0};
        double tradePrice{-1.0};
        double tradeVolume{0.0};
        double tradeFee{0.0};
        char tradeFeeCurrency[ASSET_LEN]{""};
        AssetAmountDetail tradeShortFee;
        AssetAmountDetail tradeLongFee;
        AssetAmount totalShortFee;
        AssetAmount totalLongFee;
        TimeStatus orderTimeStatus;
        int errorId{0};
        char originErrorMsg[MSG_LEN]{""};
        char strategyName[NAME_LEN]{""};
        bool reduceOnly{false};
        bool isActiveOrder{false};
        bool rebalance{false};
        int64_t orderTime{0};
        int64_t updateTime{0};
        int64_t killTime{0};
        int queryCount{0};

        QuantOrder() {
            orderTime = GetCurrentTimeUs();
            updateTime = GetCurrentTimeUs();
        }

        QuantOrder UpdateOrderOnOrder(const stra::TdOrder& tdOrder, int64_t eventTime) {
            stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
            double lastTotalAmountOnOrder = 0.0;
            double totalAmountOnOrder = 0.0;
            double lastExecutedVolumeOnOrder = 0.0;
            double lastExecutedPriceOnOrder = 0.0;
            double lastExecutedAmountOnOrder = 0.0;
            double tradeAmount = 0.0;
            queryCount = 0;
            // 报单状态更新
            // 状态转换需要过滤
            orderStatus = tdOrder.orderStatus;
            updateTime = eventTime;

            auto& dt = orderTimeStatus.detail[orderTimeStatus.size];
            dt.updateTime = eventTime;
            dt.orderStatus = orderStatus;
            orderTimeStatus.size++;
            if (orderTimeStatus.size >= TIME_STATUS_LEN) {
                orderTimeStatus.size = TIME_STATUS_LEN - 1;
                char msg[MSG_LEN];
                LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", orderTimeStatus.size, TIME_STATUS_LEN);
            }

            errorId = tdOrder.errorId;
            strncpy(originErrorMsg, tdOrder.originErrorMsg, MSG_LEN);

            if (tdOrder.orderStatus == OrderStatus_PENDING_NEW) {
                systemOrderId = tdOrder.sysOrdId;
            } else if (tdOrder.orderStatus == OrderStatus_NEW) {
                systemOrderId = tdOrder.sysOrdId;
                strncpy(exchangeOrderId, tdOrder.exOrdId, stra::ID_LEN);
            }

            // if (tdOrder.totalVolumeOnOrder - totalVolumeOnOrder > MIN_FLOAT) {
                if (info.calculateType == 0) {
                    lastTotalPriceOnOrder = totalPriceOnOrder;
                    lastTotalVolumeOnOrder = totalVolumeOnOrder;
                    lastTotalAmountOnOrder = lastTotalPriceOnOrder * lastTotalVolumeOnOrder * info.multiple;
                    totalVolumeOnOrder = tdOrder.totalVolumeOnOrder;
                    totalPriceOnOrder = -1.0;
                    if (totalVolumeOnOrder > stra::MIN_FLOAT) {
                        totalPriceOnOrder = tdOrder.avgPrice;
                    }
                    totalAmountOnOrder = totalVolumeOnOrder * totalPriceOnOrder * info.multiple;
                    lastExecutedVolumeOnOrder = tdOrder.lastExecutedVolumeOnOrder;
                    lastExecutedPriceOnOrder = tdOrder.lastExecutedPriceOnOrder;
                    lastExecutedAmountOnOrder = lastExecutedVolumeOnOrder * lastExecutedPriceOnOrder * info.multiple;
                    tradeVolume = totalVolumeOnOrder - lastTotalVolumeOnOrder;
		    //LOG_INFO("QuantOrder updateOrder totalVolumeOnOrder:%f lastTotalVolumeOnOrder:%f", totalVolumeOnOrder, lastTotalVolumeOnOrder);
                    tradeAmount = totalAmountOnOrder - lastTotalAmountOnOrder;
                    tradePrice = -1.0;
                    if (tradeVolume > stra::MIN_FLOAT) {
                        tradePrice = tradeAmount / (tradeVolume * info.multiple);
                    }

                    tradeFee = 0.0;
                    if (lastExecutedAmountOnOrder > MIN_FLOAT) {
                        tradeFee = tdOrder.lastExecutedTradeFee * tradeAmount / lastExecutedAmountOnOrder;
                    }

                    strncpy(tradeFeeCurrency, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                    if (tradeFee > MIN_FLOAT && strlen(tradeFeeCurrency) > 0) {
                        if (direction == Direction_LONG) {
                            bool assetExist = false;
                            for (int i = 0; i < totalLongFee.size; ++i) {
                                if (strcmp(totalLongFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalLongFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalLongFee.detail[totalLongFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalLongFee.size++;
                                if (totalLongFee.size >= ASSET_AMOUNT_LEN) {
                                    totalLongFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalLongFee size:%d > ASSET_AMOUNT_LEN:%d", totalLongFee.size, ASSET_AMOUNT_LEN);
                                }
                            }

                            strncpy(tradeLongFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeLongFee.amount = tradeFee;
                        } else {
                            bool assetExist = false;
                            for (int i = 0; i < totalShortFee.size; ++i) {
                                if (strcmp(totalShortFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalShortFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalShortFee.detail[totalShortFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalShortFee.size++;
                                if (totalShortFee.size >= ASSET_AMOUNT_LEN) {
                                    totalShortFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalShortFee size:%d > ASSET_AMOUNT_LEN:%d", totalShortFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeShortFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeShortFee.amount = tradeFee;
                        }
                    }
                } else if (info.calculateType == 1) {
                    lastTotalPriceOnOrder = totalPriceOnOrder;
                    lastTotalVolumeOnOrder = totalVolumeOnOrder;
                    lastTotalAmountOnOrder = lastTotalVolumeOnOrder * info.multiple;
                    totalVolumeOnOrder = tdOrder.totalVolumeOnOrder;
                    totalPriceOnOrder = -1.0;
                    if (totalVolumeOnOrder > MIN_FLOAT) {
                        totalPriceOnOrder = tdOrder.avgPrice;
                    }
                    totalAmountOnOrder = totalVolumeOnOrder * info.multiple;
                    lastExecutedVolumeOnOrder = tdOrder.lastExecutedVolumeOnOrder;
                    lastExecutedPriceOnOrder = tdOrder.lastExecutedPriceOnOrder;
                    lastExecutedAmountOnOrder = lastExecutedVolumeOnOrder * info.multiple;
                    tradeVolume = totalVolumeOnOrder - lastTotalVolumeOnOrder;
                    tradeAmount = tradeVolume * info.multiple;
                    tradePrice = -1.0;
                    if (tradeVolume > MIN_FLOAT) {
                        tradePrice = tradeVolume / (totalVolumeOnOrder / totalPriceOnOrder - lastTotalVolumeOnOrder / lastTotalPriceOnOrder);
                    }

                    double tradeFee = 0.0;
                    if (lastExecutedAmountOnOrder > MIN_FLOAT) {
                        tradeFee = tdOrder.lastExecutedTradeFee * (tradeAmount / tradePrice) / (lastExecutedAmountOnOrder / lastExecutedPriceOnOrder);
                    }

                    strncpy(tradeFeeCurrency, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                    if (tradeFee > MIN_FLOAT && strlen(tradeFeeCurrency) > 0) {
                        if (direction == Direction_LONG) {
                            bool assetExist = false;
                            for (int i = 0; i < totalLongFee.size; ++i) {
                                if (strcmp(totalLongFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalLongFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalLongFee.detail[totalLongFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalLongFee.size++;
                                if (totalLongFee.size >= ASSET_AMOUNT_LEN) {
                                    totalLongFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalLongFee size:%d > ASSET_AMOUNT_LEN:%d", totalLongFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeLongFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeLongFee.amount = tradeFee;
                        } else {
                            bool assetExist = false;
                            for (int i = 0; i < totalShortFee.size; ++i) {
                                if (strcmp(totalShortFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalShortFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalShortFee.detail[totalShortFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalShortFee.size++;
                                if (totalShortFee.size >= ASSET_AMOUNT_LEN) {
                                    totalShortFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalShortFee size:%d > ASSET_AMOUNT_LEN:%d", totalShortFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeShortFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeShortFee.amount = tradeFee;
                        }
                    }
                }
            // }
            return *this;
        }

        QuantOrder UpdateOrderOnQueryOrder(const stra::TdOrder& tdOrder, int64_t eventTime) {
           stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
            double lastTotalAmountOnOrder = 0.0;
            double totalAmountOnOrder = 0.0;
            double lastExecutedVolumeOnOrder = 0.0;
            double lastExecutedPriceOnOrder = 0.0;
            double lastExecutedAmountOnOrder = 0.0;
            double tradeAmount = 0.0;
            queryCount += 1;
            // 报单状态更新
            // 状态转换需要过滤
            if (tdOrder.errorId == OrderNotFoundError && strlen(exchangeOrderId) <= 0) {
                orderStatus = OS_REJECTED;
                char msg[stra::MSG_LEN];
                sprintf(msg, "strategyName:%s algoPairId:%ld strategyOrderId:%ld", strategyName, algoPairId, strategyOrderId);
                rLarkMsg.Push(string(msg));
            } else {
                orderStatus = tdOrder.orderStatus;
            }
            updateTime = eventTime;

            auto& dt = orderTimeStatus.detail[orderTimeStatus.size];
            dt.updateTime = eventTime;
            dt.orderStatus = orderStatus;
            orderTimeStatus.size++;
            if (orderTimeStatus.size >= TIME_STATUS_LEN) {  // 不抛异常，写log或覆盖
                orderTimeStatus.size = TIME_STATUS_LEN - 1;
                char msg[MSG_LEN];
                LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", orderTimeStatus.size, TIME_STATUS_LEN);
            }

            errorId = tdOrder.errorId;
            strncpy(originErrorMsg, tdOrder.originErrorMsg, MSG_LEN);

            if (tdOrder.orderStatus == OrderStatus_PENDING_NEW) {
                systemOrderId = tdOrder.sysOrdId;
            } else if (tdOrder.orderStatus == OrderStatus_NEW) {
                systemOrderId = tdOrder.sysOrdId;
                strncpy(exchangeOrderId, tdOrder.exOrdId, stra::ID_LEN);
            }

            if (tdOrder.totalVolumeOnOrder - totalVolumeOnOrder > MIN_FLOAT) {
                if (info.calculateType == 0) {
                    lastTotalPriceOnOrder = totalPriceOnOrder;
                    lastTotalVolumeOnOrder = totalVolumeOnOrder;
                    lastTotalAmountOnOrder = lastTotalPriceOnOrder * lastTotalVolumeOnOrder * info.multiple;
                    totalVolumeOnOrder = tdOrder.totalVolumeOnOrder;
                    totalPriceOnOrder = -1.0;
                    if (totalVolumeOnOrder > stra::MIN_FLOAT) {
                        totalPriceOnOrder = tdOrder.avgPrice;
                    }
                    totalAmountOnOrder = totalVolumeOnOrder * totalPriceOnOrder * info.multiple;
                    lastExecutedVolumeOnOrder = tdOrder.lastExecutedVolumeOnOrder;
                    lastExecutedPriceOnOrder = tdOrder.lastExecutedPriceOnOrder;
                    lastExecutedAmountOnOrder = lastExecutedVolumeOnOrder * lastExecutedPriceOnOrder * info.multiple;
                    tradeVolume = totalVolumeOnOrder - lastTotalVolumeOnOrder;
                    tradeAmount = totalAmountOnOrder - lastTotalAmountOnOrder;
                    tradePrice = -1.0;
                    if (tradeVolume > stra::MIN_FLOAT) {
                        tradePrice = tradeAmount / (tradeVolume * info.multiple);
                    }

                    tradeFee = 0.0;
                    if (lastExecutedAmountOnOrder > MIN_FLOAT) {
                        tradeFee = tdOrder.lastExecutedTradeFee * tradeAmount / lastExecutedAmountOnOrder;
                    }

                    strncpy(tradeFeeCurrency, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                    if (tradeFee > MIN_FLOAT && strlen(tradeFeeCurrency) > 0) {
                        if (direction == Direction_LONG) {
                            bool assetExist = false;
                            for (int i = 0; i < totalLongFee.size; ++i) {
                                if (strcmp(totalLongFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalLongFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalLongFee.detail[totalLongFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalLongFee.size++;
                                if (totalLongFee.size >= ASSET_AMOUNT_LEN) {
                                    totalLongFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalLongFee size:%d > ASSET_AMOUNT_LEN:%d", totalLongFee.size, ASSET_AMOUNT_LEN);
                                }
                            }

                            strncpy(tradeLongFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeLongFee.amount = tradeFee;
                        } else {
                            bool assetExist = false;
                            for (int i = 0; i < totalShortFee.size; ++i) {
                                if (strcmp(totalShortFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalShortFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalShortFee.detail[totalShortFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalShortFee.size++;
                                if (totalShortFee.size >= ASSET_AMOUNT_LEN) {
                                    totalShortFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalShortFee size:%d > ASSET_AMOUNT_LEN:%d", totalShortFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeShortFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeShortFee.amount = tradeFee;
                        }
                    }
                } else if (info.calculateType == 1) {
                    lastTotalPriceOnOrder = totalPriceOnOrder;
                    lastTotalVolumeOnOrder = totalVolumeOnOrder;
                    lastTotalAmountOnOrder = lastTotalVolumeOnOrder * info.multiple;
                    totalVolumeOnOrder = tdOrder.totalVolumeOnOrder;
                    totalPriceOnOrder = -1.0;
                    if (totalVolumeOnOrder > MIN_FLOAT) {
                        totalPriceOnOrder = tdOrder.avgPrice;
                    }
                    totalAmountOnOrder = totalVolumeOnOrder * info.multiple;
                    lastExecutedVolumeOnOrder = tdOrder.lastExecutedVolumeOnOrder;
                    lastExecutedPriceOnOrder = tdOrder.lastExecutedPriceOnOrder;
                    lastExecutedAmountOnOrder = lastExecutedVolumeOnOrder * info.multiple;
                    tradeVolume = totalVolumeOnOrder - lastTotalVolumeOnOrder;
                    tradeAmount = tradeVolume * info.multiple;
                    tradePrice = -1.0;
                    if (tradeVolume > MIN_FLOAT) {
                        tradePrice = tradeVolume / (totalVolumeOnOrder / totalPriceOnOrder - lastTotalVolumeOnOrder / lastTotalPriceOnOrder);
                    }

                    double tradeFee = 0.0;
                    if (lastExecutedAmountOnOrder > MIN_FLOAT) {
                        tradeFee = tdOrder.lastExecutedTradeFee * (tradeAmount / tradePrice) / (lastExecutedAmountOnOrder / lastExecutedPriceOnOrder);
                    }

                    strncpy(tradeFeeCurrency, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                    if (tradeFee > MIN_FLOAT && strlen(tradeFeeCurrency) > 0) {
                        if (direction == Direction_LONG) {
                            bool assetExist = false;
                            for (int i = 0; i < totalLongFee.size; ++i) {
                                if (strcmp(totalLongFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalLongFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalLongFee.detail[totalLongFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalLongFee.size++;
                                if (totalLongFee.size >= ASSET_AMOUNT_LEN) {
                                    totalLongFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalLongFee size:%d > ASSET_AMOUNT_LEN:%d", totalLongFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeLongFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeLongFee.amount = tradeFee;
                        } else {
                            bool assetExist = false;
                            for (int i = 0; i < totalShortFee.size; ++i) {
                                if (strcmp(totalShortFee.detail[i].asset, tradeFeeCurrency) == 0) {
                                    totalShortFee.detail[i].amount += tradeFee;
                                    assetExist = true;
                                    break;
                                }
                            }
                            if (!assetExist) {
                                auto& dt = totalShortFee.detail[totalShortFee.size];
                                strncpy(dt.asset, tradeFeeCurrency, ASSET_LEN);
                                dt.amount = tradeFee;
                                totalShortFee.size++;
                                if (totalShortFee.size >= ASSET_AMOUNT_LEN) {
                                    totalShortFee.size = ASSET_AMOUNT_LEN - 1;
                                    char msg[MSG_LEN];
                                    LOG_INFO("totalShortFee size:%d > ASSET_AMOUNT_LEN:%d", totalShortFee.size, ASSET_AMOUNT_LEN);
                                }
                            }
                            strncpy(tradeShortFee.asset, tdOrder.lastExecutedTradeFeeCurrency, ASSET_LEN);
                            tradeShortFee.amount = tradeFee;
                        }
                    }
                }
            }
            return *this;
        }

        string GetStr() const {
            /*
            string s = fmt::format("{},{},{},{},{},{},"
                            "{},{},{},{},{},{},{},"
                            "{},{},{},{},{},{},{},{},"
                            "{},{},{},{},{},{},{},"
                            "{},{},{},{},{},{},"
                            "{},{},{},{},{},{},"
                            "{},{},{},{},{}", 
                            strategyOrderId, systemOrderId, exchangeOrderId, strategyAccountId, pairId, algoPairId, 
                            instrument, instrumentKey, pairInstrumentKey, exchangeType, instType, orderType, posDirection, 
                            direction, marginType, orderStatus, tradingType, price, volume, targetPrice, volumeFront, 
                            volumeAfter, totalPriceOnOrder, totalVolumeOnOrder, lastTotalPriceOnOrder, lastTotalVolumeOnOrder, tradePrice, tradeVolume, 
                            tradeFee, tradeFeeCurrency, tradeShortFee.GetStr(), tradeLongFee.GetStr(), totalShortFee.GetStr(), totalLongFee.GetStr(), 
                            orderTimeStatus.GetEnumStr(), errorId, originErrorMsg, strategyName, reduceOnly, isActiveOrder, rebalance, orderTime, updateTime, killTime, queryCount);          
            return s;
            */
            return "";
        }
    };

    struct QuantKline {
        int64_t timestamp{0};
        ExchangeType exchangeType;
        char instrument[INST_ID_LEN];
        InstType instType;
        double open{0.0};
        double high{0.0};
        double low{0.0};
        double close{0.0};
        double volume{0.0};
        double amount{0.0};
        int64_t count{0};
        double buyVolume{0.0};
        double buyAmount{0.0};
        int64_t buyCount{0};
        double sellVolume{0.0};
        double sellAmount{0.0};
        int64_t sellCount{0};
        int64_t updateTime{0};
        double process{0.0};
    };

    struct QuantMarketTrade {
        int64_t timestamp{0};
        int64_t arriveTime{0};
        int64_t exchangeTime{0};
        int64_t platformTime{0};
        int64_t eventTime{0};
        ExchangeType exchangeType;
        char instrument[INST_ID_LEN];
        InstType instType;
        Direction direction;
        double tradePrice{0.0};
        double tradeVolume{0.0};
    };

    struct QuantMarketDepth {
        int64_t timestamp{0};
        int64_t arriveTime{0};
        int64_t exchangeTime{0};
        int64_t platformTime{0};
        int64_t eventTime{0};
        ExchangeType exchangeType;
        char instrument[INST_ID_LEN];
        InstType instType;
        vector<double> vAskPrice{vector<double>(5, 0)};
        vector<double> vAskVolume{vector<double>(5, 0)};
        vector<double> vBidPrice{vector<double>(5, 0)};
        vector<double> vBidVolume{vector<double>(5, 0)};
        double avgInterval{0.0};
        double maxInterval{300};
        double minInterval{5};
    };

    struct QuantTransfer {
        int64_t algoOrderId{0};
        int64_t pairOrderId{0};
        int64_t strategyTransferId{0};
        char algoStrategyName[NAME_LEN]{0};
        char pairInstrumentKey[INST_KEY_LEN]{0};
        char systemTransferId[ORDER_ID_LEN]{0};
        char exchangeTransferId[ORDER_ID_LEN]{0};
        char transferAsset[ASSET_LEN]{0};
        int withdrawStrategyAccountId{0};
        int depositStrategyAccountId{0};
        double transferVolume{0.0};
        double activePrice{0.0};
        double passivePrice{0.0};
        ExchangeType exchangeType{ET_MIN};
        OrderStatus transferStatus{OrderStatus_MIN};
        TimeStatus transferTimeStatus;
        OrderType pairOrderType{OrderType_MIN};
        int64_t updateTime{0};

        string GetStr() const {
            char s[STR_LEN];
            fmt::format_to(s, "algoOrderId:{} pairOrderId:{} strategyTransferId:{} algoStrategyName:{} pairInstrumentKey:{} "
                            "systemTransferId:{} exchangeTransferId:{} transferAsset:{} withdrawStrategyAccountId:{} "
                            "depositStrategyAccountId:{} transferVolume:{} activePrice:{} passivePrice:{} exchangeType:{} "
                            "transferStatus:{} transferTimeStatus:{} pairOrderType:{} updateTime:{}", algoOrderId, pairOrderId,
                            strategyTransferId, algoStrategyName, pairInstrumentKey, systemTransferId, exchangeTransferId,
                            transferAsset, withdrawStrategyAccountId, depositStrategyAccountId, transferVolume, activePrice,
                            passivePrice, ExchangeTypeEnum2Str[exchangeType], OrderStatusEnum2Str[transferStatus], transferTimeStatus.GetStr(),
                            OrderTypeEnum2Str[pairOrderType], updateTime);
            return string(s);
        }
    };

    struct QuantFrate {
        int64_t timestamp{0};
        int64_t arriveTime{0};
        int64_t exchangeTime{0};
        int64_t platformTime{0};
        int64_t fundingTime{0};
        ExchangeType exchangeType;
        char instrument[INST_ID_LEN];
        InstType instType;
        double fundingRate{0.0};
        double nextFundingRate{0.0};
    };

    struct QuantLending {
        int64_t strategyTransferId{0};
        int strategyAccountId{0};
        char asset[ASSET_LEN]{0};
        double lendingVolume{0.0};
        OrderStatus orderStatus{OrderStatus_MIN};
        LendingType lendingType{LendingType_MIN};
        TimeStatus lendingStatus;
        int64_t updateTime{0};

        string GetStr() const {
            char s[STR_LEN];
            fmt::format_to(s, "strategyTransferId:{} strategyAccountId:{} asset:{} lendingVolume:{} orderStatus:{} lendingType:{}"
                            "lendingStatus:{} updateTime:{}", strategyTransferId, strategyAccountId, asset, lendingVolume, 
                            OrderStatusEnum2Str[orderStatus], LendingTypeEnum2Str[lendingType], lendingStatus.GetStr(), updateTime);
            return string(s);
        }
    };

    struct AssetUnit {  // marginAmount floatAmount positionValue 会根据depth变化
        char asset[ASSET_LEN];
        char baseAsset[ASSET_LEN];
        double initAmount{0.0};
        double totalAmount{0.0};
        double transferAmount{0.0};
        double frozenAmount{0.0};
        double marginAmount{0.0}; // 已有持仓占用保证金
        double openMarginAmount{0.0}; // 挂单占用保证金
        double feeAmount{0.0};   // 手续费
        double fundAmount{0.0};  // 资金费用
        double loanAmount{0.0};  // 借贷金额
        double interestAmount{0.0}; //借贷利息
        double closeAmount{0.0};
        double floatAmount{0.0};
        double positionValue{0.0};

        string GetStr() const {
            string s = fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}", asset, baseAsset, initAmount, totalAmount, transferAmount, frozenAmount, marginAmount, 
                            openMarginAmount, feeAmount, fundAmount, loanAmount, interestAmount, closeAmount, floatAmount, positionValue);
            return string(s);
        }
    };

    struct PositionUnit {
        char instrumentKey[INST_KEY_LEN];
        char baseAsset[ASSET_LEN];
        double longPosition{0.0};
        double longAvgPrice{-1};
        double shortPosition{0.0};
        double shortAvgPrice{-1};
        double floatAmount{0.0};
        double closeAmount{0.0};
        double positionValue{0.0};
        double frozenLongPosition{0.0};
        double frozenLongPrice{-1};
        double frozenShortPosition{0.0};
        double frozenShortPrice{-1};
        double lastFloatAmount{0.0};
        double lastPositionValue{0.0};

        string GetStr() const {
            string s = fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}", instrumentKey, baseAsset, longPosition,
                            longAvgPrice, shortPosition, shortAvgPrice, floatAmount, closeAmount, positionValue, frozenLongPosition,
                            frozenLongPrice, frozenShortPosition, frozenShortPrice, lastFloatAmount, lastPositionValue);
            return s;
        }
    };

    struct QuantAccount {
        int strategyAccountId{0};
        int systemAccountId{0};
        int physicalAccountId{0};
        AccountType accountType{AT_MIN};
        AccountMarginType marginType{AccountMarginType_MIN};
        AccountMarginType accountMarginType{AccountMarginType_MIN};
        double openRealLeverage{0.9};
        double maxRealLeverage{0.95};
	    double passiveOpenRealLeverage{1.0};
        double passiveMaxRealLeverage{1.05};
        double totalEquity{0.0};
        double adjEquity{0.0};
        double mmr{0.0};
        double mgnRatio{0.0};
        double openActiveMgnRatio{1000000};
        double openPassiveMgnRatio{1000000};
        unordered_map<string, AssetUnit> mAsset;
        unordered_map<string, PositionUnit> mPosition;
        vector<int> vTransfer;

        string GetStr() const {
            stringstream ss;
            ss << strategyAccountId << " --- ";
            for (auto iter = mAsset.begin(); iter != mAsset.end(); ++iter) {
                ss << iter->second.GetStr() << "|";
            }

            for (auto iter = mPosition.begin(); iter != mPosition.end(); ++iter) {
                ss << iter->second.GetStr() << "|";
            }

            return ss.str();
        }

        void LoadFromFile(string filePath) {
            std::ifstream accountFile(filePath.c_str());
            if (!accountFile) {
                LOG_INFO("File: %s does not exist!", filePath.c_str());
                return;
            }

            json accountInfo;
            accountFile >> accountInfo;

            auto i = accountInfo.find("strategyAccountId");
            if (i != accountInfo.end()) {
                strategyAccountId = int(i.value());
            }
            
            i = accountInfo.find("systemAccountId");
            if (i != accountInfo.end()) {
                systemAccountId = int(i.value());
            }

            i = accountInfo.find("physicalAccountId");
            if (i != accountInfo.end()) {
                physicalAccountId = int(i.value());
            }

            i = accountInfo.find("accountType");
            if (i != accountInfo.end()) {
                accountType = stra::AccountTypeStr2Enum[string(i.value())];
            }

            i = accountInfo.find("marginType");
            if (i != accountInfo.end()) {
                marginType = stra::AccountMarginTypeStr2Enum[string(i.value())];
            }

            i = accountInfo.find("accountMarginType");
            if (i != accountInfo.end()) {
                accountMarginType = stra::AccountMarginTypeStr2Enum[string(i.value())];
            }

            i = accountInfo.find("openRealLeverage");
            if (i != accountInfo.end()) {
                openRealLeverage = double(i.value());
            }

            
            i = accountInfo.find("assetInfo");
            if (i != accountInfo.end()) {
                json assetInfo = i.value();
                for (auto j = assetInfo.begin(); j != assetInfo.end(); ++j) {
                    stra::AssetUnit assetUnit;
                    json asset = j.value();
                    auto k = asset.find("asset");
                    if (k != asset.end()) {
                        strncpy(assetUnit.asset, string(k.value()).c_str(), stra::ASSET_LEN);
                    }

                    k = asset.find("baseAsset");
                    if (k != asset.end()) {
                        strncpy(assetUnit.baseAsset, string(k.value()).c_str(), stra::ASSET_LEN);
                    }

                    k = asset.find("initAmount");
                    if (k != asset.end()) {
                        assetUnit.initAmount = double(k.value());
                    }

                    k = asset.find("totalAmount");
                    if (k != asset.end()) {
                        assetUnit.totalAmount = double(k.value());
                    }

                    k = asset.find("transferAmount");
                    if (k != asset.end()) {
                        assetUnit.transferAmount = double(k.value());
                    }

                    k = asset.find("frozenAmount");
                    if (k != asset.end()) {
                        assetUnit.frozenAmount = double(k.value());
                    }

                    k = asset.find("marginAmount");
                    if (k != asset.end()) {
                        assetUnit.marginAmount = double(k.value());
                    }

                    k = asset.find("openMarginAmount");
                    if (k != asset.end()) {
                        assetUnit.openMarginAmount = double(k.value());
                    }

                    k = asset.find("feeAmount");
                    if (k != asset.end()) {
                        assetUnit.feeAmount = double(k.value());
                    }

                    k = asset.find("fundAmount");
                    if (k != asset.end()) {
                        assetUnit.fundAmount = double(k.value());
                    }

                    k = asset.find("loanAmount");
                    if (k != asset.end()) {
                        assetUnit.loanAmount = double(k.value());
                    }

                    k = asset.find("interestAmount");
                    if (k != asset.end()) {
                        assetUnit.interestAmount = double(k.value());
                    }

                    k = asset.find("closeAmount");
                    if (k != asset.end()) {
                        assetUnit.closeAmount = double(k.value());
                    }

                    k = asset.find("floatAmount");
                    if (k != asset.end()) {
                        assetUnit.floatAmount = double(k.value());
                    }

                    k = asset.find("positionValue");
                    if (k != asset.end()) {
                        assetUnit.positionValue = double(k.value());
                    }

                    mAsset[assetUnit.asset] = assetUnit;
                }
            }


            i = accountInfo.find("positionInfo");
            if (i != accountInfo.end()) {
                json positionInfo = i.value();
                for (auto j = positionInfo.begin(); j != positionInfo.end(); ++j) {
                    stra::PositionUnit positionUnit;
                    json position = j.value();
                    auto k = position.find("instrumentKey");
                    if (k != position.end()) {
                        strncpy(positionUnit.instrumentKey, string(k.value()).c_str(), stra::INST_KEY_LEN);
                    }

                    k = position.find("baseAsset");
                    if (k != position.end()) {
                        strncpy(positionUnit.baseAsset, string(k.value()).c_str(), stra::ASSET_LEN);
                    }

                    k = position.find("longPosition");
                    if (k != position.end()) {
                        positionUnit.longPosition = double(k.value());
                    }

                    k = position.find("longAvgPrice");
                    if (k != position.end()) {
                        positionUnit.longAvgPrice = double(k.value());
                    }
                    
                    k = position.find("shortPosition");
                    if (k != position.end()) {
                        positionUnit.shortPosition = double(k.value());
                    }

                    k = position.find("shortAvgPrice");
                    if (k != position.end()) {
                        positionUnit.shortAvgPrice = double(k.value());
                    }

                    k = position.find("floatAmount");
                    if (k != position.end()) {
                        positionUnit.floatAmount = double(k.value());
                    }

                    k = position.find("closeAmount");
                    if (k != position.end()) {
                        positionUnit.closeAmount = double(k.value());
                    }

                    k = position.find("positionValue");
                    if (k != position.end()) {
                        positionUnit.positionValue = double(k.value());
                    }

                    k = position.find("frozenLongPosition");
                    if (k != position.end()) {
                        positionUnit.frozenLongPosition = double(k.value());
                    }

                    k = position.find("frozenLongPrice");
                    if (k != position.end()) {
                        positionUnit.frozenLongPrice = double(k.value());
                    }

                    k = position.find("frozenShortPosition");
                    if (k != position.end()) {
                        positionUnit.frozenShortPosition = double(k.value());
                    }

                    k = position.find("frozenShortPrice");
                    if (k != position.end()) {
                        positionUnit.frozenShortPrice = double(k.value());
                    }

                    k = position.find("lastFloatAmount");
                    if (k != position.end()) {
                        positionUnit.lastFloatAmount = double(k.value());
                    }

                    k = position.find("lastPositionValue");
                    if (k != position.end()) {
                        positionUnit.lastPositionValue = double(k.value());
                    }
                    mPosition[positionUnit.instrumentKey] = positionUnit;
                }
            }


            i = accountInfo.find("transferInfo");
            if (i != accountInfo.end()) {
                json transferInfo = i.value();
                for (int i = 0; i < transferInfo.size(); ++i) {
                    int transferId = int(transferInfo.at(i));
                    vTransfer.emplace_back(transferId);
                }
            }
        }

        void SaveToFile(string filePath) {
            json accountInfo;
            accountInfo["strategyAccountId"] = strategyAccountId;
            accountInfo["systemAccountId"] = systemAccountId;
            accountInfo["physicalAccountId"] = physicalAccountId;
            accountInfo["accountType"] = stra::AccountTypeEnum2Str[accountType];
            accountInfo["marginType"] = stra::AccountMarginTypeEnum2Str[marginType];
            accountInfo["accountMarginType"] = stra::AccountMarginTypeEnum2Str[accountMarginType];
            accountInfo["openRealLeverage"] = openRealLeverage;

            json assetInfo;
            for (auto iter = mAsset.begin(); iter != mAsset.end(); ++iter) {
                json asset;
                asset["asset"] = iter->second.asset;
                asset["baseAsset"] = iter->second.baseAsset;
                asset["initAmount"] = iter->second.initAmount;
                asset["totalAmount"] = iter->second.totalAmount;
                asset["transferAmount"] = iter->second.transferAmount;
                asset["frozenAmount"] = iter->second.frozenAmount;
                asset["marginAmount"] = iter->second.marginAmount;
                asset["openMarginAmount"] = iter->second.openMarginAmount;
                asset["feeAmount"] = iter->second.feeAmount;
                asset["fundAmount"] = iter->second.fundAmount;
                asset["loanAmount"] = iter->second.loanAmount;
                asset["interestAmount"] = iter->second.interestAmount;
                asset["closeAmount"] = iter->second.closeAmount;
                asset["floatAmount"] = iter->second.floatAmount;
                asset["positionValue"] = iter->second.positionValue;
                assetInfo[iter->second.asset] = asset;
            }
            accountInfo["assetInfo"] = assetInfo;

            json positionInfo;
            for (auto iter = mPosition.begin(); iter != mPosition.end(); ++iter) {
                json position;
                position["instrumentKey"] = iter->second.instrumentKey;
                position["baseAsset"] = iter->second.baseAsset;
                position["longPosition"] = iter->second.longPosition;
                position["longAvgPrice"] = iter->second.longAvgPrice;
                position["shortPosition"] = iter->second.shortPosition;
                position["shortAvgPrice"] = iter->second.shortAvgPrice;
                position["floatAmount"] = iter->second.floatAmount;
                position["closeAmount"] = iter->second.closeAmount;
                position["positionValue"] = iter->second.positionValue;
                position["frozenLongPosition"] = iter->second.frozenLongPosition;
                position["frozenLongPrice"] = iter->second.frozenLongPrice;
                position["frozenShortPosition"] = iter->second.frozenShortPosition;
                position["frozenShortPrice"] = iter->second.frozenShortPrice;
                position["lastFloatAmount"] = iter->second.lastFloatAmount;
                position["lastPositionValue"] = iter->second.lastPositionValue;
                positionInfo[iter->second.instrumentKey] = position;
            }
            accountInfo["positionInfo"] = positionInfo;
        
            json transferInfo;
            for (size_t i = 0; i < vTransfer.size(); ++i) {
                transferInfo.emplace_back(vTransfer[i]);
            }
            accountInfo["transferInfo"] = transferInfo;

            std::ofstream o(filePath.c_str());
            o << std::setw(4) << accountInfo;
        }

    }; 



    // 不同的交易所应定义不同的结构体，目前只定义了binance

    struct SpotAsset {
        string a;
        double f;
        double l;
    };

    struct FutureAsset {
        string a;
        double wb;
        double cw;
        double bc;
    };

    struct FuturePosition {
        string s;
        double pa;
        double ep;
        double cr;
        double up;
        string mt;
        double iw;
        string ps;
    };

    struct ExAccountInfo {
        AccountType accountType;
        int accountId;
        unordered_map<string, SpotAsset> mSpotAsset;
        unordered_map<string, FutureAsset> mFutureAsset;
        unordered_map<string, FuturePosition> mFuturePosition;
    };   
}

struct ReceiveInfo {
	string id{""};
	string type{""};
};

struct ReceiveGroupInfo {
	string code{""};
	string importance{""};
};

struct MsgCard {
	int accountId{0};
	int templateId{1};
	string name{""};
	string title{""};
	string object{""};
	string datetime{""};
	string content{""};
};

struct AccountInfo {
	string accountName;
	int accountId;
    stra::AccountType accountType;
	ExchangeType exchangeType;
	vector<InstType> vInstType;
	string strategyId;
	double openRealLeverage;
	double maxRealLeverage;
	double passiveOpenRealLeverage;
	double passiveMaxRealLeverage;
	double openActiveMgnRatio;
	double openPassiveMgnRatio;

    int maxPersec;
    int maxCancelPersec;
    int orderNum;
};


extern unordered_map<string, int> mAccountNameAccountId;

#endif
