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
    std::string s = fmt::format(
        "{},{},{},{},{},{},{},{},"      // strings + enums
        "{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},"  // targetPrice ~ totalVolumeOnOrder
        "{:.13f},{:.13f},{:.13f},{:.13f},"                  // spread.activeBidPrice1 ~ spread.activeAskVolume1
        "{:.13f},{:.13f},{:.13f},{:.13f},"                  // spread.passiveBidPrice1 ~ spread.passiveAskVolume1
        "{},{},{},{},{},{},{},{},{},{},{},{}", // remaining fields
        order.strategyName,
        order.strategyOrderId,
        order.systemOrderId,
        order.exchangeOrderId,
        order.instrumentKey,
        OrderTypeEnum2StrMap[order.orderType],
        DirectionEnum2StrMap[order.direction],
        OrderStatusEnum2StrMap[order.orderStatus],
    
        order.targetPrice,
        order.price,
        order.volume,
        order.totalPriceOnOrder,
        order.totalVolumeOnOrder,
        order.tradeVolume,
    
        pdata->activeBidPrice[0],
        pdata->activeBidVolume[0],
        pdata->activeAskPrice[0],
        pdata->activeAskVolume[0],

        pdata->passiveBidPrice[0],
        pdata->passiveBidVolume[0],
        pdata->passiveAskPrice[0],
        pdata->passiveAskVolume[0],
        
        order.updateTime,    
        order.errorId,
        order.originErrorMsg,
        order.reduceOnly,
    
        order.pairId,
        order.algoPairId,
        order.isActiveOrder,
        order.rebalance,
    
        pdata->activeDepthTs,
        pdata->passiveDepthTs,
        pdata->activeDepthDelay,
        pdata->passiveDepthDelay
    );

    content c;
    c.type = 1;
    c.msg = s;
    contentQueue.Push(c);
}

inline void WritePairOrder(const PairOrder& order, const dbp::DbpData* pdata) {
    std::string s = fmt::format(
        // 1-7
        "{},{},{},{},{},{},{:.13f}," 
        // 8-14
        "{},{},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f}," 
        // 15-21
        "{},{},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f}," 
        // 22-25
        "{:.13f},{:.13f},{:.13f},{:.13f}," 
        // 26-30
        "{},{},{},{},{},"
        // 31-41 (11 doubles)
        "{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},"
        // 42-48 (6 integers/long + 1 double)
        "{},{},{},{},{},{},{:.13f}",
        // ---- 参数 1 ~ 48 ----
        order.pairId,                         // 1  %ld
        order.algoPairId,                     // 2  %ld
        order.strategyName,                   // 3  %s
        order.baseAsset,                      // 4  %s
        stra::TradingTypeEnum2Str[order.tradingTypeOrder],   // 5  %s
        stra::TradingTypeEnum2Str[order.tradingTypeOffset],  // 6  %s
        order.targetVolume,                   // 7  %.13f
    
        order.activeInstrumentKey,            // 8  %s
        DirectionEnum2StrMap[order.activeDirection],     // 9  %s
        order.activeTargetPrice,              //10  %.13f

        pdata->activeBidPrice[0],               //11  %.13f
        pdata->activeBidVolume[0],              //12  %.13f
        pdata->activeAskPrice[0],               //13  %.13f
        pdata->activeAskVolume[0],              //14  %.13f
    
        order.passiveInstrumentKey,           //15  %s
        DirectionEnum2StrMap[order.passiveDirection],    //16  %s
        order.passiveTargetPrice,             //17  %.13f
        pdata->passiveBidPrice[0],              //18  %.13f
        pdata->passiveBidVolume[0],             //19  %.13f
        pdata->passiveAskPrice[0],              //20  %.13f
        pdata->passiveAskVolume[0],             //21  %.13f
    
        pdata->spreadBidAsk,                  //22  %.13f
        pdata->spreadBidBid,                  //23  %.13f
        pdata->spreadAskBid,                  //24  %.13f
        pdata->spreadAskAsk,                  //25  %.13f
    
        pdata->generateTs,                    //26  %ld (or suitable integer)
        pdata->activeDepthTs,                 //27  %ld
        pdata->passiveDepthTs,                //28  %ld
        pdata->activeDepthDelay,              //29  %ld
        pdata->passiveDepthDelay,             //30  %ld
    
        order.activeTotalPriceOnOrder,        //31  %.13f
        order.activeTotalVolumeOnOrder,       //32  %.13f
        order.passiveTotalPriceOnOrder,       //33  %.13f
        order.passiveTotalVolumeOnOrder,      //34  %.13f
        order.pairTotalVolume,                //35  %.13f
        order.pairActiveTotalPrice,           //36  %.13f
        order.pairPassiveTotalPrice,          //37  %.13f
        order.activeFrozenPrice,              //38  %.13f
        order.activeFrozenVolume,             //39  %.13f
        order.passiveFrozenPrice,             //40  %.13f
        order.passiveFrozenVolume,            //41  %.13f
    
        order.activeAccountId,                //42  %ld (or %d as appropriate)
        order.passiveAccountId,               //43  %ld
        order.status,                         //44  %d
        order.rebalanceFlag,                  //45  %d
        order.updateTime,                     //46  %ld
        order.createTime,                     //47  %ld
    
        order.pairTargetSpread                //48  %.13f
    );

    content c;
    c.type = 2;
    c.msg = s;
    contentQueue.Push(c);
}

