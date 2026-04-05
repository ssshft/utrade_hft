#include "AlgoPairOrder.h"
#include "SpreadManager.h"
#include "AccountManager.h"
#include "QuantTrade.h"
#include "BasicInfoMgr.h"
#include "Convert.h"


AlgoPairOrder::AlgoPairOrder() : BaseAlgoOrder() {

}

PairOrder AlgoPairOrder::GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId) {
    PairOrder pairOrder;
    if (fundVerifyFailedFlag && (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::OPEN_LONG)){
        return pairOrder;
    }

    // check para
    if (!passInfoParameterCheck()) {
        LOG_ERROR("{} contract info parameter error, please check!", pairInstrumentKey);
        return pairOrder;
    }

    bool zeroPriceTickFlag = false;
    double tempOrgSpread = 0;
    if (activePriceTickFlag){
        // 如果主动腿报单有tick调整, 则初始化一个在一档挂单的pairOrder参数
        zeroPriceTickFlag = false;
        tempOrgSpread = 0;
    }
    double expectActiveVolume = GetExpectActiveVolume();
    double lockedSpread = GetLockedSpread();
    stra::QuantSpread spread = SpreadManager::Instance().GetLastSpread(pairInstrumentKey);
    double tempSpread;
    double tempVolume;
    double startSpread;
    double endSpread;
    double startVolume;
    double endVolume;
    bool swch;
    // 判断价格波动是否在接受范围内
    if ((ttPriceTrendProtectFlag && tradingTypeOrder == stra::TAKER_TAKER) || (mtPriceTrendProtectFlag && tradingTypeOrder == stra::MAKER_TAKER)) {
        double midPrice = (spread.activeAskPrice1 + spread.activeBidPrice1) / 2;
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            // activeDirection = L
            //if (midPrice / spread.activePriceTema - 1 < -0.005) {
            if (midPrice / spread.activePriceTema - 1 < -0.003) {
                LOG_INFO("midPrice / spread.activePriceTema - 1 < -0.003  midPrice:%f  activePriceTema:%f", midPrice, spread.activePriceTema);
                return pairOrder;
            }
        } else if(tradingTypeOffset == stra::OPEN_LONG || tradingTypeOffset == stra::CLOSE_SHORT){
            // activeDirection = S
            //if (midPrice / spread.activePriceTema - 1 > 0.005) {
            if (midPrice / spread.activePriceTema - 1 > 0.003) {
                LOG_INFO("midPrice / spread.activePriceTema - 1 > 0.003  midPrice:%f  activePriceTema:%f", midPrice, spread.activePriceTema);
                return pairOrder;
            }
        }
    }

    // ask --- bid
    if ((spread.passiveAskPrice1 / spread.passiveBidPrice1 - 1 > 0.0015) && (spread.passiveAskPrice1 - spread.passiveBidPrice1 > 2.5 * passiveInfo.tickSize)) {
        LOG_INFO("ask --- bid too big! instrumentKey:{} passiveAskPrice1:{} passiveBidPrice1:{}", spread.pairInstrumentKey, spread.passiveAskPrice1, spread.passiveBidPrice1);
        return pairOrder;
    }

    // filter passive amount < active amount
    if (tradingTypeOrder == stra::MAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_SHORT) {
            double activeBidAmount1 = 0.0;
            if (activeInfo.calculateType == 0) {
                activeBidAmount1 = spread.activeBidVolume1 * spread.activeBidPrice1 * activeInfo.multiple;
            }
            else if (activeInfo.calculateType == 1) {
                activeBidAmount1 = spread.activeBidVolume1 * activeInfo.multiple;
            }

            double passiveBidAmount1 = 0.0;
            if (passiveInfo.calculateType == 0) {
                passiveBidAmount1 = spread.passiveBidVolume1 * spread.passiveBidPrice1 * passiveInfo.multiple;
            }
            else if (passiveInfo.calculateType == 1) {
                passiveBidAmount1 = spread.passiveBidVolume1 * passiveInfo.multiple;
            }

            if (activeBidAmount1 >= passiveBidAmount1) {
                LOG_INFO("spread.activeBidAmount1 >= spread.passiveBidAmount1 instrumentKey:{} tradingTypeOffset:{} activeBidAmount1:{} passiveBidAmount1:{}", spread.pairInstrumentKey, stra::TradingTypeEnum2Str[tradingTypeOffset], activeBidAmount1, passiveBidAmount1);
                return pairOrder;
            }
        }
        else if (tradingTypeOffset == stra::OPEN_LONG) {
            double activeAskAmount1 = 0.0;
            if (activeInfo.calculateType == 0) {
                activeAskAmount1 = spread.activeAskVolume1 * spread.activeAskPrice1 * activeInfo.multiple;
            }
            else if (activeInfo.calculateType == 1) {
                activeAskAmount1 = spread.activeAskVolume1 * activeInfo.multiple;
            }

            double passiveAskAmount1 = 0.0;
            if (passiveInfo.calculateType == 0) {
                passiveAskAmount1 = spread.passiveAskVolume1 * spread.passiveAskPrice1 * passiveInfo.multiple;
            }
            else if (passiveInfo.calculateType == 1) {
                passiveAskAmount1 = spread.passiveAskVolume1 * passiveInfo.multiple;
            }
            
            if (activeAskAmount1 >= passiveAskAmount1) {
                LOG_INFO("spread.activeAskAmount1 >= spread.passiveAskAmount1 instrumentKey:{} tradingTypeOffset:{} activeAskAmount1:{} passiveAskAmount1:{}", spread.pairInstrumentKey, stra::TradingTypeEnum2Str[tradingTypeOffset], activeAskAmount1, passiveAskAmount1);
                return pairOrder;
            }
        }
    }


    if (tradingTypeOrder == stra::TAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_SHORT) {
            startSpread = ttOSStartSpread;
            endSpread = ttOSEndSpread;
            startVolume = ttOSStartVolume;
            endVolume = ttOSEndVolume;
            swch = ttOSSwitch;
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            startSpread = ttCSStartSpread;
            endSpread = ttCSEndSpread;
            startVolume = ttCSStartVolume;
            endVolume = ttCSEndVolume;
            swch = ttCSSwitch;
        } else if (tradingTypeOffset == stra::OPEN_LONG) {
            startSpread = ttOLStartSpread;
            endSpread = ttOLEndSpread;
            startVolume = ttOLStartVolume;
            endVolume = ttOLEndVolume;
            swch = ttOLSwitch;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            startSpread = ttCLStartSpread;
            endSpread = ttCLEndSpread;
            startVolume = ttCLStartVolume;
            endVolume = ttCLEndVolume;
            swch = ttCLSwitch;
        }
    } else if (tradingTypeOrder == stra::MAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_SHORT) {
            startSpread = mtOSStartSpread;
            endSpread = mtOSEndSpread;
            startVolume = mtOSStartVolume;
            endVolume = mtOSEndVolume;
            swch = mtOSSwitch;
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            startSpread = mtCSStartSpread;
            endSpread = mtCSEndSpread;
            startVolume = mtCSStartVolume;
            endVolume = mtCSEndVolume;
            swch = mtCSSwitch;
        } else if (tradingTypeOffset == stra::OPEN_LONG) {
            startSpread = mtOLStartSpread;
            endSpread = mtOLEndSpread;
            startVolume = mtOLStartVolume;
            endVolume = mtOLEndVolume;
            swch = mtOLSwitch;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            startSpread = mtCLStartSpread;
            endSpread = mtCLEndSpread;
            startVolume = mtCLStartVolume;
            endVolume = mtCLEndVolume;
            swch = mtCLSwitch;
        }
    }
    if (!swch){
        // 如果开关关闭则返回空配对单
         LOG_INFO("swch is false! tradingTypeOrder:%s tradingTypeOffset:%s startSpread:%f endSpread:%f startVolume:%f endVolume:%f", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), startSpread, endSpread, startVolume, endVolume);
        return pairOrder;
    }
    if (tradingTypeOrder == stra::TAKER_TAKER) {
        double tradingFs = takerTakerFs;
        double tempSpreadAdj = 0;
        if (activePriceTickFlag) {
            tempSpreadAdj = max(activePriceTickNum, 0) * activeInfo.tickSize * 2 / (spread.activeBidPrice1 + spread.activeAskPrice1);
        }
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadAskBid - tradingFs - tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = spread.spreadAskBid - tradingFs;
                }
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(spread.spreadAskBid, spread.spreadAskBidTema) - tradingFs - tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = min(spread.spreadAskBid, spread.spreadAskBidTema) - tradingFs;
                }
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveBidVolume1, spread.passiveBidPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }

        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadBidAsk + tradingFs + tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = spread.spreadBidAsk + tradingFs;
                }
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(spread.spreadBidAsk, spread.spreadBidAskTema) + tradingFs + tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = max(spread.spreadBidAsk, spread.spreadBidAskTema) + tradingFs;
                }
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveAskVolume1, spread.passiveAskPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        }
    } else if (tradingTypeOrder == stra::MAKER_TAKER) {
        double tradingFs = makerTakerFs;
        double tempSpreadAdj = 0;
        // 为了在maker报单向前挂时不损失价差
        if (activePriceTickFlag){
            double tempTickNum = max(min((spread.activeAskPrice1 - spread.activeBidPrice1) / activeInfo.tickSize - 1, (double)activePriceTickNum), 0.0);
            tempSpreadAdj = tempTickNum * activeInfo.tickSize * 2 / (spread.activeBidPrice1 + spread.activeAskPrice1);
        }
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadBidBid - tradingFs - tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = spread.spreadBidBid - tradingFs;
                }
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(spread.spreadBidBid, spread.spreadBidBidTema) - tradingFs - tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = min(spread.spreadBidBid, spread.spreadBidBidTema) - tradingFs;
                }
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveBidVolume1, spread.passiveBidPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadAskAsk + tradingFs + tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = spread.spreadAskAsk + tradingFs;
                }
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(spread.spreadAskAsk, spread.spreadAskAskTema) + tradingFs + tempSpreadAdj;
                if (activePriceTickFlag){
                    tempOrgSpread = max(spread.spreadAskAsk, spread.spreadAskAskTema) + tradingFs;
                }
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveAskVolume1, spread.passiveAskPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        }
    }

        if (activeInfo.calculateType == 0) {
            double midPrice = (spread.activeBidPrice1 + spread.activeAskPrice1) / 2;
            if (midPrice > 0 && activeInfo.multiple > 0) {
                // double volume = 100 / midPrice / activeInfo.multiple;
                double volume = minOrderAmount / midPrice / activeInfo.multiple;
                tempVolume = max(tempVolume, volume);
            }
        } else if (activeInfo.calculateType == 1) {
            if (activeInfo.multiple > 0) {
                // double volume = 100 / activeInfo.multiple;
                double volume = minOrderAmount / activeInfo.multiple;
                tempVolume = max(tempVolume, volume);
            }
        }


    LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s tempSpread:%f startSpread:%f endSpread:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), tempSpread, startSpread, endSpread, activeInstrumentKey, passiveInstrumentKey);


    double targetActiveVolume = 0;
    // order 相关
    stra::Direction activeDirection;
    stra::Direction passiveDirection;
    double targetVolume;
    double activeTargetPrice;
    double passiveTargetPrice;
    stra::OrderType acOrderType;
    stra::OrderType paOrderType;
    if (tradingTypeOffset == stra::OPEN_SHORT) {
        if (tempSpread < startSpread) {
            targetActiveVolume = 0;
        } else if (tempSpread > endSpread) {
            targetActiveVolume = endVolume;
        } else {
            targetActiveVolume = startVolume + (endVolume - startVolume) * (tempSpread - startSpread) / (endSpread - startSpread);
        }
        if (activePriceTickFlag){
            if (targetActiveVolume < expectActiveVolume && tempOrgSpread > tempSpread){
                zeroPriceTickFlag = true;
                if (tempOrgSpread < startSpread) {
                    targetActiveVolume = 0;
                } else if (tempOrgSpread > endSpread) {
                    targetActiveVolume = endVolume;
                } else {
                    targetActiveVolume = startVolume + (endVolume - startVolume) * (tempOrgSpread - startSpread) / (endSpread - startSpread);
                }
            }
        }
        if (targetActiveVolume > expectActiveVolume && targetActiveVolume > 0) {
            activeDirection = stra::Direction_LONG;
            passiveDirection = stra::Direction_SHORT;
            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(targetActiveVolume - expectActiveVolume, ttTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeAskPrice1;
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType;
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(targetActiveVolume - expectActiveVolume, mtTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeBidPrice1;
                passiveTargetPrice = spread.passiveBidPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType;
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s targetActiveVolume <= expectActiveVolume or targetActiveVolume <= 0. targetActiveVolume:%f expectActiveVolume:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), targetActiveVolume, expectActiveVolume, activeInstrumentKey, passiveInstrumentKey);
            return pairOrder;
        }

    } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
        if (tempSpread > startSpread) {
            targetActiveVolume = startVolume;
        } else if (tempSpread < endSpread) {
            targetActiveVolume = endVolume;
        } else {
            targetActiveVolume = startVolume + (endVolume - startVolume) * (tempSpread - startSpread) / (endSpread - startSpread);
        } 

        if (profitSwitch) {
            double profitRate = lockedSpread - tempSpread;
            if (profitRate < profitPct) {
                targetActiveVolume = max(startVolume, targetActiveVolume);
            }
        }

        if (activePriceTickFlag){
            if (targetActiveVolume > expectActiveVolume && tempOrgSpread < tempSpread){
                zeroPriceTickFlag = true;
                if (tempOrgSpread > startSpread) {
                    targetActiveVolume = startVolume;
                } else if (tempOrgSpread < endSpread) {
                    targetActiveVolume = endVolume;
                } else {
                    targetActiveVolume = startVolume + (endVolume - startVolume) * (tempOrgSpread - startSpread) / (endSpread - startSpread);
                }
                if (profitSwitch) {
                    double profitRate = lockedSpread - tempOrgSpread;
                    if (profitRate < profitPct) {
                        targetActiveVolume = max(startVolume, targetActiveVolume);
                    }
                }
            }
        }

        if (targetActiveVolume < expectActiveVolume && tempSpread <= startSpread ) {
            activeDirection = stra::Direction_SHORT;
            passiveDirection = stra::Direction_LONG;

            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, ttTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeBidPrice1;
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType; 
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, mtTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeAskPrice1;
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s targetActiveVolume <= expectActiveVolume. targetActiveVolume:%f expectActiveVolume:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), targetActiveVolume, expectActiveVolume, activeInstrumentKey, passiveInstrumentKey);
            return pairOrder;
        }

    } else if (tradingTypeOffset == stra::OPEN_LONG) {
        if (tempSpread > startSpread) {
            targetActiveVolume = 0;
        } else if (tempSpread < endSpread) {
            targetActiveVolume = endVolume;
        } else {
            targetActiveVolume = startVolume + (endVolume - startVolume) * (tempSpread - startSpread) / (endSpread - startSpread);
        }
        if (activePriceTickFlag){
            if (targetActiveVolume > expectActiveVolume && tempOrgSpread < tempSpread){
                zeroPriceTickFlag = true;
                if (tempOrgSpread > startSpread) {
                    targetActiveVolume = 0;
                } else if (tempOrgSpread < endSpread) {
                    targetActiveVolume = endVolume;
                } else {
                    targetActiveVolume = startVolume + (endVolume - startVolume) * (tempOrgSpread - startSpread) / (endSpread - startSpread);
                }
            }
        }
        if (targetActiveVolume < expectActiveVolume && targetActiveVolume < 0) {
            activeDirection = stra::Direction_SHORT;
            passiveDirection = stra::Direction_LONG;

            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, ttTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeBidPrice1;
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType; 
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, mtTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeAskPrice1;
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
             LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s targetActiveVolume <= expectActiveVolume or targetActiveVolume <= 0. targetActiveVolume:%f expectActiveVolume:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), targetActiveVolume, expectActiveVolume, activeInstrumentKey, passiveInstrumentKey);
            return pairOrder;
        }

    } else if (tradingTypeOffset == stra::CLOSE_LONG) {
        if (tempSpread < startSpread) {
            targetActiveVolume = startVolume;
        } else if (tempSpread > endSpread) {
            targetActiveVolume = endVolume;
        } else {
            targetActiveVolume = startVolume + (endVolume - startVolume) * (tempSpread - startSpread) / (endSpread - startSpread);
        }

        if (profitSwitch) {
            double profitRate = tempSpread - lockedSpread;
            if (profitRate < profitPct) {
                targetActiveVolume = min(startVolume, targetActiveVolume);
            }
        }
        if (activePriceTickFlag){
            if (targetActiveVolume < expectActiveVolume && tempOrgSpread > tempSpread){
                zeroPriceTickFlag = true;
                if (tempOrgSpread < startSpread) {
                    targetActiveVolume = startVolume;
                } else if (tempOrgSpread > endSpread) {
                    targetActiveVolume = endVolume;
                } else {
                    targetActiveVolume = startVolume + (endVolume - startVolume) * (tempOrgSpread - startSpread) / (endSpread - startSpread);
                }
                if (profitSwitch) {
                    double profitRate = tempOrgSpread - lockedSpread;
                    if (profitRate < profitPct) {
                        targetActiveVolume = min(startVolume, targetActiveVolume);
                    }
                }
            }
        }
        if (targetActiveVolume > expectActiveVolume && tempSpread >= startSpread) {
            activeDirection = stra::Direction_LONG;
            passiveDirection = stra::Direction_SHORT;

            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(targetActiveVolume - expectActiveVolume, ttTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeAskPrice1;
                passiveTargetPrice = spread.passiveBidPrice1;
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType; 
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(targetActiveVolume - expectActiveVolume, mtTargetVolume);
                if (tempVolume > stra::MIN_FLOAT){
                    targetVolume = min(tempVolume, targetVolume);
                }
                activeTargetPrice = spread.activeBidPrice1;
                passiveTargetPrice = spread.passiveBidPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s targetActiveVolume <= expectActiveVolume. targetActiveVolume:%f expectActiveVolume:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), targetActiveVolume, expectActiveVolume, activeInstrumentKey, passiveInstrumentKey);
            return pairOrder;
        }
    }

    targetVolume = round(targetVolume / activeInfo.lotSize) * activeInfo.lotSize;
    if (targetVolume < stra::MIN_FLOAT) {
        return pairOrder;
    }
    
    pairOrder.activeBidPrice1 = spread.activeBidPrice1;
    pairOrder.activeBidVolume1 = spread.activeBidVolume1;
    pairOrder.activeAskPrice1 = spread.activeAskPrice1;
    pairOrder.activeAskVolume1 = spread.activeAskVolume1;
    pairOrder.passiveBidPrice1 = spread.passiveBidPrice1;
    pairOrder.passiveBidVolume1 = spread.passiveBidVolume1;
    pairOrder.passiveAskPrice1 = spread.passiveAskPrice1;
    pairOrder.passiveAskVolume1 = spread.passiveAskVolume1;

    pairOrder.pairId = pairOrderId;
    strncpy(pairOrder.baseAsset, baseAsset, stra::ASSET_LEN);
    pairOrder.targetVolume = targetVolume;
    strncpy(pairOrder.activeInstrumentKey, activeInstrumentKey, stra::INST_KEY_LEN);
    pairOrder.activeDirection = activeDirection;
    pairOrder.activeOrderType = acOrderType;
    // pairOrder.activePriceType = activePriceType;
    if (pairOrder.activeOrderType == stra::OrderType_POST_ONLY){
        pairOrder.activePricePct = activePriceMakerPct;
    } else{
        pairOrder.activePricePct = activePriceTakerPct;
    }

    pairOrder.activeAccountId = activeAccountId;
    pairOrder.activeTargetPrice = activeTargetPrice;

    strncpy(pairOrder.passiveInstrumentKey, passiveInstrumentKey, stra::INST_KEY_LEN);
    pairOrder.passiveDirection = passiveDirection;
    pairOrder.passiveOrderType = paOrderType;
    // pairOrder.passivePriceType = passivePriceType;
    if (pairOrder.passiveOrderType == stra::OrderType_POST_ONLY){
        pairOrder.passivePricePct = passivePriceMakerPct;
    } else{
        pairOrder.passivePricePct = passivePriceTakerPct;
    }

    pairOrder.passiveAccountId = passiveAccountId;
    pairOrder.passiveTargetPrice = passiveTargetPrice;

    pairOrder.pairTotalVolume = pairTotalVolume;
    pairOrder.pairActiveTotalPrice = pairActiveTotalPrice;
    pairOrder.pairPassiveTotalPrice = pairPassiveTotalPrice;

    pairOrder.algoPairId = algoOrderId;
    // pairOrder.pairOrderType = tradingTypeOffset;
    pairOrder.tradingTypeOrder = tradingTypeOrder;
    pairOrder.tradingTypeOffset = tradingTypeOffset;
    strncpy(pairOrder.strategyName, algoStrategyName, stra::NAME_LEN);

    pairOrder.activePriceTickFlag = activePriceTickFlag;
    if (activePriceTickFlag){
        if (zeroPriceTickFlag){
            // 这个时候超价报单不满足价差条件，只能在0档报单，pairOrder自动在0档报单
            pairOrder.activePriceTickNum = 0;
        }else{
            pairOrder.activePriceTickNum = activePriceTickNum;
        }
    }else{
        pairOrder.activePriceTickNum = activePriceTickNum;
    }
    pairOrder.passivePriceTickFlag = passivePriceTickFlag;
    pairOrder.passivePriceTickNum = passivePriceTickNum;
    if (tradingTypeOrder == stra::MAKER_TAKER){
        pairOrder.rebalanceFlag = mtRebalanceFlag;
    }else{
        pairOrder.rebalanceFlag = ttRebalanceFlag;
    }
    pairOrder.createTime = GetCurrentTimeUs();
    pairOrder.updateTime = GetCurrentTimeUs();

    if (tradingTypeOrder == stra::TAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_LONG) {
            pairOrder.pairTargetSpread = ttOLStartSpread;
            pairOrder.pairTargetSpreadProfit = ttCLStartSpread - ttOLStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            pairOrder.pairTargetSpread = ttCSStartSpread;
            pairOrder.pairTargetSpreadProfit = ttOSStartSpread - ttCSStartSpread;
        } else if (tradingTypeOffset == stra::OPEN_SHORT) {
            pairOrder.pairTargetSpread = ttOSStartSpread;
            pairOrder.pairTargetSpreadProfit = ttOSStartSpread - ttCSStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            pairOrder.pairTargetSpread = ttCLStartSpread;
            pairOrder.pairTargetSpreadProfit = ttCLStartSpread - ttOLStartSpread;
        }
    }
    else if (tradingTypeOrder == stra::MAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_LONG) {
            pairOrder.pairTargetSpread = mtOLStartSpread;
            pairOrder.pairTargetSpreadProfit = mtCLStartSpread - mtOLStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            pairOrder.pairTargetSpread = mtCSStartSpread;
            pairOrder.pairTargetSpreadProfit = mtOSStartSpread - mtCSStartSpread;
        } else if (tradingTypeOffset == stra::OPEN_SHORT) {
            pairOrder.pairTargetSpread = mtOSStartSpread;
            pairOrder.pairTargetSpreadProfit = mtOSStartSpread - mtCSStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            pairOrder.pairTargetSpread = mtCLStartSpread;
            pairOrder.pairTargetSpreadProfit = mtCLStartSpread - mtOLStartSpread;
        }
    }

    pairOrder.Init();
    strncpy(pairOrder.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);

    return pairOrder;
}

