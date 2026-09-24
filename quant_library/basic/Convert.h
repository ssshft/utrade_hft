#ifndef _CONVERT_H
#define _CONVERT_H

#include "DataStruct.h"
#include "PairManager.h"
#include "AlgoPairOrder.h"
#include "AlgoFishingOrder.h"
#include "AlgoRebalanceOrder.h"
#include "dbp/include.h"
#include <string>


inline void WriteQuantOrder(const stra::QuantOrder& order, const dbp::DbpData* pdata) {
    content c;
    c.type = 1;
    stra::QuantOrderRecord& r = c.quantOrder;
    strncpy(r.strategyName, order.strategyName, sizeof(r.strategyName) - 1);
    r.strategyOrderId = order.strategyOrderId;
    strncpy(r.systemOrderId, order.systemOrderId, sizeof(r.systemOrderId) - 1);
    strncpy(r.exchangeOrderId, order.exchangeOrderId, sizeof(r.exchangeOrderId) - 1);
    strncpy(r.instrumentKey, order.instrumentKey, sizeof(r.instrumentKey) - 1);
    r.orderType = int(order.orderType);
    r.direction = int(order.direction);
    r.orderStatus = int(order.orderStatus);
    r.targetPrice = order.targetPrice;
    r.price = order.price;
    r.volume = order.volume;
    r.totalPriceOnOrder = order.totalPriceOnOrder;
    r.totalVolumeOnOrder = order.totalVolumeOnOrder;
    r.tradeVolume = order.tradeVolume;
    r.updateTime = order.updateTime;
    r.errorId = order.errorId;
    strncpy(r.originErrorMsg, order.originErrorMsg, sizeof(r.originErrorMsg) - 1);
    r.reduceOnly = order.reduceOnly;
    r.pairId = order.pairId;
    r.algoPairId = order.algoPairId;
    r.isActiveOrder = order.isActiveOrder;
    r.rebalance = order.rebalance;

    r.dbp.From(pdata);

    contentQueue.Push(c);
}

inline void WritePairOrder(const PairOrder& order, const dbp::DbpData* pdata) {
    content c;
    c.type = 2;
    stra::PairOrderRecord& r = c.pairOrder;
    r.pairId = order.pairId;
    r.algoPairId = order.algoPairId;
    strncpy(r.strategyName, order.strategyName, sizeof(r.strategyName) - 1);
    strncpy(r.baseAsset, order.baseAsset, sizeof(r.baseAsset) - 1);
    r.tradingTypeOrder = int(order.tradingTypeOrder);
    r.tradingTypeOffset = int(order.tradingTypeOffset);
    r.targetVolume = order.targetVolume;
    strncpy(r.activeInstrumentKey, order.activeInstrumentKey, sizeof(r.activeInstrumentKey) - 1);
    r.activeDirection = int(order.activeDirection);
    r.activeTargetPrice = order.activeTargetPrice;
    strncpy(r.passiveInstrumentKey, order.passiveInstrumentKey, sizeof(r.passiveInstrumentKey) - 1);
    r.passiveDirection = int(order.passiveDirection);
    r.passiveTargetPrice = order.passiveTargetPrice;
  
    r.activeTotalPriceOnOrder = order.activeTotalPriceOnOrder;
    r.activeTotalVolumeOnOrder = order.activeTotalVolumeOnOrder;
    r.passiveTotalPriceOnOrder = order.passiveTotalPriceOnOrder;
    r.passiveTotalVolumeOnOrder = order.passiveTotalVolumeOnOrder;
    r.pairTotalVolume = order.pairTotalVolume;
    r.pairActiveTotalPrice = order.pairActiveTotalPrice;
    r.pairPassiveTotalPrice = order.pairPassiveTotalPrice;
    r.activeFrozenPrice = order.activeFrozenPrice;
    r.activeFrozenVolume = order.activeFrozenVolume;
    r.passiveFrozenPrice = order.passiveFrozenPrice;
    r.passiveFrozenVolume = order.passiveFrozenVolume;
    r.activeAccountId = order.activeAccountId;
    r.passiveAccountId = order.passiveAccountId;
    r.status = order.status;
    r.rebalanceFlag = order.rebalanceFlag;
    r.updateTime = order.updateTime;
    r.createTime = order.createTime;
    r.pairTargetSpread = order.pairTargetSpread;

    r.dbp.From(pdata);

    contentQueue.Push(c);
}


