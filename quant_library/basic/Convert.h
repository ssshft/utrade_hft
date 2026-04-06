#ifndef _CONVERT_H
#define _CONVERT_H

#include "DataStruct.h"
#include "PairManager.h"
#include "AlgoPairOrder.h"
#include "AlgoFishingOrder.h"
#include "AlgoRebalanceOrder.h"
#include <string>

inline stra::QuantSpread ConvertTdSpreadToStraSpread(const stra::MdSpread& tdSpread) {
    stra::QuantSpread spread;
    spread.spreadDrive = tdSpread.spreadDrive;
    spread.spreadType = tdSpread.spreadType;
    spread.spreadEffective = tdSpread.spreadEffective;
    spread.statEffective = tdSpread.statEffective;
    strncpy(spread.pairInstrumentKey, tdSpread.pairInstrumentKey, stra::INST_KEY_LEN);
    strncpy(spread.activeInstumentKey, tdSpread.activeInstumentKey, stra::INST_KEY_LEN);
    strncpy(spread.passiveInstrumentKey, tdSpread.passiveInstrumentKey, stra::INST_KEY_LEN);

    spread.spreadBidAsk = -tdSpread.spreadBidAsk;
    spread.spreadBidBid = -tdSpread.spreadBidBid;
    spread.spreadAskBid = -tdSpread.spreadAskBid;
    spread.spreadAskAsk = -tdSpread.spreadAskAsk;

    spread.spreadBidAskTema = -tdSpread.spreadBidAskTema;
    spread.spreadBidBidTema = -tdSpread.spreadBidBidTema;
    spread.spreadAskBidTema = -tdSpread.spreadAskBidTema;
    spread.spreadAskAskTema = -tdSpread.spreadAskAskTema;

    spread.spreadBidAskMax = -tdSpread.spreadBidAskMax;
    spread.spreadBidBidMax = -tdSpread.spreadBidBidMax;
    spread.spreadAskBidMax = -tdSpread.spreadAskBidMax;
    spread.spreadAskAskMax = -tdSpread.spreadAskAskMax;

    spread.spreadBidAskMin = -tdSpread.spreadBidAskMin;
    spread.spreadBidBidMin = -tdSpread.spreadBidBidMin;
    spread.spreadAskBidMin = -tdSpread.spreadAskBidMin;
    spread.spreadAskAskMin = -tdSpread.spreadAskAskMin;

    spread.activePriceTema = tdSpread.activePriceTema;
    spread.passivePriceTema = tdSpread.passivePriceTema;

    spread.activeFundingRate = tdSpread.activeFundingRate;
    spread.passiveFundingRate = tdSpread.passiveFundingRate;
    spread.activeMultiply = tdSpread.activeMultiply;
    spread.passiveMultiply = tdSpread.passiveMultiply;

    spread.passiveAskPrice1 = tdSpread.passiveAskPrice1;
    spread.passiveAskVolume1 = tdSpread.passiveAskVolume1;
    spread.passiveAskPrice2 = tdSpread.passiveAskPrice2;
    spread.passiveAskVolume2 = tdSpread.passiveAskVolume2;
    spread.passiveAskPrice3 = tdSpread.passiveAskPrice3;
    spread.passiveAskVolume3 = tdSpread.passiveAskVolume3;
    spread.passiveAskPrice4 = tdSpread.passiveAskPrice4;
    spread.passiveAskVolume4 = tdSpread.passiveAskVolume4;
    spread.passiveAskPrice5 = tdSpread.passiveAskPrice5;
    spread.passiveAskVolume5 = tdSpread.passiveAskVolume5;

    spread.passiveBidPrice1 = tdSpread.passiveBidPrice1;
    spread.passiveBidVolume1 = tdSpread.passiveBidVolume1;
    spread.passiveBidPrice2 = tdSpread.passiveBidPrice2;
    spread.passiveBidVolume2 = tdSpread.passiveBidVolume2;
    spread.passiveBidPrice3 = tdSpread.passiveBidPrice3;
    spread.passiveBidVolume3 = tdSpread.passiveBidVolume3;
    spread.passiveBidPrice4 = tdSpread.passiveBidPrice4;
    spread.passiveBidVolume4 = tdSpread.passiveBidVolume4;
    spread.passiveBidPrice5 = tdSpread.passiveBidPrice5;
    spread.passiveBidVolume5 = tdSpread.passiveBidVolume5;

    spread.activeAskPrice1 = tdSpread.activeAskPrice1;
    spread.activeAskVolume1 = tdSpread.activeAskVolume1;
    spread.activeAskPrice2 = tdSpread.activeAskPrice2;
    spread.activeAskVolume2 = tdSpread.activeAskVolume2;
    spread.activeAskPrice3 = tdSpread.activeAskPrice3;
    spread.activeAskVolume3 = tdSpread.activeAskVolume3;
    spread.activeAskPrice4 = tdSpread.activeAskPrice4;
    spread.activeAskVolume4 = tdSpread.activeAskVolume4;
    spread.activeAskPrice5 = tdSpread.activeAskPrice5;
    spread.activeAskVolume5 = tdSpread.activeAskVolume5;

    spread.activeBidPrice1 = tdSpread.activeBidPrice1;
    spread.activeBidVolume1 = tdSpread.activeBidVolume1;
    spread.activeBidPrice2 = tdSpread.activeBidPrice2;
    spread.activeBidVolume2 = tdSpread.activeBidVolume2;
    spread.activeBidPrice3 = tdSpread.activeBidPrice3;
    spread.activeBidVolume3 = tdSpread.activeBidVolume3;
    spread.activeBidPrice4 = tdSpread.activeBidPrice4;
    spread.activeBidVolume4 = tdSpread.activeBidVolume4;
    spread.activeBidPrice5 = tdSpread.activeBidPrice5;
    spread.activeBidVolume5 = tdSpread.activeBidVolume5;

    spread.activeFundingTs = tdSpread.activeFundingTs;
    spread.passiveFundingTs = tdSpread.passiveFundingTs;
    spread.activeDepthTs = tdSpread.activeDepthTs;
    spread.passiveDepthTs = tdSpread.passiveDepthTs;
    spread.generateTs = tdSpread.generateTs;
    spread.diffTs = tdSpread.diffTs;

    spread.activeDepthDelay = tdSpread.activeDepthDelay;
    spread.passiveDepthDelay = tdSpread.passiveDepthDelay;

    spread.exchActiveTradeDelay = tdSpread.exchActiveTradeDelay;
    spread.exchPassiveTradeDelay = tdSpread.exchPassiveTradeDelay;

    return spread;
}

