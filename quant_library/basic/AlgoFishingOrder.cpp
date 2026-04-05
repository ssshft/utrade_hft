#include "SpreadManager.h"
#include "AccountManager.h"
#include "QuantTrade.h"
#include "BasicInfoMgr.h"
#include "Convert.h"
#include "AlgoFishingOrder.h"


AlgoFishingOrder::AlgoFishingOrder() : BaseAlgoOrder() {
    fishingSlippagePct = 0;
}


PairOrder AlgoFishingOrder::GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId) {
    // 这里需要进行改造, 按照持仓量与盈利参数计算目标报价
    PairOrder pairOrder;
    if (fundVerifyFailedFlag && (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::OPEN_LONG)){
        return pairOrder;
    }
    double expectActiveVolume = GetExpectActiveVolume();
    double lockedSpread = GetLockedSpread();
    stra::QuantSpread spread = SpreadManager::Instance().GetLastSpread(pairInstrumentKey);
    double tempTargetVolume = 0.0;
    double tempTargetSpread = 0.0;
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
            if (midPrice / spread.activePriceTema - 1 < -0.003) {
                LOG_INFO("midPrice / spread.activePriceTema - 1 < -0.005  midPrice:%f  activePriceTema:%f", midPrice, spread.activePriceTema);
                return pairOrder;
            }
        } else if(tradingTypeOffset == stra::OPEN_LONG || tradingTypeOffset == stra::CLOSE_SHORT){
            // activeDirection = S
            if (midPrice / spread.activePriceTema - 1 > 0.003) {
                LOG_INFO("midPrice / spread.activePriceTema - 1 > 0.005  midPrice:%f  activePriceTema:%f", midPrice, spread.activePriceTema);
                return pairOrder;
            }
        }
    }

    if (spread.activeAskPrice1 / spread.activeBidPrice1 - 1 > 0.001) {
        LOG_INFO("ask -- bid too big! instrumentKey:{} activeAskPrice1:{} activeBidPrice1:{}", spread.pairInstrumentKey, spread.activeAskPrice1, spread.activeBidPrice1);
        return pairOrder;
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
    // 从这里开始改造钓鱼单报价 ***********************

    if (tradingTypeOrder == stra::TAKER_TAKER) {
        double tradingFs = takerTakerFs;
        double tempSpreadAdj = 0;
        if (activePriceTickFlag == true) {
            tempSpreadAdj = max(activePriceTickNum, 0) * activeInfo.tickSize * 2 / (spread.activeBidPrice1 + spread.activeAskPrice1);
        }
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadAskBid - tradingFs - tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(spread.spreadAskBid, spread.spreadAskBidTema) - tradingFs - tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveBidVolume1, spread.passiveBidPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }

        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadBidAsk + tradingFs + tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(spread.spreadBidAsk, spread.spreadBidAskTema) + tradingFs + tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveAskVolume1, spread.passiveAskPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        }
        tempTargetVolume = ttTargetVolume;
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
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(spread.spreadBidBid, spread.spreadBidBidTema) - tradingFs - tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveBidVolume1, spread.passiveBidPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = spread.spreadAskAsk + tradingFs + tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(spread.spreadAskAsk, spread.spreadAskAskTema) + tradingFs + tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(spread.passiveAskVolume1, spread.passiveAskPrice1) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        }
        tempTargetVolume = mtTargetVolume;
    }

    //LOG_INFO("GetTargetPairOrder tempSpread:%f   tempVolume:%f   tempTargetVolume:%f", tempSpread, tempVolume, tempTargetVolume);

    // tempSpread 是可以锁定的价差
    // tempVolume 是最大挂单量
    // 挂单价格会在pairOrder创建订单时赋值
    double targetActiveVolume = 0;
    // order 相关
    stra::Direction activeDirection;
    stra::Direction passiveDirection;
    double targetVolume;
    double ajdPct;
    double activeTargetPrice;
    double passiveTargetPrice;
    stra::OrderType acOrderType;
    stra::OrderType paOrderType;
    if (tradingTypeOffset == stra::OPEN_SHORT) {
        // algoPairOrder.mtOSStartSpread = 0.0002 + meanShift;
        // algoPairOrder.mtOSEndSpread = 0.00021 + meanShift;
        // algoPairOrder.mtOSStartVolume = 0;
        // algoPairOrder.mtOSEndVolume = 100;
        // algoPairOrder.mtOSSwitch = true;
        activeDirection = stra::Direction_LONG;
        passiveDirection = stra::Direction_SHORT;
        // 买主动腿卖被动腿, 主动腿够便宜才做
        // 价差越高越开
        if (endVolume - pairTotalVolume > stra::MIN_FLOAT)
        {
            // tempSpread 是使用买1卖1价格考虑了滑点和手续费后目标锁定的价差, 如果报钓鱼单则应该向更远的方向报单
            tempTargetSpread = startSpread + (endSpread - startSpread) * max(min((pairTotalVolume - startVolume) / (endVolume - startVolume), 1.0), 0.0);
            // 需要调整的百分比
            ajdPct = (tempTargetSpread - tempSpread) / fishingSlippagePct;
            // 具备开仓条件则选择适合的价格开仓
            targetVolume = tempTargetVolume;
            targetVolume = min(endVolume - pairTotalVolume, targetVolume);
            if (tempVolume > stra::MIN_FLOAT){
                targetVolume = min(tempVolume, targetVolume);
            }
            if (tradingTypeOrder == stra::TAKER_TAKER) {
                if (ajdPct <= 0){
                    activeTargetPrice = spread.activeAskPrice1 * (1 - ajdPct);
                    passiveTargetPrice = spread.passiveBidPrice1;
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = spread.activeBidPrice1 * (1 - max(ajdPct, 0.0));
                passiveTargetPrice = spread.passiveBidPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType;
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }

        }

    } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
        // algoPairOrder.mtCSStartSpread = -0.0002 + meanShift + closeShift;
        // algoPairOrder.mtCSEndSpread = -0.00021 + meanShift + closeShift;
        // algoPairOrder.mtCSStartVolume = 100;
        // algoPairOrder.mtCSEndVolume = 0;
        // algoPairOrder.mtCSSwitch = true;
        activeDirection = stra::Direction_SHORT;
        passiveDirection = stra::Direction_LONG;
        // 价差越低越平
        if (endVolume - pairTotalVolume < -stra::MIN_FLOAT) {
            // tempSpread 是使用买1卖1价格考虑了滑点和手续费后目标锁定的价差, 如果报钓鱼单则应该向更远的方向报单
            tempTargetSpread = startSpread + (endSpread - startSpread) * max(min((startVolume - pairTotalVolume) / (startVolume - endVolume), 1.0), 0.0);
            // 需要调整的百分比
            ajdPct = (tempTargetSpread - tempSpread) / fishingSlippagePct;
            // 具备开仓条件则选择适合的价格开仓
            targetVolume = tempTargetVolume;
            targetVolume = min(pairTotalVolume - endVolume, targetVolume);
            if (tempVolume > stra::MIN_FLOAT){
                targetVolume = min(tempVolume, targetVolume);
            }
            if (tradingTypeOrder == stra::TAKER_TAKER) {
                if (ajdPct >= 0){
                    activeTargetPrice = spread.activeBidPrice1 * (1 - ajdPct);
                    passiveTargetPrice = spread.passiveAskPrice1;
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = spread.activeAskPrice1 * (1 - min(ajdPct, 0.0));
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("targetActiveVolume <= expectActiveVolume. targetActiveVolume:%f expectActiveVolume:%f", targetActiveVolume, expectActiveVolume);
            return pairOrder;
        }

    } else if (tradingTypeOffset == stra::OPEN_LONG) {
        // algoPairOrder.mtOLStartSpread = -0.0002 + meanShift;
        // algoPairOrder.mtOLEndSpread = -0.00021 + meanShift;
        // algoPairOrder.mtOLStartVolume = 0;
        // algoPairOrder.mtOLEndVolume = -100;
        // algoPairOrder.mtOLSwitch = true;
        activeDirection = stra::Direction_SHORT;
        passiveDirection = stra::Direction_LONG;
        if (endVolume - pairTotalVolume < -stra::MIN_FLOAT) {
            // tempSpread 是使用买1卖1价格考虑了滑点和手续费后目标锁定的价差, 如果报钓鱼单则应该向更远的方向报单
            tempTargetSpread = startSpread + (endSpread - startSpread) * max(min((startVolume - pairTotalVolume) / (startVolume - endVolume), 1.0), 0.0);
            // 需要调整的百分比
            ajdPct = (tempTargetSpread - tempSpread) / fishingSlippagePct;
            // 具备开仓条件则选择适合的价格开仓
            targetVolume = tempTargetVolume;
            targetVolume = min(pairTotalVolume - endVolume, targetVolume);
            if (tempVolume > stra::MIN_FLOAT){
                targetVolume = min(tempVolume, targetVolume);
            }
            if (tradingTypeOrder == stra::TAKER_TAKER) {
                if (ajdPct >= 0){
                    activeTargetPrice = spread.activeBidPrice1 * (1 - ajdPct);
                    passiveTargetPrice = spread.passiveAskPrice1;
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = spread.activeAskPrice1 * (1 - min(ajdPct, 0.0));
                passiveTargetPrice = spread.passiveAskPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
             LOG_INFO("targetActiveVolume <= expectActiveVolume or targetActiveVolume <= 0. targetActiveVolume:%f expectActiveVolume:%f", targetActiveVolume, expectActiveVolume);
            return pairOrder;
        }

    } else if (tradingTypeOffset == stra::CLOSE_LONG) {
        // algoPairOrder.mtCLStartSpread = 0.0002 + meanShift - closeShift;
        // algoPairOrder.mtCLEndSpread = 0.00021 + meanShift - closeShift;
        // algoPairOrder.mtCLStartVolume = -100;
        // algoPairOrder.mtCLEndVolume = -0;
        // algoPairOrder.mtCLSwitch = true;
        activeDirection = stra::Direction_LONG;
        passiveDirection = stra::Direction_SHORT;
        if (endVolume - pairTotalVolume > stra::MIN_FLOAT) {
            // tempSpread 是使用买1卖1价格考虑了滑点和手续费后目标锁定的价差, 如果报钓鱼单则应该向更远的方向报单
            tempTargetSpread = startSpread + (endSpread - startSpread) * max(min((pairTotalVolume - startVolume) / (endVolume - startVolume), 1.0), 0.0);
            // 需要调整的百分比
            ajdPct = (tempTargetSpread - tempSpread) / fishingSlippagePct;
            // 具备开仓条件则选择适合的价格开仓
            targetVolume = tempTargetVolume;
            targetVolume = min(endVolume - pairTotalVolume, targetVolume);
            if (tempVolume > stra::MIN_FLOAT){
                targetVolume = min(tempVolume, targetVolume);
            }
            if (tradingTypeOrder == stra::TAKER_TAKER) {
                if (ajdPct <= 0){
                    activeTargetPrice = spread.activeAskPrice1 * (1 - ajdPct);
                    passiveTargetPrice = spread.passiveBidPrice1;
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = spread.activeBidPrice1 * (1 - max(ajdPct, 0.0));
                passiveTargetPrice = spread.passiveBidPrice1;
                acOrderType = stra::OrderType_POST_ONLY;
                paOrderType = passiveOrderType;
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("targetActiveVolume <= expectActiveVolume. targetActiveVolume:%f expectActiveVolume:%f", targetActiveVolume, expectActiveVolume);
            return pairOrder;
        }
    }

    LOG_INFO("tradingTypeOffset:%s tradingTypeOrder:%s activeDirection:%s passiveDirection:%s endVolume:%f pairTotalVolume:%f tempTargetSpread:%f ajdPct:%f targetVolume:%f activeTargetPrice:%f passiveTargetPrice:%f acOrderType:%s paOrderType:%s", 
            stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::DirectionEnum2Str[activeDirection].c_str(), stra::DirectionEnum2Str[passiveDirection].c_str(), 
            endVolume, pairTotalVolume, tempTargetSpread, ajdPct, targetVolume, activeTargetPrice, passiveTargetPrice, stra::OrderTypeEnum2Str[acOrderType].c_str(), stra::OrderTypeEnum2Str[paOrderType].c_str());

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
    pairOrder.activePriceTickNum = activePriceTickNum;

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
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            pairOrder.pairTargetSpread = ttCSStartSpread;
        } else if (tradingTypeOffset == stra::OPEN_SHORT) {
            pairOrder.pairTargetSpread = ttOSStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            pairOrder.pairTargetSpread = ttCLStartSpread;
        }
    }
    else if (tradingTypeOrder == stra::MAKER_TAKER) {
        if (tradingTypeOffset == stra::OPEN_LONG) {
            pairOrder.pairTargetSpread = mtOLStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_SHORT) {
            pairOrder.pairTargetSpread = mtCSStartSpread;
        } else if (tradingTypeOffset == stra::OPEN_SHORT) {
            pairOrder.pairTargetSpread = mtOSStartSpread;
        } else if (tradingTypeOffset == stra::CLOSE_LONG) {
            pairOrder.pairTargetSpread = mtCLStartSpread;
        }
    }

    pairOrder.Init();
    strncpy(pairOrder.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);

    return pairOrder;
}

PairOrder AlgoFishingOrder::CreatePairOrder(stra::TradingType tradingType, stra::Direction activeDirection) {
    // OpenShort CloseLong activeDirection = Long
    // OpenLong CloseShort activeDirection = Short
    PairOrder pairOrder;
    int64_t pairOrderId = GenerateStrategyPairId();
    double expectVolume = GetExpectActiveVolume();
    if (activeDirection == stra::Direction_LONG){
        if (tradingType == stra::TAKER_TAKER) {
            if (expectVolume >= minVolume - stra::MIN_FLOAT) {
                if (ttOSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                }
            } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
                if (ttCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                }  
            } else {
                if (ttOSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                }
            }
        } else if (tradingType == stra::MAKER_TAKER) {
            if (expectVolume >= minVolume - stra::MIN_FLOAT) {
                if (mtOSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                }
            } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
                if (mtCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                }
            } else {
                if (mtOSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_SHORT, pairOrderId);
                }
            }
        }
    } else {
        if (tradingType == stra::TAKER_TAKER) {
            if (expectVolume >= minVolume - stra::MIN_FLOAT) {
                if (fabs(pairTotalVolume) < stra::MIN_FLOAT) {
                    if (ttOLSwitch) {
                        pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                    }
                    else {
                        if (ttCSSwitch) {
                            pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                        }
                    }
                }
            } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
                if (ttOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }
            } else {
                if (ttOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }
            }
        } else if (tradingType == stra::MAKER_TAKER) {
            if (expectVolume >= minVolume - stra::MIN_FLOAT) {
                if (fabs(pairTotalVolume) < stra::MIN_FLOAT) {
                    if (mtOLSwitch) {
                        pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                    }
                    else {
                        if (mtCSSwitch) {
                            pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                        }
                    }
                }
            } else if (expectVolume <= -minVolume + stra::MIN_FLOAT) {
                if (mtOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }  
            } else {
                if (mtOLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::OPEN_LONG, pairOrderId);
                }
            }
        }
    }

    return pairOrder;
}
