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
    dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
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
        double midPrice = (pdata->activeAskPrice[0] + pdata->activeBidPrice[0]) / 2;
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            // activeDirection = L
            if (midPrice / pdata->activePriceTema - 1 < -0.003) {
                LOG_INFO("midPrice / pdata->activePriceTema - 1 < -0.005  midPrice:%f  activePriceTema:%f", midPrice, pdata->activePriceTema);
                return pairOrder;
            }
        } else if(tradingTypeOffset == stra::OPEN_LONG || tradingTypeOffset == stra::CLOSE_SHORT){
            // activeDirection = S
            if (midPrice / pdata->activePriceTema - 1 > 0.003) {
                LOG_INFO("midPrice / pdata->activePriceTema - 1 > 0.005  midPrice:%f  activePriceTema:%f", midPrice, pdata->activePriceTema);
                return pairOrder;
            }
        }
    }

    if (pdata->activeAskPrice[0] / pdata->activeBidPrice[0] - 1 > 0.001) {
        LOG_INFO("ask -- bid too big! instrumentKey:{} activeAskPrice1:{} activeBidPrice1:{}", pairInstrumentKey, pdata->activeAskPrice[0], pdata->activeBidPrice[0]);
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
            tempSpreadAdj = max(activePriceTickNum, 0) * activeInfo.tickSize * 2 / (pdata->activeBidPrice[0] + pdata->activeAskPrice[0]);
        }
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = pdata->spreadAskBid - tradingFs - tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(pdata->spreadAskBid, pdata->spreadAskBidTema) - tradingFs - tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(pdata->passiveBidVolume[0], pdata->passiveBidPrice[0]) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }

        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = pdata->spreadBidAsk + tradingFs + tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(pdata->spreadBidAsk, pdata->spreadBidAskTema) + tradingFs + tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(pdata->passiveAskVolume[0], pdata->passiveAskPrice[0]) * passiveVolumePct;
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
            double tempTickNum = max(min((pdata->activeAskPrice[0] - pdata->activeBidPrice[0]) / activeInfo.tickSize - 1, (double)activePriceTickNum), 0.0);
            tempSpreadAdj = tempTickNum * activeInfo.tickSize * 2 / (pdata->activeBidPrice[0] + pdata->activeAskPrice[0]);
        }
        if (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::CLOSE_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = pdata->spreadBidBid - tradingFs - tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = min(pdata->spreadBidBid, pdata->spreadBidBidTema) - tradingFs - tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(pdata->passiveBidVolume[0], pdata->passiveBidPrice[0]) * passiveVolumePct;
            } else {
                tempVolume = -1;
            }
        } else if (tradingTypeOffset == stra::CLOSE_SHORT || tradingTypeOffset == stra::OPEN_LONG) {
            if (targetSpreadType == stra::TargetSpredPrice_NOW) { // ? 1代表什么
                tempSpread = pdata->spreadAskAsk + tradingFs + tempSpreadAdj;
            } else if (targetSpreadType == stra::TargetSpredPrice_NOW_MEAN) { // ? 2代表什么
                tempSpread = max(pdata->spreadAskAsk, pdata->spreadAskAskTema) + tradingFs + tempSpreadAdj;
            }
            if (activeVolumeCalcualteType == stra::ActiveVolumeCalcualteType_PassiveVolumePct) {
                tempVolume = GetActiveVolumeByPassiveVolume(pdata->passiveAskVolume[0], pdata->passiveAskPrice[0]) * passiveVolumePct;
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
    Direction activeDirection;
    Direction passiveDirection;
    double targetVolume;
    double ajdPct;
    double activeTargetPrice;
    double passiveTargetPrice;
    OrderType acOrderType;
    OrderType paOrderType;
    if (tradingTypeOffset == stra::OPEN_SHORT) {
        // algoPairOrder.mtOSStartSpread = 0.0002 + meanShift;
        // algoPairOrder.mtOSEndSpread = 0.00021 + meanShift;
        // algoPairOrder.mtOSStartVolume = 0;
        // algoPairOrder.mtOSEndVolume = 100;
        // algoPairOrder.mtOSSwitch = true;
        activeDirection = DT_LONG;
        passiveDirection = DT_SHORT;
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
                    activeTargetPrice = pdata->activeAskPrice[0] * (1 - ajdPct);
                    passiveTargetPrice = pdata->passiveBidPrice[0];
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = pdata->activeBidPrice[0] * (1 - max(ajdPct, 0.0));
                passiveTargetPrice = pdata->passiveBidPrice[0];
                acOrderType = OT_POST_ONLY;
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
        activeDirection = DT_SHORT;
        passiveDirection = DT_LONG;
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
                    activeTargetPrice = pdata->activeBidPrice[0] * (1 - ajdPct);
                    passiveTargetPrice = pdata->passiveAskPrice[0];
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = pdata->activeAskPrice[0] * (1 - min(ajdPct, 0.0));
                passiveTargetPrice = pdata->passiveAskPrice[0];
                acOrderType = OT_POST_ONLY;
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
        activeDirection = DT_SHORT;
        passiveDirection = DT_LONG;
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
                    activeTargetPrice = pdata->activeBidPrice[0] * (1 - ajdPct);
                    passiveTargetPrice = pdata->passiveAskPrice[0];
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = pdata->activeAskPrice[0] * (1 - min(ajdPct, 0.0));
                passiveTargetPrice = pdata->passiveAskPrice[0];
                acOrderType = OT_POST_ONLY;
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
        activeDirection = DT_LONG;
        passiveDirection = DT_SHORT;
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
                    activeTargetPrice = pdata->activeAskPrice[0] * (1 - ajdPct);
                    passiveTargetPrice = pdata->passiveBidPrice[0];
                    acOrderType = activeOrderType;
                    paOrderType = passiveOrderType;
                } else {
                    targetVolume = -1;
                }
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                activeTargetPrice = pdata->activeBidPrice[0] * (1 - max(ajdPct, 0.0));
                passiveTargetPrice = pdata->passiveBidPrice[0];
                acOrderType = OT_POST_ONLY;
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
            stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), DirectionEnum2StrMap[activeDirection].c_str(), DirectionEnum2StrMap[passiveDirection].c_str(), 
            endVolume, pairTotalVolume, tempTargetSpread, ajdPct, targetVolume, activeTargetPrice, passiveTargetPrice, OrderTypeEnum2StrMap[acOrderType].c_str(), OrderTypeEnum2StrMap[paOrderType].c_str());

    targetVolume = round(targetVolume / activeInfo.lotSize) * activeInfo.lotSize;
    if (targetVolume < stra::MIN_FLOAT) {
        return pairOrder;
    }

    pairOrder.activeBidPrice1 = pdata->activeBidPrice[0];
    pairOrder.activeBidVolume1 = pdata->activeBidVolume[0];
    pairOrder.activeAskPrice1 = pdata->activeAskPrice[0];
    pairOrder.activeAskVolume1 = pdata->activeAskVolume[0];
    pairOrder.passiveBidPrice1 = pdata->passiveBidPrice[0];
    pairOrder.passiveBidVolume1 = pdata->passiveBidVolume[0];
    pairOrder.passiveAskPrice1 = pdata->passiveAskPrice[0];
    pairOrder.passiveAskVolume1 = pdata->passiveAskVolume[0];

    pairOrder.pairId = pairOrderId;
    strncpy(pairOrder.baseAsset, baseAsset, stra::ASSET_LEN);
    pairOrder.targetVolume = targetVolume;
    strncpy(pairOrder.activeInstrumentKey, activeInstrumentKey, stra::INST_KEY_LEN);
    pairOrder.activeDirection = activeDirection;
    pairOrder.activeOrderType = acOrderType;
    // pairOrder.activePriceType = activePriceType;
    if (pairOrder.activeOrderType == OT_POST_ONLY){
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
    if (pairOrder.passiveOrderType == OT_POST_ONLY){
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

    pairOrder.Init(smc);
    strncpy(pairOrder.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);

    return pairOrder;
}

PairOrder AlgoFishingOrder::CreatePairOrder(stra::TradingType tradingType, Direction activeDirection) {
    // OpenShort CloseLong activeDirection = Long
    // OpenLong CloseShort activeDirection = Short
    PairOrder pairOrder;
    int64_t pairOrderId = GenerateStrategyPairId();
    double expectVolume = GetExpectActiveVolume();
    if (activeDirection == DT_LONG){
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
