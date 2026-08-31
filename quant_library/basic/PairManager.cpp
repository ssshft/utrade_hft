#include "PairManager.h"
#include "StrategyConfig.h"
#include "SpreadManager.h"


static PairOrder defaultPairOrder;

void PairOrder::Init() {
    vector<string> vActive;
    splitString(activeInstrumentKey, vActive, ".");
    if (vActive.size() >= 3) {
        activeExchangeType = ExchangeTypeStr2EnumMap[vActive[0]];
        activeInstType = InstTypeStr2EnumMap[vActive[1]];
        strncpy(activeInstrument, vActive[2].c_str(), stra::INST_ID_LEN);
    }
 

    vector<string> vPassive;
    splitString(passiveInstrumentKey, vPassive, ".");
    if (vPassive.size() >= 3) {
        passiveExchangeType = ExchangeTypeStr2EnumMap[vPassive[0]];
        passiveInstType = InstTypeStr2EnumMap[vPassive[1]];
        strncpy(passiveInstrument, vPassive[2].c_str(), stra::INST_ID_LEN);
    }

    activeInfo = BasicInfoMgr::GetInstance().GetBasicInfo(activeInstrumentKey);
    passiveInfo = BasicInfoMgr::GetInstance().GetBasicInfo(passiveInstrumentKey);


    double minAmount = StrategyConfig::GetInstance().GetMinOrderAmount();
    if (minAmount > 0) {
        minOrderAmount = minAmount;
    }
}