inline void WriteAlgoOrder(BaseAlgoOrder* ord) {
    if (!ord) {
        return;
    }

    content c;
    c.type = 3;                        // 三种算法单合并到一个文件（表头是 57 列超集）
    stra::AlgoOrderRecord& r = c.algoOrder;
    r.algoType = int(ord->algoType);
    strncpy(r.algoStrategyName, ord->algoStrategyName, sizeof(r.algoStrategyName) - 1);
    r.algoOrderId = ord->algoOrderId;
    strncpy(r.pairInstrumentKey, ord->pairInstrumentKey, sizeof(r.pairInstrumentKey) - 1);
    strncpy(r.baseAsset, ord->baseAsset, sizeof(r.baseAsset) - 1);
    r.algoOrderStatus = int(ord->algoOrderStatus);
    r.updateTime = ord->updateTime;

    strncpy(r.activeInstrumentKey, ord->activeInstrumentKey, sizeof(r.activeInstrumentKey) - 1);
    r.activePriceTakerPct = ord->activePriceTakerPct;
    r.activePriceMakerPct = ord->activePriceMakerPct;
    r.activeAccountId = ord->activeAccountId;
    r.activeDriveType = int(ord->activeDriveType);
    r.activeDepthMakerCheck = ord->activeDepthMakerCheck;
    r.activeDepthTakerCheck = ord->activeDepthTakerCheck;
    r.activeDepthMakerCheckType = int(ord->activeDepthMakerCheckType);
    r.activeDepthTakerCheckType = int(ord->activeDepthTakerCheckType);
    r.activeOrderType = int(ord->activeOrderType);

    strncpy(r.passiveInstrumentKey, ord->passiveInstrumentKey, sizeof(r.passiveInstrumentKey) - 1);
    r.passivePriceTakerPct = ord->passivePriceTakerPct;
    r.passivePriceMakerPct = ord->passivePriceMakerPct;
    r.passiveAccountId = ord->passiveAccountId;
    r.passiveDriveType = int(ord->passiveDriveType);
    r.passiveDepthMakerCheck = ord->passiveDepthMakerCheck;
    r.passiveDepthTakerCheck = ord->passiveDepthTakerCheck;
    r.passiveDepthMakerCheckType = int(ord->passiveDepthMakerCheckType);
    r.passiveDepthTakerCheckType = int(ord->passiveDepthTakerCheckType);
    r.passiveOrderType = int(ord->passiveOrderType);

    r.passiveVolumePct = ord->passiveVolumePct;
    r.activeMakerCancelOrderTime = ord->activeMakerCancelOrderTime;
    r.activeTakerCancelOrderTime = ord->activeTakerCancelOrderTime;
    r.passiveMakerCancelOrderTime = ord->passiveMakerCancelOrderTime;
    r.passiveTakerCancelOrderTime = ord->passiveTakerCancelOrderTime;
    r.activePassiveCancelOrderPct = ord->activePassiveCancelOrderPct;
    r.activeMakerCancelOrderPct = ord->activeMakerCancelOrderPct;
    r.activeTakerCancelOrderPct = ord->activeTakerCancelOrderPct;
    r.passiveMakerCancelOrderPct = ord->passiveMakerCancelOrderPct;
    r.passiveTakerCancelOrderPct = ord->passiveTakerCancelOrderPct;

    r.activeMakerFeeRate = ord->activeMakerFeeRate;
    r.activeTakerFeeRate = ord->activeTakerFeeRate;
    r.passiveMakerFeeRate = ord->passiveMakerFeeRate;
    r.passiveTakerFeeRate = ord->passiveTakerFeeRate;
    r.activeTakerSlippage = ord->activeTakerSlippage;
    r.activeMakerSlippage = ord->activeMakerSlippage;
    r.passiveTakerSlippage = ord->passiveTakerSlippage;
    r.passiveMakerSlippage = ord->passiveMakerSlippage;
        
    r.pairActiveTotalPrice = ord->pairActiveTotalPrice;
    r.pairTotalVolume = ord->pairTotalVolume;
    r.pairPassiveTotalPrice = ord->pairPassiveTotalPrice;

    r.makerTakerFs = ord->makerTakerFs;
    r.takerTakerFs = ord->takerTakerFs;
    r.maxMTOrderSize = ord->maxMTOrderSize;
    r.maxTTOrderSize = ord->maxTTOrderSize;

    r.targetSpreadType = int(ord->targetSpreadType);
    r.activeVolumeCalcualteType = int(ord->activeVolumeCalcualteType);
    r.ttTargetVolume = ord->ttTargetVolume;
    r.mtTargetVolume = ord->mtTargetVolume;

    // 子类专属字段：表头对三种算法单是同一套 57 列，所以不匹配的类型必须显式留 0，
    // 不能依赖 content 的默认值（content 的构造函数已经 memset 清零，这里是双保险）
    if (ord->algoType == stra::AlgoType_FishingTrading) {
        AlgoFishingOrder* order = static_cast<AlgoFishingOrder*>(ord);
        r.fishingSlippagePct = order->fishingSlippagePct;
    }
    else if (ord->algoType == stra::AlgoType_Rebalance) {
        AlgoRebalanceOrder* order = static_cast<AlgoRebalanceOrder*>(ord);
        r.activeTrade = order->activeTrade;
    }

    contentQueue.Push(c);
}

#endif