inline void WriteAlgoOrder(BaseAlgoOrder* ord) {
    if (ord->algoType == stra::AlgoType_PairTrading) {
        AlgoPairOrder* order = (AlgoPairOrder*)ord;

        std::string s = fmt::format(
            // --- 第一段 ---
            "{},{},{},{},{},{},"                          // 6 字段（字符串/整数）
            // --- 第二段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10 字段 
            // --- 第三段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10 字段
            // --- 第四段 ---
            "{:.13f},{},{},{},{},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},"   // 10 字段
            // --- 第五段 ---
            "{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f}," // 8 字段
            // --- 第六段 ---
            "{:.13f},{:.13f},{:.13f},"                    // 3 字段
            // --- 第七段 ---
            "{:.13f},{:.13f},{},{},"            // 4 字段
            // --- 第八段 ---
            "{},{},{:.13f},{:.13f}",                      // 4 字段
        
            // 第一段
            stra::AlgoTypeEnum2Str[order->algoType],
            order->algoStrategyName,
            order->algoOrderId,
            order->pairInstrumentKey,
            order->baseAsset,
            OrderStatusEnum2StrMap[order->algoOrderStatus],
        
            // 第二段
            order->activeInstrumentKey,
            order->activePriceTakerPct,
            order->activePriceMakerPct,
            order->activeAccountId,
            stra::DriveTypeEnum2Str[order->activeDriveType],
            order->activeDepthMakerCheck,
            order->activeDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->activeOrderType],
        
            // 第三段
            order->passiveInstrumentKey,
            order->passivePriceTakerPct,
            order->passivePriceMakerPct,
            order->passiveAccountId,
            stra::DriveTypeEnum2Str[order->passiveDriveType],
            order->passiveDepthMakerCheck,
            order->passiveDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->passiveOrderType],
        
            // 第四段
            order->passiveVolumePct,
            order->activeMakerCancelOrderTime,
            order->activeTakerCancelOrderTime,
            order->passiveMakerCancelOrderTime,
            order->passiveTakerCancelOrderTime,
            order->activePassiveCancelOrderPct,
            order->activeMakerCancelOrderPct,
            order->activeTakerCancelOrderPct,
            order->passiveMakerCancelOrderPct,
            order->passiveTakerCancelOrderPct,
        
            // 第五段
            order->activeMakerFeeRate,
            order->activeTakerFeeRate,
            order->passiveMakerFeeRate,
            order->passiveTakerFeeRate,
            order->activeTakerSlippage,
            order->activeMakerSlippage,
            order->passiveTakerSlippage,
            order->passiveMakerSlippage,
        
            // 第六段
            order->pairActiveTotalPrice,
            order->pairTotalVolume,
            order->pairPassiveTotalPrice,
        
            // 第七段
            order->makerTakerFs,
            order->takerTakerFs,
            order->maxMTOrderSize,
            order->maxTTOrderSize,
        
            // 第八段
            stra::TargetSpredPriceEnum2Str[order->targetSpreadType],
            stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType],
            order->ttTargetVolume,
            order->mtTargetVolume
        );

        content c;
        c.type = 3;
        c.msg = s;
        contentQueue.Push(c);
    } else if (ord->algoType == stra::AlgoType_FishingTrading) {
        AlgoFishingOrder* order = (AlgoFishingOrder*)ord;

        std::string s = fmt::format(
            // --- 第一段 ---
            "{},{},{},{},{},{},"                        // 6
            // --- 第二段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10
            // --- 第三段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10
            // --- 第四段 ---
            "{:.13f},{},{},{},{},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},"   // 10
            // --- 第五段 ---
            "{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f}," // 8
            // --- 第六段 ---
            "{:.13f},{:.13f},{:.13f},"                   // 3
            // --- 第七段 ---
            "{:.13f},{:.13f},{},{},"
            // --- 第八段 ---
            "{},{},{:.13f},{:.13f},{:.13f}",            // 5
        
            // ================= 参数 =================
        
            // 第一段
            stra::AlgoTypeEnum2Str[order->algoType],
            order->algoStrategyName,
            order->algoOrderId,
            order->pairInstrumentKey,
            order->baseAsset,
            OrderStatusEnum2StrMap[order->algoOrderStatus],
        
            // 第二段
            order->activeInstrumentKey,
            order->activePriceTakerPct,
            order->activePriceMakerPct,
            order->activeAccountId,
            stra::DriveTypeEnum2Str[order->activeDriveType],
            order->activeDepthMakerCheck,
            order->activeDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->activeOrderType],
        
            // 第三段
            order->passiveInstrumentKey,
            order->passivePriceTakerPct,
            order->passivePriceMakerPct,
            order->passiveAccountId,
            stra::DriveTypeEnum2Str[order->passiveDriveType],
            order->passiveDepthMakerCheck,
            order->passiveDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->passiveOrderType],
        
            // 第四段
            order->passiveVolumePct,
            order->activeMakerCancelOrderTime,
            order->activeTakerCancelOrderTime,
            order->passiveMakerCancelOrderTime,
            order->passiveTakerCancelOrderTime,
            order->activePassiveCancelOrderPct,
            order->activeMakerCancelOrderPct,
            order->activeTakerCancelOrderPct,
            order->passiveMakerCancelOrderPct,
            order->passiveTakerCancelOrderPct,
        
            // 第五段
            order->activeMakerFeeRate,
            order->activeTakerFeeRate,
            order->passiveMakerFeeRate,
            order->passiveTakerFeeRate,
            order->activeTakerSlippage,
            order->activeMakerSlippage,
            order->passiveTakerSlippage,
            order->passiveMakerSlippage,
        
            // 第六段
            order->pairActiveTotalPrice,
            order->pairTotalVolume,
            order->pairPassiveTotalPrice,
        
            // 第七段
            order->makerTakerFs,
            order->takerTakerFs,
            order->maxMTOrderSize,
            order->maxTTOrderSize,
        
            // 第八段
            stra::TargetSpredPriceEnum2Str[order->targetSpreadType],
            stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType],
            order->ttTargetVolume,
            order->mtTargetVolume,
            order->fishingSlippagePct
        );

        content c;
        c.type = 4;
        c.msg = s;
        contentQueue.Push(c);
    } else if (ord->algoType == stra::AlgoType_Rebalance) {
        AlgoRebalanceOrder* order = (AlgoRebalanceOrder*)ord;

        std::string s = fmt::format(
            // --- 第一段 ---
            "{},{},{},{},{},{},"                     // 6
            // --- 第二段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10
            // --- 第三段 ---
            "{},{:.13f},{:.13f},{},{},{},{},{},{},{},"   // 10
            // --- 第四段 ---
            "{:.13f},{},{},{},{},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},"  // 10
            // --- 第五段 ---
            "{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f},{:.13f}," // 8
            // --- 第六段 ---
            "{:.13f},{:.13f},{:.13f},"               // 3
            // --- 第七段 ---
            "{:.13f},{:.13f},{},{},"                           // 4
            // --- 第八段 ---
            "{},{},{:.13f},{:.13f},{}",           // 5（最后是 order->activeTrade）
            // ================ 参数列表 ================
        
            // 第一段 (6)
            stra::AlgoTypeEnum2Str[order->algoType],
            order->algoStrategyName,
            order->algoOrderId,
            order->pairInstrumentKey,
            order->baseAsset,
            OrderStatusEnum2StrMap[order->algoOrderStatus],
        
            // 第二段 (10)
            order->activeInstrumentKey,
            order->activePriceTakerPct,
            order->activePriceMakerPct,
            order->activeAccountId,
            stra::DriveTypeEnum2Str[order->activeDriveType],
            order->activeDepthMakerCheck,
            order->activeDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->activeDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->activeDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->activeOrderType],
        
            // 第三段 (10)
            order->passiveInstrumentKey,
            order->passivePriceTakerPct,
            order->passivePriceMakerPct,
            order->passiveAccountId,
            stra::DriveTypeEnum2Str[order->passiveDriveType],
            order->passiveDepthMakerCheck,
            order->passiveDepthTakerCheck,
            stra::CheckTypeEnum2Str[order->passiveDepthMakerCheckType],
            stra::CheckTypeEnum2Str[order->passiveDepthTakerCheckType],
            OrderTypeEnum2StrMap[order->passiveOrderType],
        
            // 第四段 (10)
            order->passiveVolumePct,
            order->activeMakerCancelOrderTime,
            order->activeTakerCancelOrderTime,
            order->passiveMakerCancelOrderTime,
            order->passiveTakerCancelOrderTime,
            order->activePassiveCancelOrderPct,
            order->activeMakerCancelOrderPct,
            order->activeTakerCancelOrderPct,
            order->passiveMakerCancelOrderPct,
            order->passiveTakerCancelOrderPct,
        
            // 第五段 (8)
            order->activeMakerFeeRate,
            order->activeTakerFeeRate,
            order->passiveMakerFeeRate,
            order->passiveTakerFeeRate,
            order->activeTakerSlippage,
            order->activeMakerSlippage,
            order->passiveTakerSlippage,
            order->passiveMakerSlippage,
        
            // 第六段 (3)
            order->pairActiveTotalPrice,
            order->pairTotalVolume,
            order->pairPassiveTotalPrice,
        
            // 第七段 (4)
            order->makerTakerFs,
            order->takerTakerFs,
            order->maxMTOrderSize,
            order->maxTTOrderSize,
        
            // 第八段 (5)
            stra::TargetSpredPriceEnum2Str[order->targetSpreadType],
            stra::ActiveVolumeCalcualteTypeEnum2Str[order->activeVolumeCalcualteType],
            order->ttTargetVolume,
            order->mtTargetVolume,
            order->activeTrade
        );

        content c;
        c.type = 5;
        c.msg = s;
        contentQueue.Push(c); 
    }
}

#endif