stra::QuantOrder PairOrder::CreateActiveOrder(int64_t strategyOrderId) {
    double price = 0.0;
    const Bbo& bbo = SpreadManager::Instance().GetBbo(activeInstrumentKey);

    if (activeOrderType == OT_POST_ONLY) { // OrderType_POST_ONLY == MAKER
        if (activeDirection == DT_LONG) {
            if (activePriceTickFlag) { 
                price = activeTargetPrice + activePriceTickNum * activeInfo.tickSize;
            } else {
                price = activeTargetPrice * (1 + activePricePct);
            }
            price = min(price, bbo.askPrice - activeInfo.tickSize);  // 取整，是tickSize的整数倍
            price = floor((price + stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        } else {
            if (activePriceTickFlag) { 
                price = activeTargetPrice - activePriceTickNum * activeInfo.tickSize;
            } else {
                price = activeTargetPrice * (1 - activePricePct);
            }
            price = max(price, bbo.bidPrice + activeInfo.tickSize);
            price = ceil((price - stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        }
    } else {
        if (activeDirection == DT_LONG) {
            if (activePriceTickFlag) { 
                price = activeTargetPrice + activePriceTickNum * activeInfo.tickSize;
            } else {
                price = activeTargetPrice * (1 + activePricePct);
            }
            price = floor((price + stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        } else {
            if (activePriceTickFlag) {
                price = activeTargetPrice - activePriceTickNum * activeInfo.tickSize;
            } else {
                price = activeTargetPrice * (1 - activePricePct);
            }
            price = ceil((price - stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        }
    }
    // price = round(price / activeInfo.tickSize) * activeInfo.tickSize;
    stra::QuantOrder order;
    // 小于最小报单量,不创建order
    double orderAmount = 0.0;

    double currentOrderAmount = 0.0;
    if (activeInfo.calculateType == 0) {
        currentOrderAmount = targetVolume * price * activeInfo.multiple;
    }
    else if (activeInfo.calculateType == 1) {
        currentOrderAmount = targetVolume * activeInfo.multiple;
    }

    if (activeExchangeType == BINANCE) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == GATEIO) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == BYBIT) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == OKX) {
        orderAmount = targetVolume;
    }
    else {
        orderAmount = targetVolume;
    }

    LOG_INFO("CreateActiveOrder activeInstrument%s orderAmount:%f   activeInfo.minSize:%f", activeInstrument, orderAmount, activeInfo.minSize);
    
    if (!reduceOnly) {
        if (orderAmount < activeInfo.minSize) {
            return order;
        }

        if (currentOrderAmount < minOrderAmount) {
            return order;
        }
    }


    order.strategyOrderId = strategyOrderId;
    order.strategyAccountId = activeAccountId;
    order.exchangeType = activeExchangeType;
    strncpy(order.instrument, activeInstrument, stra::INST_ID_LEN);
    strncpy(order.instrumentKey, activeInstrumentKey, stra::INST_KEY_LEN);

    strncpy(order.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);
    order.tradingType = tradingTypeOrder;
    order.tradingTypeOffset = tradingTypeOffset;
    order.instType = activeInstType;
    order.orderType = activeOrderType;
    order.direction = activeDirection;
    order.offsetFlag = OF_OPEN;
    order.orderStatus = OS_PEND;

    auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
    int64_t currentTime = GetCurrentTimeUs();
    dt.updateTime = currentTime;
    dt.orderStatus = order.orderStatus;
    order.orderTimeStatus.size++;
    if (order.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
        order.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
        LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", order.orderTimeStatus.size, stra::TIME_STATUS_LEN);
    }

    order.price = price;
    order.volume = targetVolume;
    order.targetPrice = activeTargetPrice;
    order.pairId = pairId;
    order.algoPairId = algoPairId;
    order.isActiveOrder = true;
    order.rebalance = rebalanceFlag;
    strncpy(order.strategyName, strategyName, stra::NAME_LEN);
    sActiveOrder.insert(order.strategyOrderId);
    order.reduceOnly = reduceOnly;
    return order;
}

stra::QuantOrder PairOrder::CreateVolumePassiveOrder(int64_t strategyOrderId) {
    double price = 0.0;
    const Bbo& bbo = SpreadManager::Instance().GetBbo(passiveInstrumentKey);

    if (passiveOrderType == OT_POST_ONLY) { // OrderType_POST_ONLY == MAKER
        if (passiveDirection == DT_LONG) {
            if (passivePriceTickFlag) { 
                price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
            } else {
                price = passiveTargetPrice * (1 + passivePricePct);
            }
            price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
            price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
        } else {
            if (passivePriceTickFlag) { 
                price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
            } else {
                price = passiveTargetPrice * (1 - passivePricePct);
            }
            price = max(price, bbo.bidPrice + passiveInfo.tickSize);
            price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
        }
    } else {
        if (passiveDirection == DT_LONG) {
            if (passivePriceTickFlag) { 
                price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
            } else {
                price = passiveTargetPrice * (1 + passivePricePct);
            }
            price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
        } else {
            if (passivePriceTickFlag) {
                price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
            } else {
                price = passiveTargetPrice * (1 - passivePricePct);
            }
            price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
        }
    }
    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
    stra::QuantOrder order;
    // 小于最小报单量,不创建order
    double orderAmount = 0.0;

    double currentOrderAmount = 0.0;
    if (passiveInfo.calculateType == 0) {
        currentOrderAmount = targetVolume * price * passiveInfo.multiple;
    }
    else if (passiveInfo.calculateType == 1) {
        currentOrderAmount = targetVolume * passiveInfo.multiple;
    }

    if (activeExchangeType == BINANCE) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == GATEIO) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == BYBIT) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == OKX) {
        orderAmount = targetVolume;
    }
    else {
        orderAmount = targetVolume;
    }

    LOG_INFO("CreateActiveOrder activeInstrument%s orderAmount:%f   passiveInfo.minSize:%f", activeInstrument, orderAmount, passiveInfo.minSize);
    
    if (!reduceOnly) {
        if (orderAmount < passiveInfo.minSize) {
            return order;
        }

        // if (currentOrderAmount < minOrderAmount) {
        //     return order;
        // }
    }


    order.strategyOrderId = strategyOrderId;
    order.strategyAccountId = passiveAccountId;
    order.exchangeType = passiveExchangeType;
    strncpy(order.instrument, passiveInstrument, stra::INST_ID_LEN);
    strncpy(order.instrumentKey, passiveInstrumentKey, stra::INST_KEY_LEN);

    strncpy(order.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);
    order.tradingType = tradingTypeOrder;
    order.tradingTypeOffset = tradingTypeOffset;
    order.instType = activeInstType;
    order.orderType = activeOrderType;
    order.direction = activeDirection;
    order.offsetFlag = OF_OPEN;
    order.orderStatus = OS_PEND;

    auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
    int64_t currentTime = GetCurrentTimeUs();
    dt.updateTime = currentTime;
    dt.orderStatus = order.orderStatus;
    order.orderTimeStatus.size++;
    if (order.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
        order.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
        LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", order.orderTimeStatus.size, stra::TIME_STATUS_LEN);
    }

    order.price = price;
    order.volume = targetVolume;
    order.targetPrice = passiveTargetPrice;
    order.pairId = pairId;
    order.algoPairId = algoPairId;
    order.isActiveOrder = true;
    order.rebalance = rebalanceFlag;
    strncpy(order.strategyName, strategyName, stra::NAME_LEN);
    sActiveOrder.insert(order.strategyOrderId);
    order.reduceOnly = reduceOnly;
    return order;
}


stra::QuantOrder PairOrder::CreateOrginActiveOrder(int64_t strategyOrderId) {
    // 直接使用pairOrder的报单价格作为报单价
    double price = 0.0;
    const Bbo& bbo = SpreadManager::Instance().GetBbo(activeInstrumentKey);

    if (activeOrderType == OT_POST_ONLY) { // OrderType_POST_ONLY == MAKER
        if (activeDirection == DT_LONG) {
            price = activeTargetPrice;
            price = min(price, bbo.askPrice - activeInfo.tickSize);  // 取整，是tickSize的整数倍
            price = min(price, bbo.bidPrice + activeInfo.tickSize);  // 取整，是tickSize的整数倍
            price = floor((price + stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        } else {
            price = activeTargetPrice;
            price = max(price, bbo.bidPrice + activeInfo.tickSize);
            price = max(price, bbo.askPrice - activeInfo.tickSize);  // 取整，是tickSize的整数倍
            price = ceil((price - stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        }
    } else {
        if (activeDirection == DT_LONG) {
            price = activeTargetPrice;
            price = floor((price + stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        } else {
            price = activeTargetPrice;
            price = ceil((price - stra::MIN_FLOAT) / activeInfo.tickSize) * activeInfo.tickSize;
        }
    }
    // price = round(price / activeInfo.tickSize) * activeInfo.tickSize;
    stra::QuantOrder order;
    // 小于最小报单量,不创建order
    double orderAmount = 0.0;

    double currentOrderAmount = 0.0;
    if (activeInfo.calculateType == 0) {
        currentOrderAmount = targetVolume * price * activeInfo.multiple;
    }
    else if (activeInfo.calculateType == 1) {
        currentOrderAmount = targetVolume * activeInfo.multiple;
    }


    if (activeExchangeType == BINANCE) {
        // if (activeInfo.calculateType == 0) {
        //     orderAmount = targetVolume * price * activeInfo.multiple;
        // } else if (activeInfo.calculateType == 1) {
        //     orderAmount = targetVolume * activeInfo.multiple;
        // }
        orderAmount = targetVolume;
    } else if (activeExchangeType == GATEIO) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == BYBIT) {
        orderAmount = targetVolume;
    } else if (activeExchangeType == OKX) {
        orderAmount = targetVolume;
    }
    else {
        orderAmount = targetVolume;
    }

    if (!reduceOnly) {
        if (orderAmount < activeInfo.minSize) {
            return order;
        }

        if (currentOrderAmount < minOrderAmount) {
            return order;
        }
    }


    order.strategyOrderId = strategyOrderId;
    order.strategyAccountId = activeAccountId;
    order.exchangeType = activeExchangeType;
    strncpy(order.instrument, activeInstrument, stra::INST_ID_LEN);
    strncpy(order.instrumentKey, activeInstrumentKey, stra::INST_KEY_LEN);
    
    strncpy(order.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);
    order.tradingType = tradingTypeOrder;
    order.tradingTypeOffset = tradingTypeOffset;
    order.instType = activeInstType;
    order.orderType = activeOrderType;
    order.direction = activeDirection;
    order.offsetFlag = OF_OPEN;
    order.orderStatus = OS_PEND;

    auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
    int64_t currentTime = GetCurrentTimeUs();
    dt.updateTime = currentTime;
    dt.orderStatus = order.orderStatus;
    order.orderTimeStatus.size++;
    if (order.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
        order.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
        LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", order.orderTimeStatus.size, stra::TIME_STATUS_LEN);
    }

    order.price = price;
    order.volume = targetVolume;
    order.targetPrice = activeTargetPrice;
    order.pairId = pairId;
    order.algoPairId = algoPairId;
    order.isActiveOrder = true;
    order.rebalance = rebalanceFlag;
    strncpy(order.strategyName, strategyName, stra::NAME_LEN);
    sActiveOrder.insert(order.strategyOrderId);

    order.reduceOnly = reduceOnly;
    return order;
}


stra::QuantOrder PairOrder::CreatePassiveOrder(int64_t strategyOrderId, PositionManager* posMgr) {
    double activeAmount = 0.0;
    double passiveAmount = 0.0;

    if (strcmp(activeInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instRight.c_str(), "USDT") == 0 || strcmp(activeInfo.instRight.c_str(), "USD") == 0 || strcmp(activeInfo.instRight.c_str(), "USDC") == 0 || strcmp(activeInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        auto& leftAss = posMgr->GetAccount().mAsset[activeInfo.instLeft];
        auto& pos = posMgr->GetAccount().mPosition[activeInstrumentKey];
        activeAmount += leftAss.totalAmount - leftAss.loanAmount;
	//LOG_INFO("CreatePassiveOrder leftAss:%s activeAmount: %f leftAss.totalAmount: %f loanAmount: %f", activeInfo.instLeft.c_str(), activeAmount, leftAss.totalAmount, leftAss.loanAmount);
        if (activeInfo.calculateType == 0) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount += pos.longPosition * activeInfo.multiple;
                }
            } else {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount -= pos.shortPosition * activeInfo.multiple;
                }
            }
        } else if (activeInfo.calculateType == 1) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount += pos.longPosition * activeInfo.multiple / pos.longAvgPrice;
                }
            } else {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount -= pos.shortPosition * activeInfo.multiple / pos.shortAvgPrice;
                }
            }
        }
    } else if (strcmp(activeInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instLeft.c_str(), "USDT") == 0 || strcmp(activeInfo.instLeft.c_str(), "USD") == 0 || strcmp(activeInfo.instLeft.c_str(), "USDC") == 0 || strcmp(activeInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) { 
        auto& rightAss = posMgr->GetAccount().mAsset[activeInfo.instRight];
        auto& pos = posMgr->GetAccount().mPosition[activeInstrumentKey];
        activeAmount += rightAss.totalAmount - rightAss.loanAmount;
        if (activeInfo.calculateType == 0) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount -= pos.longPosition * activeInfo.multiple * pos.longAvgPrice;
                }
            } else {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount += pos.shortPosition * activeInfo.multiple * pos.shortAvgPrice;
                }
            }
        } else if (activeInfo.calculateType == 1) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount -= pos.longPosition * activeInfo.multiple;
                }
            } else {
                if (activeInstType == USDT_SWAP || activeInstType == USDT_FUTURES || activeInstType == BUSD_SWAP || activeInstType == C_SWAP || activeInstType == C_FUTURES) {
                    activeAmount += pos.shortPosition * activeInfo.multiple;
                }
            }
        }
    }

    if (strcmp(passiveInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instRight.c_str(), "USDT") == 0 || strcmp(passiveInfo.instRight.c_str(), "USD") == 0 || strcmp(passiveInfo.instRight.c_str(), "USDC") == 0 || strcmp(passiveInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        auto& pos = posMgr->GetAccount().mPosition[passiveInstrumentKey];
        if (passiveInfo.calculateType == 0) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount += (pos.longPosition + pos.frozenLongPosition - pos.frozenShortPosition) * passiveInfo.multiple;
                }
            } else {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount -= (pos.shortPosition + pos.frozenShortPosition - pos.frozenLongPosition) * passiveInfo.multiple;
                }
            }
        } else if (passiveInfo.calculateType == 1) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount += (pos.longPosition  / pos.longAvgPrice + pos.frozenLongPosition / pos.frozenLongPrice - pos.frozenShortPosition / pos.frozenShortPrice) * passiveInfo.multiple;
                }
            } else {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount -= (pos.shortPosition  / pos.shortAvgPrice + pos.frozenShortPosition / pos.frozenShortPrice - pos.frozenLongPosition / pos.frozenLongPrice) * passiveInfo.multiple;
                }
            }
        }
    } else if (strcmp(passiveInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instLeft.c_str(), "USDT") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USD") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USDC") == 0 || strcmp(passiveInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {  
        auto& pos = posMgr->GetAccount().mPosition[passiveInstrumentKey];
        if (passiveInfo.calculateType == 0) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount -= (pos.longPosition  * pos.longAvgPrice + pos.frozenLongPosition * pos.frozenLongPrice - pos.frozenShortPosition * pos.frozenShortPrice) * passiveInfo.multiple;
                }
            } else {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount += (pos.shortPosition * pos.shortAvgPrice + pos.frozenShortPosition * pos.frozenShortPrice - pos.frozenLongPosition * pos.frozenLongPrice) * passiveInfo.multiple;
                }
            }
        } else if (activeInfo.calculateType == 1) {
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount -= (pos.longPosition + pos.frozenLongPosition - pos.frozenShortPosition) * passiveInfo.multiple;
                }
            } else {
                if (passiveInstType == USDT_SWAP || passiveInstType == USDT_FUTURES || passiveInstType == BUSD_SWAP || passiveInstType == C_SWAP || passiveInstType == C_FUTURES) {
                    passiveAmount += (pos.shortPosition + pos.frozenShortPosition - pos.frozenLongPosition) * passiveInfo.multiple;
                }
            }
        }
    }


    // create order
    double price = -1.0;
    double volume = 0;
    double passiveTargetAmount = activeAmount + passiveAmount;

    const Bbo& bbo = SpreadManager::Instance().GetBbo(passiveInstrumentKey);

    // minVolume 
    if (strcmp(passiveInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instRight.c_str(), "USDT") == 0 || strcmp(passiveInfo.instRight.c_str(), "USD") == 0 || strcmp(passiveInfo.instRight.c_str(), "USDC") == 0 || strcmp(passiveInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        if (passiveDirection == DT_LONG) {
            if (passiveTargetAmount <= stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    price = floor((price + stra::MIN_FLOAT)/  passiveInfo.tickSize) * passiveInfo.tickSize;
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        } else if (passiveDirection == DT_SHORT) {
            if (passiveTargetAmount > stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        }
    } else if (strcmp(passiveInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instLeft.c_str(), "USDT") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USD") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USDC") == 0 || strcmp(passiveInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {  
        if (passiveDirection == DT_LONG) {
            if (passiveTargetAmount > stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        } else if (passiveDirection == DT_SHORT) {
            if (passiveTargetAmount <= stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        }
    }

    //LOG_INFO("CreatPassiveOrder volume:%f lotSize:%f", volume, passiveInfo.lotSize);

    stra::QuantOrder order;

    // 小于最小报单量,不创建order
    double orderAmount = 0.0;
    volume = round(volume / passiveInfo.lotSize) * passiveInfo.lotSize;
    if (passiveExchangeType == BINANCE) {
        orderAmount = volume;
    } else if (passiveExchangeType == GATEIO) {
        orderAmount = volume;
    } else if (passiveExchangeType == BYBIT) {
        orderAmount = volume;
    } else if (passiveExchangeType == OKX) {
        orderAmount = volume;
    }
    else {
        orderAmount = volume;
    }

    if (!reduceOnly) {
        if (orderAmount < passiveInfo.minSize) {
            return order;
        }

        double currentOrderAmount = 0.0;
        if (passiveInfo.calculateType == 0) {
            currentOrderAmount = volume * price * passiveInfo.multiple;
        }
        else if (passiveInfo.calculateType == 1) {
            currentOrderAmount = volume * passiveInfo.multiple;
        }

        if (passiveExchangeType == BINANCE) {
            if (currentOrderAmount < passiveInfo.minAmount) {
                return order;
            }
        }
    }

  
    order.strategyOrderId = strategyOrderId;
    order.strategyAccountId = passiveAccountId;
    order.exchangeType = passiveExchangeType;
    strncpy(order.instrument, passiveInstrument, stra::INST_ID_LEN);
    strncpy(order.instrumentKey, passiveInstrumentKey, stra::INST_KEY_LEN);

    strncpy(order.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);
    order.tradingType = tradingTypeOrder;
    order.tradingTypeOffset = tradingTypeOffset;
    order.instType = passiveInstType;
    order.orderType = passiveOrderType;
    order.direction = passiveDirection;
    order.offsetFlag = OF_OPEN;
    order.orderStatus = OS_PEND;
    order.price = price;
//order.volume = round(volume / passiveInfo.lotSize) * passiveInfo.lotSize;
    order.volume = volume;
    order.targetPrice = passiveTargetPrice;
    order.pairId = pairId;
    order.algoPairId = algoPairId;
    order.isActiveOrder = false;


    auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
    int64_t currentTime = GetCurrentTimeUs();
    dt.updateTime = currentTime;
    dt.orderStatus = order.orderStatus;
    order.orderTimeStatus.size++;
    if (order.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
        order.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
        LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", order.orderTimeStatus.size, stra::TIME_STATUS_LEN);
    }

    strncpy(order.strategyName, strategyName, stra::NAME_LEN);
    order.rebalance = rebalanceFlag;
    order.reduceOnly = reduceOnly;

    return order;
}

stra::QuantOrder PairOrder::CreatePassiveOrder(int64_t strategyOrderId) {
    // LOG_INFO("CreatePassiveOrder now is empty! should not run here. strategyOrderId:%ld", strategyOrderId);
    // char msg[stra::MSG_LEN];
    // sprintf(msg, "CreatePassiveOrder strategyOrderId:%ld", strategyOrderId);
    // rLarkMsg.Push(msg);
    double activeAmount = 0.0;
    double passiveAmount = 0.0;

    if (strcmp(activeInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instRight.c_str(), "USDT") == 0 || strcmp(activeInfo.instRight.c_str(), "USD") == 0 || strcmp(activeInfo.instRight.c_str(), "USDC") == 0 || strcmp(activeInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        if (activeInfo.calculateType == 0) {
            if (activeDirection == DT_LONG){
                activeAmount += activeTotalVolumeOnOrder * activeInfo.multiple;
            } else {
                activeAmount -= activeTotalVolumeOnOrder * activeInfo.multiple;
            }
        } else if (activeInfo.calculateType == 1) {
            if (activeDirection == DT_LONG){
                activeAmount += activeTotalVolumeOnOrder * activeInfo.multiple / activeTotalPriceOnOrder;
            } else {
                activeAmount -= activeTotalVolumeOnOrder * activeInfo.multiple / activeTotalPriceOnOrder;
            }
        }
    } else if (strcmp(activeInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instLeft.c_str(), "USDT") == 0 || strcmp(activeInfo.instLeft.c_str(), "USD") == 0 || strcmp(activeInfo.instLeft.c_str(), "USDC") == 0 || strcmp(activeInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) { 
        if (activeInfo.calculateType == 0) {
            if (activeDirection == DT_LONG){
                activeAmount -= activeTotalVolumeOnOrder * activeInfo.multiple * activeTotalPriceOnOrder;
            } else {
                activeAmount += activeTotalVolumeOnOrder * activeInfo.multiple * activeTotalPriceOnOrder;
            }
        } else if (activeInfo.calculateType == 1) {
            if (activeDirection == DT_LONG){
                activeAmount -= activeTotalVolumeOnOrder * activeInfo.multiple;
            } else {
                activeAmount += activeTotalVolumeOnOrder * activeInfo.multiple;
            }
        }
    }

    if (strcmp(passiveInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instRight.c_str(), "USDT") == 0 || strcmp(passiveInfo.instRight.c_str(), "USD") == 0 || strcmp(passiveInfo.instRight.c_str(), "USDC") == 0 || strcmp(passiveInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        // auto& pos = posMgr->GetAccount().mPosition[passiveInstrumentKey];
        if (passiveInfo.calculateType == 0) {
            if (passiveDirection == DT_LONG){
                passiveAmount += passiveTotalVolumeOnOrder * passiveInfo.multiple + passiveFrozenVolume * passiveInfo.multiple;
            } else {
                passiveAmount -= passiveTotalVolumeOnOrder * passiveInfo.multiple + passiveFrozenVolume * passiveInfo.multiple;
            }
        } else if (passiveInfo.calculateType == 1) {
            if (passiveDirection == DT_LONG){
                passiveAmount += passiveTotalVolumeOnOrder * passiveInfo.multiple / passiveTotalPriceOnOrder + passiveFrozenVolume * passiveInfo.multiple / passiveFrozenPrice;
            } else {
                passiveAmount -= passiveTotalVolumeOnOrder * passiveInfo.multiple / passiveTotalPriceOnOrder + passiveFrozenVolume * passiveInfo.multiple / passiveFrozenPrice;
            }
        }
    } else if (strcmp(passiveInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instLeft.c_str(), "USDT") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USD") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USDC") == 0 || strcmp(passiveInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {  
        // auto& pos = posMgr->GetAccount().mPosition[passiveInstrumentKey];
        if (passiveInfo.calculateType == 0) {
            if (passiveDirection == DT_LONG){
                passiveAmount -= passiveTotalVolumeOnOrder * passiveInfo.multiple * passiveTotalPriceOnOrder + passiveFrozenVolume * passiveInfo.multiple * passiveFrozenPrice;
            } else {
                passiveAmount += passiveTotalVolumeOnOrder * passiveInfo.multiple * passiveTotalPriceOnOrder + passiveFrozenVolume * passiveInfo.multiple * passiveFrozenPrice;
            }
        } else if (activeInfo.calculateType == 1) {
            if (passiveDirection == DT_LONG){
                passiveAmount -= passiveTotalVolumeOnOrder * passiveInfo.multiple + passiveFrozenVolume * passiveInfo.multiple;
            } else {
                passiveAmount += passiveTotalVolumeOnOrder * passiveInfo.multiple + passiveFrozenVolume * passiveInfo.multiple;
            }
        }
    }

    // create order
    double price = -1.0;
    double volume = 0;
    double passiveTargetAmount = activeAmount + passiveAmount;
   
    const Bbo& bbo = SpreadManager::Instance().GetBbo(passiveInstrumentKey);
    // minVolume 
    if (strcmp(passiveInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instRight.c_str(), "USDT") == 0 || strcmp(passiveInfo.instRight.c_str(), "USD") == 0 || strcmp(passiveInfo.instRight.c_str(), "USDC") == 0 || strcmp(passiveInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {
        if (passiveDirection == DT_LONG) {
            if (passiveTargetAmount <= stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    price = floor((price + stra::MIN_FLOAT)/  passiveInfo.tickSize) * passiveInfo.tickSize;
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        } else if (passiveDirection == DT_SHORT) {
            if (passiveTargetAmount > stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        }
    } else if (strcmp(passiveInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instLeft.c_str(), "USDT") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USD") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USDC") == 0 || strcmp(passiveInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") == 0 || strcmp(baseAsset, "BUSD") == 0))) {  
        if (passiveDirection == DT_LONG) {
            if (passiveTargetAmount > stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 + passivePricePct);
                        }
                        price = min(price, bbo.askPrice - passiveInfo.tickSize);  // 取整，是tickSize的整数倍
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice + passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.askPrice + passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 + passivePricePct);
                            price = bbo.askPrice * (1 + passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = floor((price + stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        } else if (passiveDirection == DT_SHORT) {
            if (passiveTargetAmount <= stra::MIN_FLOAT) {
                if (passiveInfo.calculateType == 0) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) / passiveInfo.multiple;
                } else if (passiveInfo.calculateType == 1) {
                    if (passiveOrderType == OT_POST_ONLY) {
                        if (passivePriceTickFlag) {
                            price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            price = passiveTargetPrice * (1 - passivePricePct);
                        }
                        price = max(price, bbo.bidPrice + passiveInfo.tickSize);
                    } else {
                        if (passivePriceTickFlag) {
                            //price = passiveTargetPrice - passivePriceTickNum * passiveInfo.tickSize;
                            price = bbo.bidPrice - passivePriceTickNum * passiveInfo.tickSize;
                        } else {
                            //price = passiveTargetPrice * (1 - passivePricePct);
                            price = bbo.bidPrice * (1 - passivePricePct);
                        }
                    }
                    // price = round(price / passiveInfo.tickSize) * passiveInfo.tickSize;
                    price = ceil((price - stra::MIN_FLOAT) / passiveInfo.tickSize) * passiveInfo.tickSize;
                    volume = fabs(passiveTargetAmount) * price / passiveInfo.multiple;
                }
            }
        }
    }

    stra::QuantOrder order;

    // 小于最小报单量,不创建order
    double orderAmount = 0.0;
    volume = round(volume / passiveInfo.lotSize) * passiveInfo.lotSize;
    if (passiveExchangeType == BINANCE) {
        orderAmount = volume;
    } else if (passiveExchangeType == GATEIO) {
        orderAmount = volume;
    } else if (passiveExchangeType == BYBIT) {
        orderAmount = volume;
    } else if (passiveExchangeType == OKX) {
        orderAmount = volume;
    }

    if (!reduceOnly) {
        if (orderAmount < passiveInfo.minSize) {
            return order;
        }

        if (passiveExchangeType == BINANCE) {
            double totalOrderAmount = 0.0;
            if (passiveInfo.calculateType == 0) {
                totalOrderAmount = volume * price * passiveInfo.multiple;
            }
            else if (passiveInfo.calculateType == 1) {
                totalOrderAmount = volume * passiveInfo.multiple;
            }

            if (totalOrderAmount < passiveInfo.minAmount) {
                return order;
            }
        }
    }

    
    order.strategyOrderId = strategyOrderId;
    order.strategyAccountId = passiveAccountId;
    order.exchangeType = passiveExchangeType;
    strncpy(order.instrument, passiveInstrument, stra::INST_ID_LEN);
    strncpy(order.instrumentKey, passiveInstrumentKey, stra::INST_KEY_LEN);

    strncpy(order.pairInstrumentKey, pairInstrumentKey, stra::INST_KEY_LEN);
    order.tradingType = tradingTypeOrder;
    order.tradingTypeOffset = tradingTypeOffset;
    order.instType = passiveInstType;
    order.orderType = passiveOrderType;
    order.direction = passiveDirection;
    order.offsetFlag = OF_OPEN;
    order.orderStatus = OS_PEND;
    order.price = price;
    //order.volume = round(volume / passiveInfo.lotSize) * passiveInfo.lotSize;
    order.volume = volume;
    order.targetPrice = passiveTargetPrice;
    order.pairId = pairId;
    order.algoPairId = algoPairId;
    order.isActiveOrder = false;

    auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
    int64_t currentTime = GetCurrentTimeUs();
    dt.updateTime = currentTime;
    dt.orderStatus = order.orderStatus;
    order.orderTimeStatus.size++;
    if (order.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
        order.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
        LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", order.orderTimeStatus.size, stra::TIME_STATUS_LEN);
    }

    strncpy(order.strategyName, strategyName, stra::NAME_LEN);
    order.rebalance = rebalanceFlag;
    order.reduceOnly = reduceOnly;
    
    return order;

}

double PairOrder::CalculatePassiveSlippage(){
    double slippage = 0;
    // 滑点计算逻辑
    if (passiveTotalVolumeOnOrder > 0){
        if (passiveDirection == DT_LONG){
            slippage = passiveTotalPriceOnOrder / passiveTargetPrice - 1;
        } else{
            slippage = 1 - passiveTotalPriceOnOrder / passiveTargetPrice;
        }
    } else {
        slippage = 0;
    }
    return slippage;
}

void PairOrder::UpdatePairOrderByInsertOrder(const stra::QuantOrder& order) {
    if (order.isActiveOrder) {
        activeFrozenPrice = order.price;
        activeFrozenVolume += order.volume;
        sActiveOrder.insert(order.strategyOrderId);
    } else {
        if (passiveInfo.calculateType == 0) {
            passiveFrozenPrice = (passiveFrozenVolume * passiveFrozenPrice + order.price * order.volume) / (passiveFrozenVolume + order.volume);
        } else if (passiveInfo.calculateType == 1) {
            passiveFrozenPrice = 1 / ((1 / passiveFrozenPrice * passiveFrozenVolume + 1 / order.price * order.volume) / (passiveFrozenVolume + order.volume));
        }
        passiveFrozenVolume += order.volume;
        sPassiveOrder.insert(order.strategyOrderId);
    }
}

void PairOrder::UpdatePairOrderByDeleteOrder(const stra::QuantOrder& order) {
    if (order.isActiveOrder) {
        double leftVolume = order.volume - order.totalVolumeOnOrder;
        if (fabs(activeFrozenVolume - leftVolume) > stra::MIN_FLOAT) {
            if (activeInfo.calculateType == 0) {
                activeFrozenPrice = (activeFrozenPrice * activeFrozenVolume - order.price * leftVolume) / (activeFrozenVolume - leftVolume);
            } else if (activeInfo.calculateType == 1) {
                activeFrozenPrice = 1 / ((1 / activeFrozenPrice * activeFrozenVolume - 1 / order.price * leftVolume) / (activeFrozenVolume - leftVolume));
            }
            activeFrozenVolume -= leftVolume;
        } else {
            activeFrozenPrice = -1.0;
            activeFrozenVolume = 0;
        }
        sActiveOrder.erase(order.strategyOrderId);
    } else {
        double leftVolume = order.volume - order.totalVolumeOnOrder;
        if (fabs(passiveFrozenVolume - leftVolume) > stra::MIN_FLOAT) {
            if (passiveInfo.calculateType == 0) {
                passiveFrozenPrice = (passiveFrozenPrice * passiveFrozenVolume - order.price * leftVolume) / (passiveFrozenVolume - leftVolume);
            } else if (passiveInfo.calculateType == 1) {
                passiveFrozenPrice = 1 / ((1 / passiveFrozenPrice * passiveFrozenVolume - 1 / order.price * leftVolume) / (passiveFrozenVolume - leftVolume));
            }
            passiveFrozenVolume -= leftVolume;
        } else {
            passiveFrozenPrice = -1.0;
            passiveFrozenVolume = 0;
        }
        sPassiveOrder.erase(order.strategyOrderId);
    }
}

void PairOrder::UpdatePairOrderByOrder(const stra::QuantOrder& order) {
    if (order.isActiveOrder) {
        activeLastPriceOnOrder = order.tradePrice;
        activeLastVolumeOnOrder = order.tradeVolume;
        activeTotalPriceOnOrder = order.totalPriceOnOrder;
        activeTotalVolumeOnOrder = order.totalVolumeOnOrder;
        if (activeInfo.calculateType == 0) {
            if (fabs(activeFrozenVolume - order.tradeVolume) > stra::MIN_FLOAT) {
                activeFrozenPrice = (activeFrozenPrice * activeFrozenVolume - order.price * order.tradeVolume) / (activeFrozenVolume - order.tradeVolume);
                activeFrozenVolume -= order.tradeVolume;
            } else {
                activeFrozenPrice = -1;
                activeFrozenVolume = 0;
            }
        } else if (activeInfo.calculateType == 1) {
            if (fabs(activeFrozenVolume - order.tradeVolume) > stra::MIN_FLOAT) {
                activeFrozenPrice = 1 / ((1 / activeFrozenPrice * activeFrozenVolume - 1 / order.price * order.tradeVolume) / (activeFrozenVolume - order.tradeVolume));
                activeFrozenVolume -= order.tradeVolume;
            } else {
                activeFrozenPrice = -1;
                activeFrozenVolume = 0;
            }
        }
    }  else {
        passiveLastPriceOnOrder = order.tradePrice;
        passiveLastVolumeOnOrder = order.tradeVolume;
        if (passiveInfo.calculateType == 0) {
            if (order.tradeVolume > 0) {
                passiveTotalPriceOnOrder = (passiveTotalPriceOnOrder * passiveTotalVolumeOnOrder + order.tradePrice * order.tradeVolume) / (passiveTotalVolumeOnOrder + order.tradeVolume);
            }
            if (fabs(passiveFrozenVolume - order.tradeVolume) > stra::MIN_FLOAT) {
                passiveFrozenPrice = (passiveFrozenPrice * passiveFrozenVolume - order.price * order.tradeVolume) / (passiveFrozenVolume - order.tradeVolume);
                passiveFrozenVolume -= order.tradeVolume;
            } else {
                passiveFrozenPrice = -1;
                passiveFrozenVolume = 0;
            }
        } else if (passiveInfo.calculateType == 1) {
            if (order.tradeVolume > 0) {
                passiveTotalPriceOnOrder = 1 / ((1 / passiveTotalPriceOnOrder * passiveTotalVolumeOnOrder + 1 / order.tradePrice * order.tradeVolume) / (passiveTotalVolumeOnOrder + order.tradeVolume));
            }
            if (fabs(passiveFrozenVolume - order.tradeVolume) > stra::MIN_FLOAT) {
                passiveFrozenPrice = 1 / ((1 / passiveFrozenPrice * passiveFrozenVolume - 1 / order.price * order.tradeVolume) / (passiveFrozenVolume - order.tradeVolume));
                passiveFrozenVolume -= order.tradeVolume;
            } else {
                passiveFrozenPrice = -1;
                passiveFrozenVolume = 0;
            }
        }
        passiveTotalVolumeOnOrder += order.tradeVolume;
    }
}

string PairOrder::GetStr() {
    string activeOrderId = "";
    for (auto iter = sActiveOrder.begin(); iter != sActiveOrder.end(); ++iter) {
        string sId = fmt::format("{}|", *iter);
        activeOrderId += sId;
    }

    string passiveOrderId = "";
    for (auto iter = sPassiveOrder.begin(); iter != sPassiveOrder.end(); ++iter) {
        string sId = fmt::format("{}|", *iter);
        passiveOrderId += sId;
    }
    /*
    string s = fmt::format("{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{},{},"
                          "{},{},{},{},{},{}", 
                          pairId, algoPairId, strategyName, baseAsset, targetVolume, tradingTypeOrder, tradingTypeOffset, 
                          activeInstrumentKey, activeInstrument, activeInstType, activeExchangeType, activeDirection, activeOrderType, activePriceType, 
                          activePricePct, activeAccountId, activeTargetPrice, activeFrozenPrice, activeFrozenVolume, activeLastPriceOnOrder, activeLastVolumeOnOrder,
                          activeTotalPriceOnOrder, activeTotalVolumeOnOrder, activeOrderId, activePriceTickFlag, activePriceTickNum, 
                          passiveInstrumentKey, passiveInstrument, passiveInstType, passiveExchangeType, passiveDirection, passiveOrderType, passivePriceType, 
                          passivePricePct, passiveAccountId, passiveTargetPrice, passiveFrozenPrice, passiveFrozenVolume, passiveLastPriceOnOrder, passiveLastVolumeOnOrder, 
                          passiveTotalPriceOnOrder, passiveTotalVolumeOnOrder, passiveOrderId, passivePriceTickFlag, passivePriceTickNum, rebalanceFlag, pairActiveTotalPrice, 
                          pairTotalVolume, pairPassiveTotalPrice, pairActiveFeeAmount, pairPassiveFeeAmount, activeBidPrice1, activeBidVolume1, activeAskPrice1, 
                          activeAskVolume1, passiveBidPrice1, passiveBidVolume1, passiveAskPrice1, passiveAskVolume1, status);
    return s;
    */
    return "";
}

PairOrderManager::PairOrderManager() {

}

PairOrderManager::~PairOrderManager() {
    mPairOrder.clear();
}

void PairOrderManager::RecoveryFromFile(string filePath) {
    ifstream f;
    f.open(filePath.c_str(), ios::in);
    if (f.is_open()) {
        string line;
        while (getline(f, line)) {
            vector<string> v;
            splitString(line, v, ",");
            if (v.size() >= 60) {
                PairOrder order;
                order.pairId = stoll(v[0]);
                order.algoPairId = stoll(v[1]);
                strncpy(order.strategyName, v[2].c_str(), stra::NAME_LEN);
                strncpy(order.baseAsset, v[3].c_str(), stra::ASSET_LEN);
                order.targetVolume = stod(v[4]);
                order.tradingTypeOrder = stra::TradingType(stoi(v[5]));
                order.tradingTypeOffset = stra::TradingType(stoi(v[6]));
                strncpy(order.activeInstrumentKey, v[7].c_str(), stra::INST_KEY_LEN);
                strncpy(order.activeInstrument, v[8].c_str(), stra::INST_ID_LEN);
                order.activeInstType = InstType(stoi(v[9]));
                order.activeExchangeType = ExchangeType(stoi(v[10]));
                order.activeDirection = Direction(stoi(v[11]));
                order.activeOrderType = OrderType(stoi(v[12]));
                order.activePriceType = stra::PriceType(stoi(v[13]));
                order.activePricePct = stod(v[14]);
                order.activeAccountId = stoi(v[15]);
                order.activeTargetPrice = stod(v[16]);
                order.activeFrozenPrice = stod(v[17]);
                order.activeFrozenVolume = stod(v[18]);
                order.activeLastPriceOnOrder = stod(v[19]);
                order.activeLastVolumeOnOrder = stod(v[20]);
                order.activeTotalPriceOnOrder = stod(v[21]);
                order.activeTotalVolumeOnOrder = stod(v[22]);

                vector<string> vActiveOrderId;
                splitString(v[23], vActiveOrderId, "|");
                for (size_t i = 0; i < vActiveOrderId.size(); ++i) {
                    if (vActiveOrderId[i].length() > 0) {
                        order.sActiveOrder.insert(stoll(vActiveOrderId[i]));
                    }
                }

                order.activePriceTickFlag = bool(stoi(v[24]));
                order.activePriceTickNum = stoi(v[25]);
                strncpy(order.passiveInstrumentKey, v[26].c_str(), stra::INST_KEY_LEN);
                strncpy(order.passiveInstrument, v[27].c_str(), stra::INST_ID_LEN);
                order.passiveInstType = InstType(stoi(v[28]));
                order.passiveExchangeType = ExchangeType(stoi(v[29]));
                order.passiveDirection = Direction(stoi(v[30]));
                order.passiveOrderType = OrderType(stoi(v[31]));
                order.passivePriceType = stra::PriceType(stoi(v[32]));
                order.passivePricePct = stod(v[33]);
                order.passiveAccountId = stoi(v[34]);
                order.passiveTargetPrice = stod(v[35]);
                order.passiveFrozenPrice = stod(v[36]);
                order.passiveFrozenVolume = stod(v[37]);
                order.passiveLastPriceOnOrder = stod(v[38]);
                order.passiveLastVolumeOnOrder = stod(v[39]);
                order.passiveTotalPriceOnOrder = stod(v[40]);
                order.passiveTotalVolumeOnOrder = stod(v[41]);

                vector<string> vPassiveOrderId;
                splitString(v[42], vPassiveOrderId, "|");
                for (size_t i = 0; i < vPassiveOrderId.size(); ++i) {
                    if (vPassiveOrderId[i].length() > 0) {
                        order.sPassiveOrder.insert(stoll(vPassiveOrderId[i]));
                    }
                }

                order.passivePriceTickFlag = bool(stoi(v[43]));
                order.passivePriceTickNum = stoi(v[44]);

                order.rebalanceFlag = bool(stoi(v[45]));

                order.pairActiveTotalPrice = stod(v[46]);
                order.pairTotalVolume = stod(v[47]);
                order.pairPassiveTotalPrice = stod(v[48]);
                order.pairActiveFeeAmount = stod(v[49]);
                order.pairPassiveFeeAmount = stod(v[50]);
                order.activeBidPrice1 = stod(v[51]);
                order.activeBidVolume1 = stod(v[52]);
                order.activeAskPrice1 = stod(v[53]);
                order.activeAskVolume1 = stod(v[54]);
                order.passiveBidPrice1 = stod(v[55]);
                order.passiveBidVolume1 = stod(v[56]);
                order.passiveAskPrice1 = stod(v[57]);
                order.passiveAskVolume1 = stod(v[58]);
                order.status = stoi(v[59]);

                if (order.status == 0) {  // 未结束pairorder
                    InsertPairOrderByPairOrder(order);
                }
                
            }
        }
    }
}

void PairOrderManager::InsertPairOrderByPairOrder(const PairOrder& pairOrder) {
    mPairOrder[pairOrder.pairId] = pairOrder;
}

void PairOrderManager::DeletePairOrderByPairOrder(const PairOrder& pairOrder) {
    DeletePairOrderByPairId(pairOrder.pairId);
}

PairOrder& PairOrderManager::SelectPairOrderByPairId(int64_t pairId) {
    auto iter = mPairOrder.find(pairId);
    if (iter != mPairOrder.end()) {
        return iter->second;
    } else {
        return defaultPairOrder;
    }
}

void PairOrderManager::DeletePairOrderByPairId(int64_t pairId) {
    auto it = mPairOrder.find(pairId);
    if (it != mPairOrder.end()) {
        mPairOrder.erase(it);
    }
}

void PairOrderManager::UpdatePairOrderByOrder(const stra::QuantOrder& order) {
    auto& pairOrder = mPairOrder[order.pairId];
    pairOrder.UpdatePairOrderByOrder(order);
}

int PairOrderManager::GetSizeByOrderType(stra::TradingType tradingType) {
    int count = 0;
    for (auto it = mPairOrder.begin(); it != mPairOrder.end(); ++it) {
        if (it->second.tradingTypeOrder == tradingType) {
            count++;
        }
    }
    return count;
}

int PairOrderManager::GetSizeByOrderTypeAndActiveDirection(stra::TradingType tradingType, Direction activeDirection) {
    // OpenShort CloseLong activeDirection = Long
    // OpenLong CloseShort activeDirection = Short
    int count = 0;
    if (activeDirection == DT_LONG) {
        for (auto it = mPairOrder.begin(); it != mPairOrder.end(); ++it) {
            if (it->second.tradingTypeOrder == tradingType && (it->second.tradingTypeOffset == stra::OPEN_SHORT|| it->second.tradingTypeOffset == stra::CLOSE_LONG)) {
                count++;
            }
        }
    } else {
        for (auto it = mPairOrder.begin(); it != mPairOrder.end(); ++it) {
            if (it->second.tradingTypeOrder == tradingType && (it->second.tradingTypeOffset == stra::OPEN_LONG|| it->second.tradingTypeOffset == stra::CLOSE_SHORT)) {
                count++;
            }
        }
    }
    return count;
}


unordered_map<int64_t, PairOrder>& PairOrderManager::GetAllPairOrders() {
    return mPairOrder;
}
