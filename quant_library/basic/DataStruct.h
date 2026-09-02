#ifndef _DATASTRUCT_H
#define _DATASTRUCT_H

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <unordered_map>
#include <cstring>
#include "Utility.h"
#include "StraException.h"
#include "crypto_errors.h"
#include "json/nlohmann/json.hpp"
#include "log_engine.h"
#include "pubsub_protocol.h"

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

    /*
    enum TradingOffset {
        TradingOffset_MIN = 0,
        OPEN_SHORT,
        OPEN_LONG,
        CLOSE_SHORT,
        CLOSE_LONG,
        TradingType_MAX
    };

    static unordered_map<TradingOffset, string> TradingOffsetEnum2Str {
        {TradingOffset_MIN, "TradingOffset_MIN"},
        {OPEN_SHORT, "OPEN_SHORT"},
        {OPEN_LONG, "OPEN_LONG"},
        {CLOSE_SHORT, "CLOSE_SHORT"},
        {CLOSE_LONG, "CLOSE_LONG"},
        {TradingType_MAX, "TradingType_MAX"}
    };

    static unordered_map<string, TradingOffset> TradingOffsetStr2Enum {
        {"TradingOffset_MIN", TradingOffset_MIN},
        {"OPEN_SHORT", OPEN_SHORT},
        {"OPEN_LONG", OPEN_LONG},
        {"CLOSE_SHORT", CLOSE_SHORT},
        {"CLOSE_LONG", CLOSE_LONG},
        {"TradingType_MAX", TradingType_MAX}
    };
    */

    struct TimeStatusDetail {
        int64_t updateTime{0};
        OrderStatus orderStatus{OS_MIN};
        
        string GetStr() const {
            char s[MSG_LEN];
            sprintf(s, "%ld-%s", updateTime, OrderStatusEnum2StrMap[orderStatus].c_str());
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

    struct TdOrder {
        int64_t algoId{0};
        int64_t pairId{0};
        int64_t clOrdId{0};
        int64_t sysOrdId{0};
        char exOrdId[ID_LEN];
        char instrument[INST_ID_LEN]{0};
        ExchangeType exchangType{ExchangeType_MIN};
        InstType instType{InstType_MIN};
        OrderStatus orderStatus{OS_MIN};
        Direction direction{DT_MIN};
        OrderType orderType{OT_MIN};
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

    };

    struct TdPosition {
        int accountId{0};
        ExchangeType exchangType{ExchangeType_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        char instrument[INST_ID_LEN]{0};
        Direction direction{DT_MIN};
        double volume{0.0};
        double maintMargin{0.0};
        double avgPrice{0.0};
        double unrealizedPnl{0.0};
        double liquidPrice{0.0};
        double adlQuantile{0.0};
        double markPrice{0.0};
        int64_t updateTime{0};

    };

    struct TdBalance {
        int accountId{0};
        ExchangeType exchangType{ExchangeType_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        char currency[ASSET_LEN]{0};
        double total{0.0};
        double available{0.0};
        double unrealizedPnl{0.0};
        double frozen{0.0};
        int64_t updateTime{0};

    };

    struct TdTotalAccount {
        int accountId{0};
        ExchangeType exchangType{ExchangeType_MIN};
        InstType instType{InstType_MIN};
        char strategyId[ID_LEN]{0};
        double totalEquity{0.0};
        double adjEquity{0.0};
        double mmr{0.0};
        double mgnRatio{0.0};
        int64_t updateTime{0};

    };

    struct QuantOrder {
        int64_t strategyOrderId{-1};
        char systemOrderId[64]{0};
        char exchangeOrderId[64]{0};
        int strategyAccountId{-1};
        int64_t pairId{-1};
        int64_t algoPairId{-1};
        char instrument[INST_ID_LEN]{0};
        char instrumentKey[INST_KEY_LEN]{0};
        char pairInstrumentKey[INST_KEY_LEN]{0};
        ExchangeType exchangeType{ExchangeType_MIN};
        InstType instType{InstType_MIN};
        OrderType orderType{OT_MIN};
        Direction direction{DT_MIN};
        OffsetFlag offsetFlag{OF_MIN};
        OrderStatus orderStatus{OS_MIN};
        TradingType tradingType{TradingType_MIN};
        TradingType tradingTypeOffset{TradingType_MIN};
        double price{0.0};
        double volume{0.0};
        double targetPrice{0.0};
        double totalPriceOnOrder{-1.0};
        double totalVolumeOnOrder{0.0};
        double tradePrice{-1.0};
        double tradeVolume{0.0};
        int errorId{0};
        char originErrorMsg[128]{""};
        char strategyName[NAME_LEN]{""};
        bool reduceOnly{false};
        bool isActiveOrder{false};
        bool rebalance{false};
        int64_t updateTime{0};
        int queryCount{0};

        QuantOrder() {
            updateTime = crypto::getCurrentTime();
        }

        

        QuantOrder UpdateOrderOnOrder(const pubsub::OrderResponse& orderResponse) {
            queryCount = 0;

            orderStatus = orderResponse.orderStatus;
            updateTime = crypto::getCurrentTime();
            errorId = orderResponse.errorId;
            strncpy(originErrorMsg, orderResponse.originMsg, 128);
            strncpy(systemOrderId, orderResponse.orderSysId, 64);
            strncpy(exchangeOrderId, orderResponse.orderId, 64);

            if (orderResponse.instTypeEnum == C_SWAP || orderResponse.instTypeEnum == C_FUTURES) {
                tradeVolume = orderResponse.volumeTraded - totalVolumeOnOrder;
                tradePrice = -1.0;
                if (tradeVolume > stra::MIN_FLOAT) {
                    tradePrice = tradeVolume / (orderResponse.volumeTraded / orderResponse.tradePrice  - totalVolumeOnOrder / totalPriceOnOrder);
                }

                totalVolumeOnOrder = orderResponse.volumeTraded;
                totalPriceOnOrder = orderResponse.tradePrice;
            } else {
                tradeVolume = orderResponse.volumeTraded - totalVolumeOnOrder;
                tradePrice = -1.0;
                if (tradeVolume > stra::MIN_FLOAT) {
                    tradePrice = (orderResponse.volumeTraded * orderResponse.tradePrice  - totalVolumeOnOrder * totalPriceOnOrder) / tradeVolume;
                }

                totalVolumeOnOrder = orderResponse.volumeTraded;
                totalPriceOnOrder = orderResponse.tradePrice;
            }
            
            return *this;
        }

        QuantOrder UpdateOrderOnQueryOrder(const pubsub::OrderResponse& orderResponse) {
            queryCount += 1;
            orderStatus = orderResponse.orderStatus;
            updateTime = crypto::getCurrentTime();
            errorId = orderResponse.errorId;
            strncpy(originErrorMsg, orderResponse.originMsg, 128);
            strncpy(systemOrderId, orderResponse.orderSysId, 64);
            strncpy(exchangeOrderId, orderResponse.orderId, 64);

            if (orderResponse.instTypeEnum == C_SWAP || orderResponse.instTypeEnum == C_FUTURES) {
                tradeVolume = orderResponse.volumeTraded - totalVolumeOnOrder;
                tradePrice = -1.0;
                if (tradeVolume > stra::MIN_FLOAT) {
                    tradePrice = tradeVolume / (orderResponse.volumeTraded / orderResponse.tradePrice  - totalVolumeOnOrder / totalPriceOnOrder);
                }

                totalVolumeOnOrder = orderResponse.volumeTraded;
                totalPriceOnOrder = orderResponse.tradePrice;
            } else {
                tradeVolume = orderResponse.volumeTraded - totalVolumeOnOrder;
                tradePrice = -1.0;
                if (tradeVolume > stra::MIN_FLOAT) {
                    tradePrice = (orderResponse.volumeTraded * orderResponse.tradePrice  - totalVolumeOnOrder * totalPriceOnOrder) / tradeVolume;
                }

                totalVolumeOnOrder = orderResponse.volumeTraded;
                totalPriceOnOrder = orderResponse.tradePrice;
            }

            return *this;
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


inline double GetAmountByVolumePrice(const md::InstrumentInfo& info, string baseAsset, double volume, double price) {
    double amount = 0.0;
    if (info.quote == baseAsset || ((info.quote == "USDT" || info.quote == "USD" || info.quote == "USDC" || info.quote == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calcType == 0) {
            amount = volume * info.value;
        } else if (info.calcType == 1) {
            amount = volume * info.value / price;
        }
    } else if (info.base == baseAsset || ((info.base == "USDT" || info.base == "USD" || info.base == "USDC" || info.base == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calcType == 0) {
            amount = volume * info.value * price;
        } else if (info.calcType == 1) {
            amount = volume * info.value;
        }
    }
    return amount;
}

inline double GetVolumeByAmountPrice(const md::InstrumentInfo& info, string baseAsset, double amount, double price) {
    double volume = 0.0;
    if (info.quote == baseAsset || ((info.quote == "USDT" || info.quote == "USD" || info.quote == "USDC" || info.quote == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calcType == 0) {
            volume = amount / info.value;
        } else if (info.calcType == 1) {
            volume = amount * price / info.value;
        }
    } else if (info.base == baseAsset || ((info.base == "USDT" || info.base == "USD" || info.base == "USDC" || info.base == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calcType == 0) {
            volume = amount / price / info.value;
        } else if (info.calcType == 1) {
            volume = amount / info.value;
        }
    }
    return volume;
}


extern std::unordered_map<std::string, int> mAccountNameAccountId;

#endif
