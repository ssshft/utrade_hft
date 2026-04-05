#ifndef _WRITE_FILE_CONTENT_H
#define _WRITE_FILE_CONTENT_H

#include "Utility.h"

class WriteFileContent {
public:
    static WriteFileContent& GetInstance() {
        static WriteFileContent writeFileContent;
	    return writeFileContent;
    }

    ~WriteFileContent() {}

    void WriteQuantOrderToFile(const string& s) {
        string dateStr = CovertToUtcDate(GetCurrentTimeUs());
        string quantOrderPath = dateStr + "_quantOrder.csv";
    
	    ofstream f;
	    if (access(quantOrderPath.c_str(), F_OK) != 0) {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << "strategyName,strategyOrderId,systemOrderId,exchangeOrderId,instrumentKey,"
		    "orderType,direction,orderStatus,orderTimeStatus,"
            "targetPrice,price,volume,totalPriceOnOrder,totalVolumeOnOrder,tradeVolume,"
            "activeBidPrice1,activeBidVolume1,activeAskPrice1,activeAskVolume1,"
            "passiveBidPrice1,passiveBidVolume1,passiveAsk1Price1,passiveAskVolume1,"
		    "totalShortFee,totalLongFee,"
		    "orderTime,updateTime,killTime,"
            "errorId,originErrorMsg,reduceOnly," 
            "pairId,algoPairId,isActiveOrder,rebalance,"
            << "\n";
		    f << s << "\n";
	    } else {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << s << "\n";
	    }

	    f.close();
    }

    void WritePairOrderToFile(const string& s) {
        string dateStr = CovertToUtcDate(GetCurrentTimeUs());
        string quantOrderPath = dateStr + "_pairOrder.csv";

        ofstream f;
        if (access(quantOrderPath.c_str(), F_OK) != 0) {
            f.open(quantOrderPath.c_str(), ios::app);
            f << "pairId,algoPairId,strategyName,baseAsset,tradingTypeOrder,tradingTypeOffset,targetVolume,"
                "activeInstrumentKey,activeDirection,activeTargetPrice,activeBidPrice1,activeBidVolume1,activeAskPrice1,activeAskVolume1,"
                "passiveInstrumentKey,passiveDirection,passiveTargetPrice,passiveBidPrice1,passiveBidVolume1,passiveAsk1Price1,passiveAskVolume1,"
                "spreadBidAsk,spreadBidBid,spreadAskBid,spreadAskAsk,generateTs,activeDepthTs,passiveDepthTs,"
                "activeTotalPriceOnOrder,activeTotalVolumeOnOrder,passiveTotalPriceOnOrder,passiveTotalVolumeOnOrder,"
                "pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,"
                "activeFrozenPrice,activeFrozenVolume,passiveFrozenPrice,passiveFrozenVolume,"
                "activeAccountId,passiveAccountId,status,rebalanceFlag,updateTime,createTime,pairTargetSpread" << "\n";
            f << s << "\n";
        } else {
            f.open(quantOrderPath.c_str(), ios::app);
            f << s << "\n";
        }

        f.close();
    }

    void WriteAlgoPairOrderToFile(const string& s) {
        string dateStr = CovertToUtcDate(GetCurrentTimeUs());
        string quantOrderPath = dateStr + "_algoPairOrder.csv";
	    ofstream f;
	    if (access(quantOrderPath.c_str(), F_OK) != 0) {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << "algoType,algoStrategyName,algoOrderId,pairInstrumentKey,baseAsset,algoOrderStatus,"
                "activeInstrumentKey,activePriceTakerPct,activePriceMakerPct,activeAccountId,activeDriveType,activeDepthMakerCheck,activeDepthTakerCheck,activeDepthMakerCheckType,activeDepthTakerCheckType,activeOrderType,"
                "passiveInstrumentKey,passivePriceTakerPct,passivePriceMakerPct,passiveAccountId,passiveDriveType,passiveDepthMakerCheck,passiveDepthTakerCheck,passiveDepthMakerCheckType,passiveDepthTakerCheckType,passiveOrderType,"
                "passiveVolumePct,activeMakerCancelOrderTime,activeTakerCancelOrderTime,passiveMakerCancelOrderTime,passiveTakerCancelOrderTime,activePassiveCancelOrderPct,activeMakerCancelOrderPct,activeTakerCancelOrderPct,passiveMakerCancelOrderPct,passiveTakerCancelOrderPct,"
                "activeMakerFeeRate,activeTakerFeeRate,passiveMakerFeeRate,passiveTakerFeeRate,activeTakerSlippage,activeMakerSlippage,passiveTakerSlippage,passiveMakerSlippage,"
                "pairActiveTotalPrice,pairTotalVolume,pairPassiveTotalPrice,"
                "makerTakerFs,takerTakerFs,maxMTOrderSize,maxTTOrderSize,"
                "targetSpreadType,activeVolumeCalcualteType,ttTargetVolume,mtTargetVolume" << "\n";
            f << s << "\n";
	    } else {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << s << "\n";
	    }

	    f.close();
    }