inline void WriteQuantOrder(const stra::QuantOrder& order, const stra::QuantSpread& spread) {
    LOG_INFO("WriteQuantOrder start format!");
	char s[stra::STR_LEN * 2];
    sprintf(s, "%s,%ld,%ld,%s,%s,%s,%s,%s,%s,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%s,%s,%ld,%ld,%ld,%d,%s,%d,%ld,%ld,%d,%d", 
					order.strategyName, order.strategyOrderId, order.systemOrderId, order.exchangeOrderId, order.instrumentKey,
		            stra::OrderTypeEnum2Str[order.orderType].c_str(), stra::DirectionEnum2Str[order.direction].c_str(), stra::OrderStatusEnum2Str[order.orderStatus].c_str(), order.orderTimeStatus.GetStr().c_str(),
                    order.targetPrice, order.price, order.volume, order.totalPriceOnOrder, order.totalVolumeOnOrder, order.tradeVolume,
                    spread.activeBidPrice1, spread.activeBidVolume1, spread.activeAskPrice1, spread.activeAskVolume1,
                    spread.passiveBidPrice1, spread.passiveBidVolume1, spread.passiveAskPrice1, spread.passiveAskVolume1,
		            order.totalShortFee.GetStr().c_str(), order.totalLongFee.GetStr().c_str(),
		            order.orderTime, order.updateTime, order.killTime,
                    order.errorId, order.originErrorMsg, order.reduceOnly,
                    order.pairId, order.algoPairId, order.isActiveOrder, order.rebalance);  
    LOG_INFO("WriteQuantOrder start push queue!");
    content c;
    c.type = 1;
    c.msg = s;
    LOG_INFO("WriteQuantOrder push queue!");
    contentQueue.Push(c);
}

