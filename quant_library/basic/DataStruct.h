#ifndef _DATASTRUCT_H
#define _DATASTRUCT_H

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <unordered_map>
#include "StraException.h"
#include "crypto_errors.h"

using namespace std;

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

    static std::unordered_map<AlgoOrderStatus, std::string> AlgoOrderStatusEnum2Str {
	    {ALGO_OS_MIN, "ALGO_OS_MIN"},
        {ALGO_OS_PEND, "ALGO_OS_PEND"},
        {ALGO_OS_PENDING_NEW, "ALGO_OS_PENDING_NEW"},
        {ALGO_OS_NEW, "ALGO_OS_NEW"},
        {ALGO_OS_PARTFILLED, "ALGO_OS_PARTFILLED"},
        {ALGO_OS_FILLED, "ALGO_OS_FILLED"},
        {ALGO_OS_REJECTED, "ALGO_OS_REJECTED"},
        {ALGO_OS_CANCEL, "ALGO_OS_CANCEL"},
        {ALGO_OS_CANCELLING, "ALGO_OS_CANCELLING"},
        {ALGO_OS_CANCELED, "ALGO_OS_CANCELED"},
        {ALGO_OS_ERRORCANCELLING, "ALGO_OS_ERRORCANCELLING"},
        {ALGO_OS_ERRORCANCELED, "ALGO_OS_ERRORCANCELED"},
        {ALGO_OS_UNKNOWN, "ALGO_OS_UNKNOWN"},
        {ALGO_OS_FAILED, "ALGO_OS_FAILED"},
        {ALGO_OS_MAX, "ALGO_OS_MAX"}
    };

    static std::unordered_map<std::string, AlgoOrderStatus> AlgoOrderStatusStr2Enum {
	    {"ALGO_OS_MIN", ALGO_OS_MIN},
        {"ALGO_OS_PEND", ALGO_OS_PEND},
        {"ALGO_OS_PENDING_NEW", ALGO_OS_PENDING_NEW},
        {"ALGO_OS_NEW", ALGO_OS_NEW},
        {"ALGO_OS_PARTFILLED", ALGO_OS_PARTFILLED},
        {"ALGO_OS_FILLED", ALGO_OS_FILLED},
        {"ALGO_OS_REJECTED", ALGO_OS_REJECTED},
        {"ALGO_OS_CANCEL", ALGO_OS_CANCEL},
        {"ALGO_OS_CANCELLING", ALGO_OS_CANCELLING},
        {"ALGO_OS_CANCELED", ALGO_OS_CANCELED},
        {"ALGO_OS_ERRORCANCELLING", ALGO_OS_ERRORCANCELLING},
        {"ALGO_OS_ERRORCANCELED", ALGO_OS_ERRORCANCELED},
        {"ALGO_OS_UNKNOWN", ALGO_OS_UNKNOWN},
        {"ALGO_OS_FAILED", ALGO_OS_FAILED},
        {"ALGO_OS_MAX", ALGO_OS_MAX}
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



    struct DbpSnapshot {
        double activeBidPrice1{0};
        double activeBidVolume1{0};
        double activeAskPrice1{0};
        double activeAskVolume1{0};
        double passiveBidPrice1{0};
        double passiveBidVolume1{0};
        double passiveAskPrice1{0};
        double passiveAskVolume1{0};
        double spreadBidAsk{0};
        double spreadBidBid{0};
        double spreadAskBid{0};
        double spreadAskAsk{0};
        int64_t generateTs{0};
        int64_t activeDepthTs{0};
        int64_t passiveDepthTs{0};
        int64_t activeDepthDelay{0};
        int64_t passiveDepthDelay{0};

        // pdata 可能为 null（BaseAlgoOrder.cpp:713 拿到的 GetSpread 就可能返回空），要判空
        void From(const dbp::DbpData* p) {
            if (!p) { 
                return; 
            }
            activeBidPrice1 = p->activeBidPrice[0];
            activeBidVolume1 = p->activeBidVolume[0];
            activeAskPrice1 = p->activeAskPrice[0];
            activeAskVolume1 = p->activeAskVolume[0];
            passiveBidPrice1 = p->passiveBidPrice[0];
            passiveBidVolume1 = p->passiveBidVolume[0];
            passiveAskPrice1  = p->passiveAskPrice[0];
            passiveAskVolume1 = p->passiveAskVolume[0];
            spreadBidAsk = p->spreadBidAsk;
            spreadBidBid = p->spreadBidBid;
            spreadAskBid = p->spreadAskBid;
            spreadAskAsk = p->spreadAskAsk;
            generateTs = p->generateTs;
            activeDepthTs = p->activeDepthTs;
            passiveDepthTs = p->passiveDepthTs;
            activeDepthDelay = p->activeDepthDelay;
            passiveDepthDelay = p->passiveDepthDelay;
        }
    };

    struct QuantOrderRecord {
        // 身份（对应 quantOrder.csv 第 1~5 列）
        char strategyName[32]{""};
        int64_t strategyOrderId{0};
        char systemOrderId[64]{""};
        char exchangeOrderId[64]{""};
        char instrumentKey[128]{""};
        // 枚举（存原始值，写线程再查 Enum2StrMap）
        int orderType{0};
        int direction{0};
        int orderStatus{0};
        // 订单自身（第 9~14 列）
        double targetPrice{0};
        double price{0};
        double volume{0};
        double totalPriceOnOrder{0};
        double totalVolumeOnOrder{0};
        double tradeVolume{0};
        // 行情快照（第 15~22 列）
        DbpSnapshot dbp;
        // 其余（第 23~33 列）
        int64_t updateTime{0};
        int errorId{0};
        char originErrorMsg[128]{""};
        bool reduceOnly{false};
        int64_t pairId{0};
        int64_t algoPairId{0};
        bool isActiveOrder{false};
        bool rebalance{false};
    };

    struct PairOrderRecord {
        // 与 pairOrder.csv 的 48 列表头 1:1，这里只列分类，落地时逐列写全
        int64_t pairId{0};
        int64_t algoPairId{0};
        char strategyName[32]{""};
        char baseAsset[32]{""};
        int tradingTypeOrder{0};
        int tradingTypeOffset{0};   // 存原始枚举值
        double targetVolume{0};
        char activeInstrumentKey[128]{""};
        int activeDirection{0};
        double activeTargetPrice{0};
        char passiveInstrumentKey[128]{""};
        int passiveDirection{0};
        double passiveTargetPrice{0};
        DbpSnapshot dbp;                                      // 第 11~14、18~30 列里的行情部分
        double activeTotalPriceOnOrder{0};
        double activeTotalVolumeOnOrder{0};
        double passiveTotalPriceOnOrder{0};
        double passiveTotalVolumeOnOrder{0};
        double pairTotalVolume{0};
        double pairActiveTotalPrice{0};
        double pairPassiveTotalPrice{0};
        double activeFrozenPrice{0};
        double activeFrozenVolume{0};
        double passiveFrozenPrice{0};
        double passiveFrozenVolume{0};
        int activeAccountId{0};
        int passiveAccountId{0};
        int status{0};
        bool rebalanceFlag{false};
        int64_t updateTime{0};
        int64_t createTime{0};
        double pairTargetSpread{0};
    };

    struct AlgoOrderRecord {
        int algoType{0};               // 决定写哪个文件 / 用哪套列
        char algoStrategyName[32]{""};
        int64_t algoOrderId{0};
        char pairInstrumentKey[128]{""};
        char baseAsset[32]{""};
        int algoOrderStatus{0};
        int64_t updateTime;

        char activeInstrumentKey[128]{""};
        double activePriceTakerPct{0};
        double activePriceMakerPct{0};
        int activeAccountId{0};
        int activeDriveType{0};
        bool activeDepthMakerCheck{false};
        bool activeDepthTakerCheck{false};
        int activeDepthMakerCheckType{0};
        int activeDepthTakerCheckType{0};
        int activeOrderType{0};

        char passiveInstrumentKey[128]{""};
        double passivePriceTakerPct{0};
        double passivePriceMakerPct{0};
        int passiveAccountId{0};
        int passiveDriveType{0};
        bool passiveDepthMakerCheck{false};
        bool passiveDepthTakerCheck{false};
        int passiveDepthMakerCheckType{0};
        int passiveDepthTakerCheckType{0};
        int passiveOrderType{0};

        double passiveVolumePct{0};
        int64_t activeMakerCancelOrderTime{0};
        int64_t activeTakerCancelOrderTime{0};
        int64_t passiveMakerCancelOrderTime{0};
        int64_t passiveTakerCancelOrderTime{0};
        double activePassiveCancelOrderPct{0};
        double activeMakerCancelOrderPct{0};
        double activeTakerCancelOrderPct{0};
        double passiveMakerCancelOrderPct{0};
        double passiveTakerCancelOrderPct{0};

        double activeMakerFeeRate{0};
        double activeTakerFeeRate{0};
        double passiveMakerFeeRate{0};
        double passiveTakerFeeRate{0};
        double activeTakerSlippage{0};
        double activeMakerSlippage{0};
        double passiveTakerSlippage{0};
        double passiveMakerSlippage{0};

        double pairActiveTotalPrice{0};
        double pairTotalVolume{0};
        double pairPassiveTotalPrice{0};
        double makerTakerFs{0};
        double takerTakerFs{0};
        double maxMTOrderSize{0};
        double maxTTOrderSize{0};
        int targetSpreadType{0};
        int activeVolumeCalcualteType{0};
        double ttTargetVolume{0};
        double mtTargetVolume{0};
        // 子类专属
        double fishingSlippagePct{0};     // AlgoFishingOrder
        int activeTrade{0};            // AlgoRebalanceOrder
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
            string s = fmt::format("asset:{},baseAsset:{},initAmount:{},totalAmount:{},transferAmount:{},frozenAmount:{},marginAmount:{},openMarginAmount:{},feeAmount:{},fundAmount:{},loanAmount:{},interestAmount:{},closeAmount:{},floatAmount:{},positionValue:{}", asset, baseAsset, initAmount, totalAmount, transferAmount, frozenAmount, marginAmount, 
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
            string s = fmt::format("instrumentKey:{},baseAsset:{},longPosition:{},longAvgPrice:{},shortPosition:{},shortAvgPrice:{},floatAmount:{},closeAmount:{},positionValue:{},frozenLongPosition:{},frozenLongPrice:{},frozenShortPosition:{},frozenShortPrice:{},lastFloatAmount:{},lastPositionValue:{}", instrumentKey, baseAsset, longPosition,
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

struct content {
    int type{0};
    union {
        stra::QuantOrderRecord quantOrder;
        stra::PairOrderRecord pairOrder;
        stra::AlgoOrderRecord algoOrder;
    };
    content() {}                       // 成员都是 POD，无需构造/析构
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