    void WriteAlgoFishingOrderToFile(const string& s) {
        string dateStr = CovertToUtcDate(GetCurrentTimeUs());
        string quantOrderPath = dateStr + "_alogFishingOrder.csv";
	    ofstream f;
	    if (access(quantOrderPath.c_str(), F_OK) != 0) {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << "algoType,algoStrategyName,algoOrderId,pairInstrumentKey,baseAsset,algoOrderStatus,"
                "activeInstrumentKey,activePriceTakerPct,activePriceMakerPct,activeAccountId,activeDriveType,activeDepthMakerCheck,activeDepthTakerCheck,activeDepthMakerCheckType,activeDepthTakerCheckType,activeOrderType,"
                "passiveInstrumentKey,passivePriceTakerPct,passivePriceMakerPct,passiveAccountId,passiveDriveType,passiveDepthMakerCheck,passiveDepthTakerCheck,passiveDepthMakerCheckType,passiveDepthTakerCheckType,passiveOrderType,"
                "passiveVolumePct,activeMakerCancelOrderTime,activeTakerCancelOrderTime,passiveMakerCancelOrderTime,passiveTakerCancelOrderTime,activePassiveCancelOrderPct,activeMakerCancelOrderPct,activeTakerCancelOrderPct,passiveMakerCancelOrderPct,passiveTakerCancelOrderPct,"
                "activeMakerFeeRate,activeTakerFeeRate,passiveMakerFeeRate,passiveTakerFeeRate,activeTakerSlippage,activeMakerSlippage,passiveTakerSlippage,passiveMakerSlippage,"
                "pairActiveTotalPrice,pairTotalVolume,pairPassiveTotalPrice,"
                "makerTakerFs,takerTakerFs,maxMTOrderSize,maxTTOrderSize,"
                "targetSpreadType,activeVolumeCalcualteType,ttTargetVolume,mtTargetVolume,fishingSlippagePct" << "\n";
            f << s << "\n";
	    } else {
		    f.open(quantOrderPath.c_str(), ios::app);
		    f << s << "\n";
	    }

	    f.close();
    }

    void WriteAlgoRebalanceOrderToFile(const string& s) {
        string dateStr = CovertToUtcDate(GetCurrentTimeUs());
        string quantOrderPath = dateStr + "_alogRebalanceOrder.csv";
	    // ofstream f;
	    // if (access(quantOrderPath.c_str(), F_OK) != 0) {
		//     f.open(quantOrderPath.c_str(), ios::app);
		//     f << "algoType,algoStrategyName,algoOrderId,pairInstrumentKey,baseAsset,algoOrderStatus,"
        //         "activeInstrumentKey,activePriceTakerPct,activePriceMakerPct,activeAccountId,activeDriveType,activeDepthMakerCheck,activeDepthTakerCheck,activeDepthMakerCheckType,activeDepthTakerCheckType,activeOrderType,"
        //         "passiveInstrumentKey,passivePriceTakerPct,passivePriceMakerPct,passiveAccountId,passiveDriveType,passiveDepthMakerCheck,passiveDepthTakerCheck,passiveDepthMakerCheckType,passiveDepthTakerCheckType,passiveOrderType,"
        //         "passiveVolumePct,activeMakerCancelOrderTime,activeTakerCancelOrderTime,passiveMakerCancelOrderTime,passiveTakerCancelOrderTime,activePassiveCancelOrderPct,activeMakerCancelOrderPct,activeTakerCancelOrderPct,passiveMakerCancelOrderPct,passiveTakerCancelOrderPct,"
        //         "activeMakerFeeRate,activeTakerFeeRate,passiveMakerFeeRate,passiveTakerFeeRate,activeTakerSlippage,activeMakerSlippage,passiveTakerSlippage,passiveMakerSlippage,"
        //         "pairActiveTotalPrice,pairTotalVolume,pairPassiveTotalPrice,"
        //         "makerTakerFs,takerTakerFs,maxMTOrderSize,maxTTOrderSize,"
        //         "targetSpreadType,activeVolumeCalcualteType,ttTargetVolume,mtTargetVolume,fishingSlippagePct" << "\n";
        //     f << s << "\n";
	    // } else {
		//     f.open(quantOrderPath.c_str(), ios::app);
		//     f << s << "\n";
	    // }

	    // f.close();
    }


    void WriteFile(const content& c) {
        if (c.type == 1) {  // quantOrder
            WriteQuantOrderToFile(c.msg);
        } else if (c.type == 2) {  // pairOrder
            WritePairOrderToFile(c.msg);
        } else if (c.type == 3) {  // algoPairOrder
            WriteAlgoPairOrderToFile(c.msg);
        } else if (c.type == 4) {  // algoFishingOrder
            WriteAlgoFishingOrderToFile(c.msg);
        } else if (c.type == 5) {  // algoFishingOrder
            WriteAlgoRebalanceOrderToFile(c.msg);
        }
    }

    void Run() {
        while (running) {
            try {
			    content c;
			    if (contentQueue.Pop(c)) {
				    WriteFile(c);
			    }
            } catch(exception& e) {
            }
            usleep(10);
        }
    }

private:

    WriteFileContent() {
        running = true;
        runningThread = new thread(&WriteFileContent::Run, this);
    }

    bool running;
    thread* runningThread;
};

#endif