inline void WritePairOrder(const PairOrder& order, const stra::QuantSpread& spread) {
    char s[stra::STR_LEN];
    sprintf(s, "%ld,%ld,%s,%s,%s,%s,%f,%s,%s,%f,%f,%f,%f,%f,%s,%s,%f,%f,%f,%f,%f,%f,%f,%f,%f,%ld,%ld,%ld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d,%d,%ld,%ld,%.13f", 
                    order.pairId, order.algoPairId, order.strategyName, order.baseAsset, stra::TradingTypeEnum2Str[order.tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[order.tradingTypeOffset].c_str(), order.targetVolume,
                    order.activeInstrumentKey, stra::DirectionEnum2Str[order.activeDirection].c_str(), order.activeTargetPrice, spread.activeBidPrice1, spread.activeBidVolume1, spread.activeAskPrice1, spread.activeAskVolume1,
                    order.passiveInstrumentKey, stra::DirectionEnum2Str[order.passiveDirection].c_str(), order.passiveTargetPrice, spread.passiveBidPrice1, spread.passiveBidVolume1, spread.passiveAskPrice1, spread.passiveAskVolume1,
                    spread.spreadBidAsk, spread.spreadBidBid, spread.spreadAskBid, spread.spreadAskAsk, spread.generateTs, spread.activeDepthTs, spread.passiveDepthTs,
                    order.activeTotalPriceOnOrder, order.activeTotalVolumeOnOrder, order.passiveTotalPriceOnOrder, order.passiveTotalVolumeOnOrder,
                    order.pairTotalVolume, order.pairActiveTotalPrice, order.pairPassiveTotalPrice,
                    order.activeFrozenPrice, order.activeFrozenVolume, order.passiveFrozenPrice, order.passiveFrozenVolume,
                    order.activeAccountId, order.passiveAccountId, order.status, order.rebalanceFlag, order.updateTime, order.createTime, order.pairTargetSpread);

    content c;
    c.type = 2;
    c.msg = s;
    contentQueue.Push(c);
}

inline void WriteAlgoOrder(BaseAlgoOrder* ord) {
    if (ord->algoType == stra::AlgoType_PairTrading) {
        AlgoPairOrder* order = (AlgoPairOrder*)ord;
        char s[stra::STR_LEN];
        sprintf(s, "%s,%s,%ld,%s,%s,%s,"
                    "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                    "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                    "%f,%ld,%ld,%ld,%ld,%f,%f,%f,%f,%f,"
                    "%f,%f,%f,%f,%f,%f,%f,%f,"
                    "%f,%f,%f,"
                    "%f,%f,%f,%f,"
                    "%s,%s,%f,%f", 
                    stra::AlgoTypeEnum2Str[order->algoType].c_str(), order->algoStrategyName, order->algoOrderId, order->pairInstrumentKey, order->baseAsset, stra::OrderStatusEnum2Str[order->algoOrderStatus].c_str(), 
                    order->activeInstrumentKey, order->activePriceTakerPct, order->activePriceMakerPct, order->activeAccountId, stra::DriveTypeEnum2Str[order->activeDriveType].c_str(), order->activeDepthMakerCheck, order->activeDepthTakerCheck, stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->activeOrderType].c_str(), 
                    order->passiveInstrumentKey, order->passivePriceTakerPct, order->passivePriceMakerPct, order->passiveAccountId, stra::DriveTypeEnum2Str[order->passiveDriveType].c_str(), order->passiveDepthMakerCheck, order->passiveDepthTakerCheck, stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->passiveOrderType].c_str(), 
                    order->passiveVolumePct, order->activeMakerCancelOrderTime, order->activeTakerCancelOrderTime, order->passiveMakerCancelOrderTime, order->passiveTakerCancelOrderTime, order->activePassiveCancelOrderPct, order->activeMakerCancelOrderPct, order->activeTakerCancelOrderPct, order->passiveMakerCancelOrderPct, order->passiveTakerCancelOrderPct,
                    order->activeMakerFeeRate, order->activeTakerFeeRate, order->passiveMakerFeeRate, order->passiveTakerFeeRate, order->activeTakerSlippage, order->activeMakerSlippage, order->passiveTakerSlippage, order->passiveMakerSlippage,
                    order->pairActiveTotalPrice, order->pairTotalVolume, order->pairPassiveTotalPrice,
                    order->makerTakerFs, order->takerTakerFs, order->maxMTOrderSize, order->maxTTOrderSize,
                    stra::TargetSpredPriceEnum2Str[order->targetSpreadType].c_str(), stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType].c_str(), order->ttTargetVolume, order->mtTargetVolume
                    );

        content c;
        c.type = 3;
        c.msg = s;
        contentQueue.Push(c);
    } else if (ord->algoType == stra::AlgoType_FishingTrading) {
        AlgoFishingOrder* order = (AlgoFishingOrder*)ord;
        char s[stra::STR_LEN];
        sprintf(s, "%s,%s,%ld,%s,%s,%s,"
                    "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                    "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                    "%f,%ld,%ld,%ld,%ld,%f,%f,%f,%f,%f,"
                    "%f,%f,%f,%f,%f,%f,%f,%f,"
                    "%f,%f,%f,"
                    "%f,%f,%f,%f,"
                    "%s,%s,%f,%f,%f", 
                    stra::AlgoTypeEnum2Str[order->algoType].c_str(), order->algoStrategyName, order->algoOrderId, order->pairInstrumentKey, order->baseAsset, stra::OrderStatusEnum2Str[order->algoOrderStatus].c_str(), 
                    order->activeInstrumentKey, order->activePriceTakerPct, order->activePriceMakerPct, order->activeAccountId, stra::DriveTypeEnum2Str[order->activeDriveType].c_str(), order->activeDepthMakerCheck, order->activeDepthTakerCheck, stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->activeOrderType].c_str(), 
                    order->passiveInstrumentKey, order->passivePriceTakerPct, order->passivePriceMakerPct, order->passiveAccountId, stra::DriveTypeEnum2Str[order->passiveDriveType].c_str(), order->passiveDepthMakerCheck, order->passiveDepthTakerCheck, stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->passiveOrderType].c_str(), 
                    order->passiveVolumePct, order->activeMakerCancelOrderTime, order->activeTakerCancelOrderTime, order->passiveMakerCancelOrderTime, order->passiveTakerCancelOrderTime, order->activePassiveCancelOrderPct, order->activeMakerCancelOrderPct, order->activeTakerCancelOrderPct, order->passiveMakerCancelOrderPct, order->passiveTakerCancelOrderPct,
                    order->activeMakerFeeRate, order->activeTakerFeeRate, order->passiveMakerFeeRate, order->passiveTakerFeeRate, order->activeTakerSlippage, order->activeMakerSlippage, order->passiveTakerSlippage, order->passiveMakerSlippage,
                    order->pairActiveTotalPrice, order->pairTotalVolume, order->pairPassiveTotalPrice,
                    order->makerTakerFs, order->takerTakerFs, order->maxMTOrderSize, order->maxTTOrderSize,
                    stra::TargetSpredPriceEnum2Str[order->targetSpreadType].c_str(), stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType].c_str(), order->ttTargetVolume, order->mtTargetVolume, order->fishingSlippagePct
                    );

        content c;
        c.type = 4;
        c.msg = s;
        contentQueue.Push(c);
    }
}

