#include "AlgoRebalanceOrder.h"
#include "SpreadManager.h"
#include "AccountManager.h"
#include "QuantTrade.h"
#include "BasicInfoMgr.h"
#include "Convert.h"


AlgoRebalanceOrder::AlgoRebalanceOrder() : BaseAlgoOrder() {
    activeTrade = -1;
}

PairOrder AlgoRebalanceOrder::GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId) {
    PairOrder pairOrder;
    if (fundVerifyFailedFlag && (tradingTypeOffset == stra::OPEN_SHORT || tradingTypeOffset == stra::OPEN_LONG)){
        return pairOrder;
    }

    if (activeTrade == -1) {
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
    double expectActiveVolume = 0.0;
    if (activeTrade == 1) {
        expectActiveVolume = GetExpectActiveVolume();
    }
    else {
        expectActiveVolume = GetExpectPassiveVolume();
    }
    
    double lockedSpread = GetLockedSpread();
    dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
    double tempSpread = 0.0;
    double tempVolume = 0.0;
    double startSpread = 0.0;
    double endSpread = 0.0;
    double startVolume = 0.0;
    double endVolume = 0.0;
    bool swch = false;

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

    LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s tempSpread:%f startSpread:%f endSpread:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), tempSpread, startSpread, endSpread, activeInstrumentKey, passiveInstrumentKey);

    double targetActiveVolume = 0;
    // order 相关
    Direction activeDirection;
    Direction passiveDirection;
    double targetVolume;
    double activeTargetPrice;
    double passiveTargetPrice;
    OrderType acOrderType;
    OrderType paOrderType;

    if (tradingTypeOffset == stra::CLOSE_SHORT) {
        targetActiveVolume = endVolume;
 
        if ((activeTrade == 1 && targetActiveVolume < expectActiveVolume) || (activeTrade == 0 && targetActiveVolume > expectActiveVolume)) {
            activeDirection = DT_SHORT;
            passiveDirection = DT_LONG;

            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, ttTargetVolume);
                activeTargetPrice = pdata->activeBidPrice[0];
                passiveTargetPrice = pdata->passiveAskPrice[0];
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType; 
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(expectActiveVolume - targetActiveVolume, mtTargetVolume);
                activeTargetPrice = pdata->activeAskPrice[0];
                passiveTargetPrice = pdata->passiveAskPrice[0];
                acOrderType = OT_POST_ONLY;
                paOrderType = passiveOrderType; 
            } else {
                LOG_INFO("not support tradingTypeOrder:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str());
                return pairOrder;
            }
        } else {
            LOG_INFO("tradingTypeOrder:%s tradingTypeOffset:%s targetActiveVolume <= expectActiveVolume. targetActiveVolume:%f expectActiveVolume:%f  activeInstrumentKey:%s  passiveInstrumentKey:%s", stra::TradingTypeEnum2Str[tradingTypeOrder].c_str(), stra::TradingTypeEnum2Str[tradingTypeOffset].c_str(), targetActiveVolume, expectActiveVolume, activeInstrumentKey, passiveInstrumentKey);
            return pairOrder;
        }
    } else if (tradingTypeOffset == stra::CLOSE_LONG) {
        targetActiveVolume = endVolume;
   
        if ((activeTrade == 1 && targetActiveVolume > expectActiveVolume) || (activeTrade == 0 && targetActiveVolume < expectActiveVolume)) {
            activeDirection = DT_LONG;
            passiveDirection = DT_SHORT;

            if (tradingTypeOrder == stra::TAKER_TAKER) {
                targetVolume = min(fabs(targetActiveVolume - expectActiveVolume), ttTargetVolume);
                activeTargetPrice = pdata->activeAskPrice[0];
                passiveTargetPrice = pdata->passiveBidPrice[0];
                acOrderType = activeOrderType;
                paOrderType = passiveOrderType; 
            } else if (tradingTypeOrder == stra::MAKER_TAKER) {
                targetVolume = min(fabs(targetActiveVolume - expectActiveVolume), mtTargetVolume);
                activeTargetPrice = pdata->activeBidPrice[0];
                passiveTargetPrice = pdata->passiveBidPrice[0];
                acOrderType = OT_POST_ONLY;
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

    if (fabs(targetActiveVolume) < stra::MIN_FLOAT) {
        pairOrder.reduceOnly = true;
    }

    if (activeTrade == 1) {
        if (activeInfo.calculateType == 0) {
            double midPrice = (pdata->activeBidPrice[0] + pdata->activeAskPrice[0]) / 2;
            if (midPrice > 0 && activeInfo.multiple > 0) {
                double volume = minOrderAmount / midPrice / activeInfo.multiple;
                if (fabs(targetActiveVolume) > stra::MIN_FLOAT) {
                    targetVolume = max(targetVolume, volume);
                }
            }
        }
        else if (activeInfo.calculateType == 1) {
           if (activeInfo.multiple > 0) {
                double volume = minOrderAmount / activeInfo.multiple;
                if (fabs(targetActiveVolume) > stra::MIN_FLOAT) {
                    targetVolume = max(targetVolume, volume);
                }
            }
        }
    }
    else if (activeTrade == 0) {
        if (passiveInfo.calculateType == 0) {
            double midPrice = (pdata->passiveBidPrice[0] + pdata->passiveAskPrice[0]) / 2;
            if (midPrice > 0 && passiveInfo.multiple > 0) {
                double volume = minOrderAmount / midPrice / passiveInfo.multiple;
                if (fabs(targetActiveVolume) > stra::MIN_FLOAT) {
                    targetVolume = max(targetVolume, volume);
                }
            }
        }
        else if (passiveInfo.calculateType == 1) {
           if (passiveInfo.multiple > 0) {
                double volume = minOrderAmount / passiveInfo.multiple;
                if (fabs(targetActiveVolume) > stra::MIN_FLOAT) {
                    targetVolume = max(targetVolume, volume);
                }
            }
        }
        targetVolume = round(targetVolume / activeInfo.lotSize) * activeInfo.lotSize;
    }

    if (targetVolume < stra::MIN_FLOAT) {
        LOG_INFO("");
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

    pairOrder.Init(smc);
    strncpy(pairOrder.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);

    return pairOrder;
}

PairOrder AlgoRebalanceOrder::CreatePairOrder(stra::TradingType tradingType) {
    PairOrder pairOrder;
    int64_t pairOrderId = GenerateStrategyPairId();
    double expectActiveVolume = GetExpectActiveVolume();
    double expectPassiveVolume = GetExpectPassiveVolume();
    //LOG_INFO("tradingType:%s  pairOrderId:%ld  expectVolume:%f  ttTargetVolume:%f  mtTargetVolume:%f", stra::TradingTypeEnum2Str[tradingType].c_str(), pairOrderId, expectVolume, ttTargetVolume, mtTargetVolume);
    if (tradingType == stra::TAKER_TAKER) {
        if (activeTrade == 1) {
            if (expectActiveVolume > -stra::MIN_FLOAT) {
                if (ttCSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            } else if (expectActiveVolume <= stra::MIN_FLOAT) {
                if (ttCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            }
        }
        else if (activeTrade == 0) {
            if (expectPassiveVolume > - stra::MIN_FLOAT) {
                if (ttCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            } else if (expectPassiveVolume <= stra::MIN_FLOAT) {
                if (ttCSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            }
        }
    } else if (tradingType == stra::MAKER_TAKER) {
        if (activeTrade == 1) {
            if (expectActiveVolume > -stra::MIN_FLOAT) {
                if (mtCSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            } else if (expectActiveVolume <= stra::MIN_FLOAT) {
                if (mtCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            }
        }
        else if (activeTrade == 0) {
            if (expectPassiveVolume > - stra::MIN_FLOAT) {
                if (mtCLSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_LONG, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            } else if (expectPassiveVolume <= stra::MIN_FLOAT) {
                if (mtCSSwitch) {
                    pairOrder = GetTargetPairOrder(tradingType, stra::CLOSE_SHORT, pairOrderId);
                } else {
                    LOG_INFO("");
                }
            }
        }
    }
    return pairOrder;
}

void AlgoRebalanceOrder::UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder, int64_t eventTime) {
    updateTime = eventTime;
    if (activeTrade == 1 && pairOrder.activeTotalVolumeOnOrder > stra::MIN_FLOAT) {
        double activeVolume = pairOrder.activeTotalVolumeOnOrder;
        double activePrice = pairOrder.activeTotalPriceOnOrder;

        double totalActiveVolume = pairTotalVolume;
        double totalActivePrice = pairActiveTotalPrice;

        if (totalActiveVolume > stra::MIN_FLOAT) {
            if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume += activeVolume;
                if (activeInfo.calculateType == 0) {
                    pairActiveTotalPrice = (totalActivePrice * totalActiveVolume + activePrice * activeVolume) / (totalActiveVolume + activeVolume);
                }
                else if (activeInfo.calculateType == 1) {
                    pairActiveTotalPrice = 1 / ((1 / totalActivePrice * totalActiveVolume + 1 / activePrice * activeVolume) / (totalActiveVolume + activeVolume));
                }
            }
            else if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume -= activeVolume;
                if (pairTotalVolume <= stra::MIN_FLOAT) {
                    pairActiveTotalPrice = activePrice;
                }
            }
        }
        else if (totalActiveVolume < -stra::MIN_FLOAT) {
            if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume -= activeVolume;
                if (activeInfo.calculateType == 0) {
                    pairActiveTotalPrice = (totalActivePrice * totalActiveVolume - activePrice * activeVolume) / (totalActiveVolume - activeVolume);
                }
                else if (activeInfo.calculateType == 1) {
                    pairActiveTotalPrice = 1 / ((1 / totalActivePrice * totalActiveVolume - 1 / activePrice * activeVolume) / (totalActiveVolume - activeVolume));
                }
            }
            else if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume += activeVolume;
                if (pairTotalVolume > stra::MIN_FLOAT) {
                    pairActiveTotalPrice = activePrice;
                }
            }     
        }
        else {
            if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume = activeVolume;
                pairActiveTotalPrice = activePrice;

                pairOrder.pairTotalVolume = activeVolume;
                pairOrder.pairActiveTotalPrice = activePrice;
            }
            else if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume = -activeVolume;
                pairActiveTotalPrice = -activePrice;

                pairOrder.pairTotalVolume = -activeVolume;
                pairOrder.pairActiveTotalPrice = -activePrice;      
            }
        }
    }
    else if (activeTrade == 0 && pairOrder.passiveTotalVolumeOnOrder > stra::MIN_FLOAT) {
        double passiveVolume = pairOrder.passiveTotalVolumeOnOrder;
        double passivePrice = pairOrder.passiveTotalPriceOnOrder;

        double totalPassiveVolume = pairPassiveTotalVolume;
        double totalPassivePrice = pairPassiveTotalPrice;

        if (totalPassiveVolume > stra::MIN_FLOAT) {
            if (pairOrder.passiveDirection == DT_LONG) {
                pairPassiveTotalVolume += passiveVolume;
                if (passiveInfo.calculateType == 0) {
                    pairPassiveTotalPrice = (totalPassivePrice * totalPassiveVolume + passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume);
                }
                else if (passiveInfo.calculateType == 1) {
                    pairPassiveTotalPrice = 1 / ((1 / totalPassivePrice * totalPassiveVolume + 1 / passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume));
                }
            }
            else if (pairOrder.passiveDirection == DT_SHORT) {
                pairPassiveTotalVolume -= passiveVolume;
                if (pairPassiveTotalVolume <= stra::MIN_FLOAT) {
                    pairPassiveTotalPrice = passivePrice;
                }
            }
        }
        else if (totalPassiveVolume < -stra::MIN_FLOAT) {
            if (pairOrder.passiveDirection == DT_SHORT) {
                pairPassiveTotalVolume -= passiveVolume;
                if (passiveInfo.calculateType == 0) {
                    pairPassiveTotalPrice = (totalPassivePrice * totalPassiveVolume - passivePrice * passiveVolume) / (totalPassiveVolume - passiveVolume);
                }
                else if (passiveInfo.calculateType == 1) {
                    pairPassiveTotalPrice = 1 / ((1 / totalPassivePrice * totalPassiveVolume - 1 / passivePrice * passiveVolume) / (totalPassiveVolume - passiveVolume));
                }
            }
            else if (pairOrder.passiveDirection == DT_LONG) {
                pairPassiveTotalVolume += passiveVolume;
                if (pairPassiveTotalVolume > stra::MIN_FLOAT) {
                    pairPassiveTotalPrice = passivePrice;
                }
            }     
        }
        else {
            if (pairOrder.passiveDirection == DT_LONG) {
                pairPassiveTotalVolume = passiveVolume;
                pairPassiveTotalPrice = passivePrice;

                pairOrder.pairPassiveTotalVolume = passiveVolume;
                pairOrder.pairPassiveTotalPrice = passivePrice;
            }
            else if (pairOrder.passiveDirection == DT_SHORT) {
                pairPassiveTotalVolume = -passiveVolume;
                pairPassiveTotalPrice = -passivePrice;

                pairOrder.pairPassiveTotalVolume = -passiveVolume;
                pairOrder.pairPassiveTotalPrice = -passivePrice;      
            }
        }
    }
}

void AlgoRebalanceOrder::PairOrderTrade(PairOrder& pairOrder, int64_t eventTime) {
    double activeFrozenValue = 0.0;
    if (activeInfo.calculateType == 0) {
        activeFrozenValue = pairOrder.activeFrozenVolume * pairOrder.activeFrozenPrice * activeInfo.multiple;
    }
    else {
        activeFrozenValue = pairOrder.activeFrozenVolume * activeInfo.multiple;
    }

    double passiveFrozenValue = 0.0;
    if (passiveInfo.calculateType == 0) {
        passiveFrozenValue = pairOrder.passiveFrozenVolume * pairOrder.passiveFrozenPrice * passiveInfo.multiple;
    }
    else {
        passiveFrozenValue = pairOrder.passiveFrozenVolume * passiveInfo.multiple;
    }


    if ((activeTrade == 1 && activeFrozenValue <= stra::MIN_FLOAT) && (activeTrade == 0 && passiveFrozenValue <= stra::MIN_FLOAT)) {
        // 这时候pairOrder已经完结，进行完结更新
        //LOG_INFO("start UpdateAlgoPairOrderByPairOrder!");
        UpdateAlgoPairOrderByPairOrder(pairOrder, eventTime);
        pairOrder.status = 1;
        pairOrder.updateTime = eventTime;
        string pairInstrumentKey = string(pairOrder.activeInstrumentKey) + "|" + string(pairOrder.passiveInstrumentKey);
        dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
        WritePairOrder(pairOrder, pdata);

        if (pairOrder.rebalanceFlag && pairOrder.passiveTotalVolumeOnOrder > 0) { // 可以去掉
            // 被动腿有成交且pairOrder完结,更改rebalance状态
            if (pairOrder.tradingTypeOrder == stra::MAKER_TAKER){
                // 被动腿有成交且pairOrder完结,更改rebalance状态
                // mtRebalanceFlag = false;
                // 被动腿有成交且pairOrder完结,更新滑点
                mtSlipage = 0.8 * mtSlipage + 0.2 * pairOrder.CalculatePassiveSlippage();
                if (mtSlipage > 2 * passiveTakerSlippage + profitPct){
                    mtSlipageFlag = true;
                    // 异常报警
                    char msg[stra::MSG_LEN];
                    sprintf(msg, "strategyName:%s algoOrderId:%ld mtSlipageFlag:%d  mtSlipage:%f", algoStrategyName, algoOrderId, mtSlipageFlag, mtSlipage);
                    rLarkMsg.Push(msg);
                }
            } else {
                // 被动腿有成交且pairOrder完结,更改rebalance状态
                // ttRebalanceFlag = false;
                // 被动腿有成交且pairOrder完结,更新滑点
                ttSlipage = 0.8 * ttSlipage + 0.2 * pairOrder.CalculatePassiveSlippage();
                if (ttSlipage > 2 * passiveTakerSlippage + profitPct){
                    ttSlipageFlag = true;
                    // 异常报警
                    char msg[stra::MSG_LEN];
                    sprintf(msg, "strategyName:%s algoOrderId:%ld ttSlipageFlag:%d  ttSlipage:%f", algoStrategyName, algoOrderId, ttSlipageFlag, ttSlipage);
                    rLarkMsg.Push(msg);
                }
            }            
        }
        pairOrderMgr.DeletePairOrderByPairOrder(pairOrder);
    }
    else {
        LOG_INFO("");
    }
 
}