PairOrder AlgoPairOrder::CreatePairOrder(stra::TradingType tradingType) {
    PairOrder pairOrder;
    int64_t pairOrderId = GenerateStrategyPairId();
    double expectVolume = GetExpectActiveVolume();
    //LOG_INFO("tradingType:%s  pairOrderId:%ld  expectVolume:%f  ttTargetVolume:%f  mtTargetVolume:%f", stra::TradingTypeEnum2Str[tradingType].c_str(), pairOrderId, expectVolume, ttTargetVolume, mtTargetVolume);
    if (tradingType == stra::TAKER_TAKER) {
        if (expectVolume >= minVolume - stra::MIN_FLOAT) {
            if (ttOSSwitch && ttCSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                if (pairOrder.algoPairId <= stra::MIN_FLOAT) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                }
            } else if (ttOSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
            } else if (ttCSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
            } else {
                LOG_INFI("");
            }
        } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
            if (ttOLSwitch && ttCLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                if (pairOrder.algoPairId <= stra::MIN_FLOAT) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                }
            } else if (ttOLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
            } else if (ttCLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
            } else {
                LOG_INFO("");
            }
        } else {
            if (ttOSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
            }
            if (pairOrder.pairId <= 0) {
                if (ttOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }
            }
        }
    } else if (tradingType == stra::MAKER_TAKER) {
        if (expectVolume >= minVolume - stra::MIN_FLOAT) {
            if (mtOSSwitch && mtCSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                if (pairOrder.algoPairId <= stra::MIN_FLOAT) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                }
            } else if (mtOSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
            } else if (mtCSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
            } else {
                LOG_INFO("");
            }
        } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
            if (mtOLSwitch && mtCLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                if (pairOrder.algoPairId <= stra::MIN_FLOAT) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                }
            } else if (mtOLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
            } else if (mtCLSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
            } else {
                LOG_INFO("");
            }
        } else {
            if (mtOSSwitch) {
                pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
            }
            if (pairOrder.pairId <= 0) {
                if (mtOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }
            }
        }
    }
    return pairOrder;
}