inline void WriteAlgoPairOrder(AlgoPairOrder* order) {
    char s[stra::STR_LEN];
    sprintf(s, "%s,%s,%ld,%s,%s,%s,"
                "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                "%f,%ld,%ld,%ld,%ld,%f,%f,%f,%f,%f,"
                "%f,%f,%f,%f,%f,%f,%f,%f,"
                "%f,%f,%f,"
                "%f,%f,%f,%f,"
                "%s,%s,%f,%f", 
                stra::AlgoTypeEnum2Str[order->algoType].c_str(), order->algoStrategyName, order->algoOrderId, order->pairInstrumentKey, order->baseAsset, stra::OrderStatusEnum2Str[order->algoOrderStatus].c_str(), 
                order->activeInstrumentKey, order->activePriceTakerPct, order->activePriceMakerPct, order->activeAccountId, stra::DriveTypeEnum2Str[order->activeDriveType].c_str(), order->activeDepthMakerCheck, order->activeDepthTakerCheck, stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->activeOrderType].c_str(), 
                order->passiveInstrumentKey, order->passivePriceTakerPct, order->passivePriceMakerPct, order->passiveAccountId, stra::DriveTypeEnum2Str[order->passiveDriveType].c_str(), order->passiveDepthMakerCheck, order->passiveDepthTakerCheck, stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->passiveOrderType].c_str(), 
                order->passiveVolumePct, order->activeMakerCancelOrderTime, order->activeTakerCancelOrderTime, order->passiveMakerCancelOrderTime, order->passiveTakerCancelOrderTime, order->activePassiveCancelOrderPct, order->activeMakerCancelOrderPct, order->activeTakerCancelOrderPct, order->passiveMakerCancelOrderPct, order->passiveTakerCancelOrderPct,
                order->activeMakerFeeRate, order->activeTakerFeeRate, order->passiveMakerFeeRate, order->passiveTakerFeeRate, order->activeTakerSlippage, order->activeMakerSlippage, order->passiveTakerSlippage, order->passiveMakerSlippage,
                order->pairActiveTotalPrice, order->pairTotalVolume, order->pairPassiveTotalPrice,
                order->makerTakerFs, order->takerTakerFs, order->maxMTOrderSize, order->maxTTOrderSize,
                stra::TargetSpredPriceEnum2Str[order->targetSpreadType].c_str(), stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType].c_str(), order->ttTargetVolume, order->mtTargetVolume
                );

    content c;
    c.type = 3;
    c.msg = s;
    contentQueue.Push(c);
}

inline void WriteAlgoFishingOrder(AlgoFishingOrder* order) {
    char s[stra::STR_LEN];
    sprintf(s, "%s,%s,%ld,%s,%s,%s,"
                "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                "%s,%f,%f,%d,%s,%d,%d,%s,%s,%s,"
                "%f,%ld,%ld,%ld,%ld,%f,%f,%f,%f,%f,"
                "%f,%f,%f,%f,%f,%f,%f,%f,"
                "%f,%f,%f,"
                "%f,%f,%f,%f,"
                "%s,%s,%f,%f,%f", 
                stra::AlgoTypeEnum2Str[order->algoType].c_str(), order->algoStrategyName, order->algoOrderId, order->pairInstrumentKey, order->baseAsset, stra::OrderStatusEnum2Str[order->algoOrderStatus].c_str(), 
                order->activeInstrumentKey, order->activePriceTakerPct, order->activePriceMakerPct, order->activeAccountId, stra::DriveTypeEnum2Str[order->activeDriveType].c_str(), order->activeDepthMakerCheck, order->activeDepthTakerCheck, stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->activeOrderType].c_str(), 
                order->passiveInstrumentKey, order->passivePriceTakerPct, order->passivePriceMakerPct, order->passiveAccountId, stra::DriveTypeEnum2Str[order->passiveDriveType].c_str(), order->passiveDepthMakerCheck, order->passiveDepthTakerCheck, stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType].c_str(), stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType].c_str(), stra::OrderTypeEnum2Str[order->passiveOrderType].c_str(), 
                order->passiveVolumePct, order->activeMakerCancelOrderTime, order->activeTakerCancelOrderTime, order->passiveMakerCancelOrderTime, order->passiveTakerCancelOrderTime, order->activePassiveCancelOrderPct, order->activeMakerCancelOrderPct, order->activeTakerCancelOrderPct, order->passiveMakerCancelOrderPct, order->passiveTakerCancelOrderPct,
                order->activeMakerFeeRate, order->activeTakerFeeRate, order->passiveMakerFeeRate, order->passiveTakerFeeRate, order->activeTakerSlippage, order->activeMakerSlippage, order->passiveTakerSlippage, order->passiveMakerSlippage,
                order->pairActiveTotalPrice, order->pairTotalVolume, order->pairPassiveTotalPrice,
                order->makerTakerFs, order->takerTakerFs, order->maxMTOrderSize, order->maxTTOrderSize,
                stra::TargetSpredPriceEnum2Str[order->targetSpreadType].c_str(), stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType].c_str(), order->ttTargetVolume, order->mtTargetVolume, order->fishingSlippagePct
                );

    content c;
    c.type = 4;
    c.msg = s;
    contentQueue.Push(c);
}

inline void WriteAlgoRebalanceOrder(AlgoRebalanceOrder* order) {

}

#endif
