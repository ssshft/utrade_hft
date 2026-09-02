#include "BaseAlgoOrder.h"
#include "SpreadManager.h"
#include "AccountManager.h"
#include "QuantTrade.h"
#include "QuantPub.h"
#include "BasicInfoMgr.h"
#include "Convert.h"
#include "StrategyConfig.h"


RQUEUE rLarkMsg;
CONTENTQUEUE contentQueue;


BaseAlgoOrder::BaseAlgoOrder() {
    systemDelayTimeSpan = 0;
    exchangeDelayTimeSpan = 0;
    systemDelayFlag = false;
    exchangeDelayFlag = false;
    fundVerifyFailedFlag = false;
    mtSlipage = 0;
    ttSlipage = 0;
    mtSlipageFlag = false;
    ttSlipageFlag = false;

    mtSpread = 0;
    ttSpread = 0;
    mtSpreadFlag = false;
    ttSpreadFlag = false;

    activePriceTickFlag = false;
    activePriceTickNum = 0;
    passivePriceTickFlag = false;
    passivePriceTickNum = 0;

    mtPriceTrendProtectFlag = false;  // 价格短期形成趋势是否停止不利交易
    ttPriceTrendProtectFlag = false;  // 价格短期形成趋势是否停止不利交易
    activeDepthMakerCheck = false; // 似乎未使用，用于检查盘口是否满足报单条件
    activeDepthTakerCheck = false; // 似乎未使用，用于检查盘口是否满足报单条件
    // algoPairOrder.activeDepthMakerCheckType = stra::CheckType_GE_VOLUME;
    // unordered_map<string, int> mActiveDepthMakerCheckTarget;
    // stra::CheckType activeDepthTakerCheckType;
    // unordered_map<string, int> mActiveDepthTakerCheckTarget;
    passiveDepthMakerCheck = false; // 似乎未使用，用于检查盘口是否满足报单条件
    passiveDepthTakerCheck = false; // 似乎未使用，用于检查盘口是否满足报单条件
    // stra::CheckType passiveDepthMakerCheckType;
    // unordered_map<string, double> mPassiveDepthMakerCheckTarget;  // 固定参数
    // stra::CheckType passiveDepthTakerCheckType;
    // unordered_map<string, double> mPassiveDepthTakerCheckTarget;
    int64_t oneSecond = 1000 * 1000;
    passiveVolumePct = 0.5;
    activeMakerCancelOrderTime = 300 * oneSecond;
    activeTakerCancelOrderTime = 5 * oneSecond;
    passiveMakerCancelOrderTime = 60 * oneSecond;
    passiveTakerCancelOrderTime = 5 * oneSecond;
    activeMakerFeeRate = 0;
    activeTakerFeeRate = 0;
    passiveMakerFeeRate = 0;
    passiveTakerFeeRate = 0;
    activeTakerSlippage = 0;
    activeMakerSlippage = 0;
    passiveTakerSlippage = 0;
    passiveMakerSlippage = 0;
    pairActiveTotalPrice = -1;
    pairTotalVolume = 0;
    pairPassiveTotalPrice = -1;
    pairPassiveTotalVolume = 0;
    takerTakerFs = 0;
    makerTakerFs = 0;
    maxMTOrderSize = 1;
    maxTTOrderSize = 1;
    targetSpreadType = stra::TargetSpredPrice_NOW;
    activeVolumeCalcualteType = stra::ActiveVolumeCalcualteType_PassiveVolumePct;
    ttTargetVolume = 0;
    mtTargetVolume = 0;
    minVolume = 0;
    profitSwitch = false;
    profitPct = 0;
    ttOLSwitch = false;
    ttCLSwitch = false;
    ttOSSwitch = false;
    ttCSSwitch = false;
    mtOLSwitch = false;
    mtCLSwitch = false;
    mtOSSwitch = false;
    mtCSSwitch = false;
    // algoPairOrder.rebalanceFlag = true;
    mtRebalanceSwitch = true; // 是否进行rebalance模式的创建被动腿订单
    ttRebalanceSwitch = true; // 是否进行rebalance模式的创建被动腿订单
    mtRebalanceFlag = true; // 是否进行rebalance模式的创建被动腿订单
    ttRebalanceFlag = true; // 是否进行rebalance模式的创建被动腿订单

    minOrderAmount = 100;

    isManual = false;

}

void BaseAlgoOrder::Init(sm::SecurityManager* s) {
    vector<string> vActive;
    splitString(activeInstrumentKey, vActive, ".");

    vector<string> vPassive;
    splitString(passiveInstrumentKey, vPassive, ".");

    smc->get_instrument_info(ExchangeTypeStr2EnumMap[vActive[0]], InstTypeStr2EnumMap[vActive[1]], vActive[2].c_str(), activeInfo);
    smc->get_instrument_info(ExchangeTypeStr2EnumMap[vPassive[0]], InstTypeStr2EnumMap[vPassive[1]], vPassive[2].c_str(), passiveInfo);

    InitPositionMgr();

    double minAmount = StrategyConfig::GetInstance().GetMinOrderAmount();
    if (minAmount > 0) {
        minOrderAmount = minAmount;
    }

    tradesDelayThreshold = StrategyConfig::GetInstance().GetTradesThreshold() * 1000;

    smc = s;
}

void BaseAlgoOrder::InitPositionMgr() {
    posMgrMakerTaker.Init(smc);
    posMgrMakerTaker.SetBaseAsset(baseAsset);
    //posMgrMakerTaker.LoadFromFile("maker_taker.json");

    posMgrTakerTaker.Init(smc);
    posMgrTakerTaker.SetBaseAsset(baseAsset);
    //posMgrTakerTaker.LoadFromFile("taker_taker.json");
}

void BaseAlgoOrder::Update() {
    smc->get_instrument_info(ExchangeTypeStr2EnumMap[vActive[0]], InstTypeStr2EnumMap[vActive[1]], vActive[2].c_str(), activeInfo);
    smc->get_instrument_info(ExchangeTypeStr2EnumMap[vPassive[0]], InstTypeStr2EnumMap[vPassive[1]], vPassive[2].c_str(), passiveInfo);
}

void BaseAlgoOrder::UpdateAlgoPairOrderByInsertQuantOrder(const stra::QuantOrder& order) {
    orderMgr.InsertOrderByOrder(order);
    PairOrder& pairOrder = pairOrderMgr.SelectPairOrderByPairId(order.pairId);
    if (pairOrder.algoPairId > 0) {
        pairOrder.UpdatePairOrderByInsertOrder(order);
    }

    if (order.tradingType == stra::MAKER_TAKER) {
        posMgrMakerTaker.OnInsertOrder(order);
    } else if (order.tradingType == stra::TAKER_TAKER) {
        posMgrTakerTaker.OnInsertOrder(order);
    }
}

void BaseAlgoOrder::UpdateAlgoPairOrderByDeleteQuantOrder(const stra::QuantOrder& order) {
    orderMgr.DeleteOrderByOrder(order);
    PairOrder& pairOrder = pairOrderMgr.SelectPairOrderByPairId(order.pairId);
    if (pairOrder.algoPairId > 0) {
        pairOrder.UpdatePairOrderByDeleteOrder(order);
    }
   
    if (order.tradingType == stra::MAKER_TAKER) {
        posMgrMakerTaker.OnDeleteOrder(order);
    } else if (order.tradingType == stra::TAKER_TAKER) {
        posMgrTakerTaker.OnDeleteOrder(order);
    }
}

void BaseAlgoOrder::UpdateAlgoPairOrderByQuantOrder(const stra::QuantOrder& order) {
    PairOrder& pairOrder = pairOrderMgr.SelectPairOrderByPairId(order.pairId);
    if (pairOrder.algoPairId > 0) {
        pairOrder.UpdatePairOrderByOrder(order);
    }
    if (order.tradingType == stra::MAKER_TAKER) {
        posMgrMakerTaker.OnOrder(order);
    } else if (order.tradingType == stra::TAKER_TAKER) {
        posMgrTakerTaker.OnOrder(order);
    }
}

void BaseAlgoOrder::UpdateAlgoPairOrderByPairOrder(PairOrder& pairOrder) {
    updateTime = crypto::getCurrentTime();
    if (pairOrder.activeTotalVolumeOnOrder > stra::MIN_FLOAT) {
        double activePrice = 0.0;
        double activeVolume = pairOrder.activeTotalVolumeOnOrder;
        double activeAmount = 0.0;
        double passivePrice = 0.0;
        if (pairOrder.activeDirection == DT_LONG) {
            if (pairOrder.activeOrderType == OT_POST_ONLY) {
                activePrice = pairOrder.activeTotalPriceOnOrder * (1 + activeMakerFeeRate);
            } else {
                activePrice = pairOrder.activeTotalPriceOnOrder * (1 + activeTakerFeeRate);
            }
        } else {
            if (pairOrder.activeOrderType == OT_POST_ONLY) {
                activePrice = pairOrder.activeTotalPriceOnOrder * (1 - activeMakerFeeRate);
            } else {
                activePrice = pairOrder.activeTotalPriceOnOrder * (1 - activeTakerFeeRate);
            }
        }

        activeAmount = GetAmountByVolumePrice(pairOrder.activeInfo, pairOrder.baseAsset, pairOrder.activeTotalVolumeOnOrder, pairOrder.activeTotalPriceOnOrder);
        double passiveVolume = GetVolumeByAmountPrice(pairOrder.passiveInfo, pairOrder.baseAsset, activeAmount, pairOrder.passiveTotalPriceOnOrder);
        if (pairOrder.passiveTotalPriceOnOrder <= stra::MIN_FLOAT) {
            if (pairOrder.passiveDirection == DT_LONG) {
                if (pairOrder.passiveOrderType == OT_POST_ONLY) {
                    passivePrice = pairOrder.passiveTargetPrice * (1 + passiveMakerSlippage) * (1 + passiveMakerFeeRate);
                } else {
                    passivePrice = pairOrder.passiveTargetPrice * (1 + passiveTakerSlippage) * (1 + passiveTakerFeeRate);
                }
            } else {
                if (pairOrder.passiveOrderType == OT_POST_ONLY) {
                    passivePrice = pairOrder.passiveTargetPrice * (1 - passiveMakerSlippage) * (1 - passiveMakerFeeRate);
                } else {
                    passivePrice = pairOrder.passiveTargetPrice * (1 + passiveTakerSlippage) * (1 - passiveTakerFeeRate);
                }
            }
        } else {
            if (pairOrder.passiveDirection == DT_LONG) {
                if (pairOrder.passiveOrderType == OT_POST_ONLY) {
                    passivePrice = pairOrder.passiveTotalPriceOnOrder * (1 + passiveMakerFeeRate);
                } else {
                    passivePrice = pairOrder.passiveTotalPriceOnOrder * (1 + passiveTakerFeeRate);
                }
            } else {
                if (pairOrder.passiveOrderType == OT_POST_ONLY) {
                    passivePrice = pairOrder.passiveTotalPriceOnOrder * (1 - passiveMakerFeeRate);
                } else {
                    passivePrice = pairOrder.passiveTotalPriceOnOrder * (1 - passiveTakerFeeRate);
                }
            }
        }

        double totalActivePrice = pairActiveTotalPrice;
        double totalActiveVolume = pairTotalVolume;
        double totalPassivePrice = pairPassiveTotalPrice;
        double totalActiveAmount = GetAmountByVolumePrice(pairOrder.activeInfo, pairOrder.baseAsset, pairTotalVolume, totalActivePrice);
        double totalPassiveVolume = fabs(GetVolumeByAmountPrice(pairOrder.passiveInfo, pairOrder.baseAsset, totalActiveAmount, totalPassivePrice)); 
        if (totalActiveVolume > stra::MIN_FLOAT) {
            if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume += activeVolume;
                if (activeInfo.calculateType == 0) {
                    pairActiveTotalPrice = (totalActivePrice * totalActiveVolume + activePrice * activeVolume) / (totalActiveVolume + activeVolume);
                } else if (activeInfo.calculateType == 1) {
                    pairActiveTotalPrice = 1 / ((1 / totalActivePrice * totalActiveVolume + 1 / activePrice * activeVolume) / (totalActiveVolume + activeVolume));
                }

                if (passiveInfo.calculateType == 0) {
                    pairPassiveTotalPrice = (totalPassivePrice * totalPassiveVolume + passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume);
                } else if (passiveInfo.calculateType == 1) {
                    pairPassiveTotalPrice = 1 / ((1 / totalPassivePrice * totalPassiveVolume + 1 / passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume));
                }
            } else if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume -= activeVolume;
                if (pairTotalVolume <= stra::MIN_FLOAT) {
                    pairActiveTotalPrice = activePrice;
                    pairPassiveTotalPrice = passivePrice;
                }
            }
        } else if (totalActiveVolume < -stra::MIN_FLOAT) {
            if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume -= activeVolume;
                if (activeInfo.calculateType == 0) {
                    pairActiveTotalPrice = (totalActivePrice * totalActiveVolume - activePrice * activeVolume) / (totalActiveVolume - activeVolume);
                } else if (activeInfo.calculateType == 1) {
                    pairActiveTotalPrice = 1 / ((1 / totalActivePrice * totalActiveVolume - 1 / activePrice * activeVolume) / (totalActiveVolume - activeVolume));
                }

                if (passiveInfo.calculateType == 0) {
                    pairPassiveTotalPrice = (totalPassivePrice * totalPassiveVolume + passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume);
                } else if (passiveInfo.calculateType == 1) {
                    pairPassiveTotalPrice = 1 / ((1 / totalPassivePrice * totalPassiveVolume + 1 / passivePrice * passiveVolume) / (totalPassiveVolume + passiveVolume));
                }
            } else if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume += activeVolume;
                if (pairTotalVolume > stra::MIN_FLOAT) {
                    pairActiveTotalPrice = activePrice;
                    pairPassiveTotalPrice = passivePrice;
                }
            }
        } else {
            if (pairOrder.activeDirection == DT_LONG) {
                pairTotalVolume = activeVolume;
                pairActiveTotalPrice = activePrice;
                pairPassiveTotalPrice = passivePrice;
                pairOrder.pairTotalVolume = activeVolume;
                pairOrder.pairActiveTotalPrice = activePrice;
                pairOrder.pairPassiveTotalPrice = passivePrice;
            } else if (pairOrder.activeDirection == DT_SHORT) {
                pairTotalVolume = -activeVolume;
                pairActiveTotalPrice = activePrice;
                pairPassiveTotalPrice = passivePrice;
                pairOrder.pairTotalVolume = -activeVolume;
                pairOrder.pairActiveTotalPrice = activePrice;
                pairOrder.pairPassiveTotalPrice = passivePrice;
            }
        }

    }
}

double BaseAlgoOrder::GetExpectActiveVolume() {
    stra::QuantAccount& accountMakerTaker = posMgrMakerTaker.GetAccount();
    stra::QuantAccount& accountTakerTaker = posMgrTakerTaker.GetAccount();
    double frozenActiveVolume = accountMakerTaker.mPosition[activeInstrumentKey].frozenLongPosition - accountMakerTaker.mPosition[activeInstrumentKey].frozenShortPosition;
    frozenActiveVolume += accountTakerTaker.mPosition[activeInstrumentKey].frozenLongPosition - accountTakerTaker.mPosition[activeInstrumentKey].frozenShortPosition;
    return pairTotalVolume + frozenActiveVolume;
}

double BaseAlgoOrder::GetExpectPassiveVolume() {
    stra::QuantAccount& accountMakerTaker = posMgrMakerTaker.GetAccount();
    stra::QuantAccount& accountTakerTaker = posMgrTakerTaker.GetAccount();
    double frozenPassiveVolume = accountMakerTaker.mPosition[passiveInstrumentKey].frozenLongPosition - accountMakerTaker.mPosition[passiveInstrumentKey].frozenShortPosition;
    frozenPassiveVolume += accountTakerTaker.mPosition[passiveInstrumentKey].frozenLongPosition - accountTakerTaker.mPosition[passiveInstrumentKey].frozenShortPosition;
    return pairPassiveTotalVolume + frozenPassiveVolume;
}

double BaseAlgoOrder::GetLockedSpread() {
    double spreadPrice = 0;
    if (fabs(pairPassiveTotalPrice) > stra::MIN_FLOAT && fabs(pairActiveTotalPrice) > stra::MIN_FLOAT) {
        spreadPrice = pairPassiveTotalPrice / pairActiveTotalPrice - 1;
    }
    
    return spreadPrice;
}

double BaseAlgoOrder::GetActiveVolumeByPassiveVolume(double volume, double price) {
    // 先计算被动腿盘口量对应的非基币的量
    double activeVolume = 0.0;
    double inbaseAssetAmount = 0.0;
    if (strcmp(passiveInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instRight.c_str(), "USDT") == 0 || strcmp(passiveInfo.instRight.c_str(), "USD") == 0 || strcmp(passiveInfo.instRight.c_str(), "USDC") == 0 || strcmp(passiveInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") || strcmp(baseAsset, "BUSD") == 0))) {
        if (passiveInfo.calculateType == 0) {
            inbaseAssetAmount =  volume * passiveInfo.multiple;
        } else if (passiveInfo.calculateType == 1) {
            inbaseAssetAmount = volume / price * passiveInfo.multiple;
        }
    } else if (strcmp(passiveInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(passiveInfo.instLeft.c_str(), "USDT") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USD") == 0 || strcmp(passiveInfo.instLeft.c_str(), "USDC") == 0 || strcmp(passiveInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") || strcmp(baseAsset, "BUSD") == 0))) { 
        if (passiveInfo.calculateType == 0) {
            inbaseAssetAmount = volume * price * passiveInfo.multiple;
        } else if (passiveInfo.calculateType == 1) {
            inbaseAssetAmount = volume * passiveInfo.multiple;
        }
    }
    if (strcmp(activeInfo.instRight.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instRight.c_str(), "USDT") == 0 || strcmp(activeInfo.instRight.c_str(), "USD") == 0 || strcmp(activeInfo.instRight.c_str(), "USDC") == 0 || strcmp(activeInfo.instRight.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") || strcmp(baseAsset, "BUSD") == 0))) {
        if (activeInfo.calculateType == 0) {
            activeVolume =  inbaseAssetAmount / activeInfo.multiple;
        } else if (activeInfo.calculateType == 1) {
            activeVolume = inbaseAssetAmount * price / activeInfo.multiple;
        }
    } else if (strcmp(activeInfo.instLeft.c_str(), baseAsset) == 0 || ((strcmp(activeInfo.instLeft.c_str(), "USDT") == 0 || strcmp(activeInfo.instLeft.c_str(), "USD") == 0 || strcmp(activeInfo.instLeft.c_str(), "USDC") == 0 || strcmp(activeInfo.instLeft.c_str(), "BUSD") == 0) && (strcmp(baseAsset, "USDT") == 0 || strcmp(baseAsset, "USD") == 0 || strcmp(baseAsset, "USDC") || strcmp(baseAsset, "BUSD") == 0))) { 
        if (activeInfo.calculateType == 0) {
            activeVolume = inbaseAssetAmount / price / activeInfo.multiple;
        } else if (activeInfo.calculateType == 1) {
            activeVolume = inbaseAssetAmount / activeInfo.multiple;
        }
    }
    return activeVolume;
}

void BaseAlgoOrder::CancelOrderOnSpread(const dbp::DbpData* pdata) {
    int64_t nowTime = crypto::getCurrentTime();
    if (algoOrderStatus == OS_CANCELLING) {
        auto& allOrders = orderMgr.GetAllOrders();
        for (auto it = allOrders.begin(); it != allOrders.end(); ++it) {
            //bool pass = LimitManager::Instance().PassLimit(it->second.strategyAccountId);
	        bool pass = LimitManager::Instance().PassCancelLimit(it->second.strategyAccountId);
            if (!pass) {
                continue;
            }

            if (it->second.isActiveOrder && (it->second.orderStatus == OS_NEW || it->second.orderStatus == OS_PARTFILLED || it->second.orderStatus == OS_FILLED)) {
                if (nowTime - it->second.updateTime > 1000 * 10) {
                    bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                    if (cancel_flag) {
                        orderMgr.UpdateOrderOnCancel(it->second);
			            WriteQuantOrder(it->second, pdata);
                        // 撤单成功报出去才能更新！！
                    }
                }
            }
        }
    } else {
        auto& allOrders = orderMgr.GetAllOrders();
        for (auto it = allOrders.begin(); it != allOrders.end(); ++it) {
            if (it->second.orderStatus == OS_NEW || it->second.orderStatus == OS_PARTFILLED || it->second.orderStatus == OS_FILLED){
                //bool pass = LimitManager::Instance().PassLimit(it->second.strategyAccountId);
                bool pass = LimitManager::Instance().PassCancelLimit(it->second.strategyAccountId);
		        if (!pass) {
                    continue;
                }

                PairOrder& pair_order = pairOrderMgr.SelectPairOrderByPairId(it->second.pairId);
                if (pair_order.pairId <= 0) {
                    continue;
                }
                if (it->second.isActiveOrder) {
                    // 主动腿订单撤单
                    if (it->second.orderType == OT_POST_ONLY){
                        // 主动腿Maker
                        // 时间撤单
                        if (nowTime - it->second.updateTime > activeMakerCancelOrderTime) {
			                if ((it->second.direction == DT_LONG && it->second.price < pdata->activeBidPrice[0] - stra::MIN_FLOAT) || (it->second.direction == DT_SHORT && it->second.price > pdata->activeAskPrice[0] + stra::MIN_FLOAT)) {
                                bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                                if (cancel_flag) {
                                    orderMgr.UpdateOrderOnCancel(it->second);
                    		        WriteQuantOrder(it->second, pdata);
                                    // 撤单成功报出去才能更新！！
                                }
                                continue;
                            }
                        }
                        // 被动腿价格变化撤单
                        bool passive_price_check = false;
                        if (pair_order.passiveOrderType == OT_POST_ONLY){
                            if (pair_order.passiveDirection == DT_LONG){
                                passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) > activePassiveCancelOrderPct;
                            } else {
                                passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) < -activePassiveCancelOrderPct;
                            }
                        } else {
                            if (pair_order.passiveDirection == DT_LONG){
                                passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) > activePassiveCancelOrderPct;
                            } else {
                                passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) < -activePassiveCancelOrderPct;
                            }
                        }
                        if (passive_price_check) {
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                        // 主动腿价格变化撤单
                        bool active_price_check = false;
                        if (pair_order.activeDirection == DT_LONG){
                            // active_price_check = (pdata->activeBidPrice[0] / pair_order.activeTargetPrice - 1) > activeMakerCancelOrderPct;
                            active_price_check = (pdata->activeBidPrice[0] / pair_order.activeBidPrice1 - 1) > activeMakerCancelOrderPct;
                        }else{
                            // active_price_check = (pdata->activeAskPrice[0] / pair_order.activeTargetPrice - 1) < -activeMakerCancelOrderPct;
                            active_price_check = (pdata->activeAskPrice[0] / pair_order.activeAskPrice1 - 1) < -activeMakerCancelOrderPct;
                        }
                        if (active_price_check){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }

                        // 大单成交撤单
                        if (pdata->exchActiveTradeDelay > tradesDelayThreshold || pdata->exchPassiveTradeDelay > tradesDelayThreshold) {
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                    } else {
                        // 主动腿Taker
                        // 时间撤单
                        if (nowTime - it->second.updateTime > activeTakerCancelOrderTime){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                        // 被动腿价格变化撤单
                        bool passive_price_check = false;
                        if (pair_order.passiveOrderType == OT_POST_ONLY){
                            if (pair_order.passiveDirection == DT_LONG){
                                passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) > activePassiveCancelOrderPct;
                            }else{
                                passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) < -activePassiveCancelOrderPct;
                            }
                        } else {
                            if (pair_order.passiveDirection == DT_LONG){
                                passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) > activePassiveCancelOrderPct;
                            }else{
                                passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) < -activePassiveCancelOrderPct;
                            }
                        }
                        if (passive_price_check){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                        // 主动腿价格变化撤单
                        bool active_price_check = false;
                        if (pair_order.activeDirection == DT_LONG){
                            // active_price_check = (pdata->activeAskPrice[0] / pair_order.activeTargetPrice - 1) > activeTakerCancelOrderPct;
                            active_price_check = (pdata->activeAskPrice[0] / pair_order.activeAskPrice1 - 1) > activeTakerCancelOrderPct;
                        }else{
                            // active_price_check = (pdata->activeBidPrice[0] / pair_order.activeTargetPrice - 1) < -activeTakerCancelOrderPct;
                            active_price_check = (pdata->activeBidPrice[0] / pair_order.activeBidPrice1 - 1) < -activeTakerCancelOrderPct;
                        }
                        if (active_price_check){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }

                        // 大单成交撤单
                        if (pdata->exchActiveTradeDelay > tradesDelayThreshold || pdata->exchPassiveTradeDelay > tradesDelayThreshold) {
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }

                    }

                }
                else {
                    // 被动腿订单撤单
                   // 主动腿订单撤单
                    if (it->second.orderType == OT_POST_ONLY) {
                        // 主动腿Maker
                        // 时间撤单
                        if (nowTime - it->second.updateTime > passiveMakerCancelOrderTime){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                        // 被动腿价格变化撤单
                        bool passive_price_check = false;
                        if (pair_order.passiveDirection == DT_LONG){
                            passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) > passiveMakerCancelOrderPct;
                        }else{
                            passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) < -passiveMakerCancelOrderPct;
                        }
                        if (passive_price_check){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }

                    } else {
                        // 主动腿Taker
                        // 时间撤单
                        if (nowTime - it->second.updateTime > passiveTakerCancelOrderTime){
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                        // 被动腿价格变化撤单
                        bool passive_price_check = false;
                        if (pair_order.passiveDirection == DT_LONG) {
                            passive_price_check = (pdata->passiveAskPrice[0] / pair_order.passiveTargetPrice - 1) > passiveTakerCancelOrderPct;
                        } else {
                            passive_price_check = (pdata->passiveBidPrice[0] / pair_order.passiveTargetPrice - 1) < -passiveTakerCancelOrderPct;
                        }
                        if (passive_price_check) {
                            bool cancel_flag = QuantTrade::Instance().CancelOrder(it->second);
                            if (cancel_flag) {
                                orderMgr.UpdateOrderOnCancel(it->second);
				                WriteQuantOrder(it->second, pdata);
                                // 撤单成功报出去才能更新！！
                            }
                            continue;
                        }
                    }
                }
            }
        }
    }
}

void BaseAlgoOrder::PairOrderTrade(PairOrder& pairOrder) {
    bool pass = LimitManager::Instance().PassLimit(pairOrder.passiveAccountId);
    if (!pass) {
        return;
    }
    
    stra::QuantOrder quant_order;
    int64_t strategyOrderId = GenerateStrategyOrderId();
    if (pairOrder.tradingTypeOrder == stra::MAKER_TAKER) {
        // rebalanceFlag需要配置, 含义是现在可以开始进行rebalance
        if (mtRebalanceFlag && mtRebalanceSwitch) {
            int cnt = pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER);
            if (cnt <= 1) {
                quant_order = pairOrder.CreatePassiveOrder(strategyOrderId, &posMgrMakerTaker);
            } else {
                quant_order = pairOrder.CreatePassiveOrder(strategyOrderId);
            }
        } else {
            quant_order = pairOrder.CreatePassiveOrder(strategyOrderId);
        }
    } else if (pairOrder.tradingTypeOrder == stra::TAKER_TAKER){
        if (ttRebalanceFlag && ttRebalanceSwitch) {
            int cnt = pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER);
            if (cnt <= 1){
                quant_order = pairOrder.CreatePassiveOrder(strategyOrderId, &posMgrTakerTaker);
                // if (fabs(pairOrder.activeTotalVolumeOnOrder * 100 -  pairOrder.passiveTotalVolumeOnOrder)> 1 && quant_order.strategyOrderId <= 0){
                //     quant_order = pairOrder.CreatePassiveOrder(strategyOrderId, &posMgrTakerTaker);
                // }
            } else {
                quant_order = pairOrder.CreatePassiveOrder(strategyOrderId);
            }
        } else {
            quant_order = pairOrder.CreatePassiveOrder(strategyOrderId);
        }
    } else{
        return;
    }

    if (quant_order.strategyOrderId <= stra::MIN_FLOAT) {
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



         if (activeFrozenValue <= stra::MIN_FLOAT && passiveFrozenValue <= stra::MIN_FLOAT) {
            // 这时候pairOrder已经完结，进行完结更新
            //LOG_INFO("start UpdateAlgoPairOrderByPairOrder!");
            UpdateAlgoPairOrderByPairOrder(pairOrder);
            pairOrder.status = 1;
            pairOrder.updateTime = crypto::getCurrentTime();
            string pairInstrumentKey = string(pairOrder.activeInstrumentKey) + "|" + string(pairOrder.passiveInstrumentKey);
            dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
            WritePairOrder(pairOrder, pdata);
            if (pairOrder.passiveTotalVolumeOnOrder > 0){
                // 存在成交, update状态
                string pubMsg = GeneratePubStrOnUpdate();
                QuantPub::Instance().Publish(pubMsg);
                WriteAlgoOrder(this);
            }
            if (pairOrder.rebalanceFlag && pairOrder.passiveTotalVolumeOnOrder > 0) {
                // 被动腿有成交且pairOrder完结,更改rebalance状态
                if (pairOrder.tradingTypeOrder == stra::MAKER_TAKER){
                    // 被动腿有成交且pairOrder完结,更改rebalance状态
                    mtRebalanceFlag = false;
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
                    ttRebalanceFlag = false;
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

                // 目标价差与实际价差离的太远，暂停交易
                
                if (pairOrder.activeTotalPriceOnOrder > 0 && pairOrder.passiveTotalPriceOnOrder > 0) {
                    double pairRealSpread = pairOrder.passiveTotalPriceOnOrder / pairOrder.activeTotalPriceOnOrder - 1;
                    if (pairOrder.tradingTypeOrder == stra::TAKER_TAKER) {
                        if (pairOrder.tradingTypeOffset == stra::OPEN_LONG) {
                            pairRealSpread += takerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::CLOSE_SHORT) {
                            pairRealSpread += takerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::OPEN_SHORT) {
                            pairRealSpread -= takerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::CLOSE_LONG) {
                            pairRealSpread -= takerTakerFs;
                        }

                        if (pairOrder.tradingTypeOffset == stra::OPEN_LONG || pairOrder.tradingTypeOffset == stra::CLOSE_SHORT) {
                            ttSpread = 0.8 * ttSpread  + 0.2 * (pairRealSpread - pairOrder.pairTargetSpread);
                            if (ttSpread > 0.0002 && ttSpread > pairOrder.pairTargetSpreadProfit / 2) {
                                ttSpreadFlag = true;
                                LOG_INFO("TargetSpreadStop TAKERTAKER tradingTypeOffset:%s strategyName:%s algoOrderId:%ld ttSpread:%.13f pairTargetSpreadProfit:%.13f pairTargetSpread:%.13f  pairRealSpread:%.13f", stra::TradingTypeEnum2Str[pairOrder.tradingTypeOffset].c_str(), algoStrategyName, algoOrderId, ttSpread, pairOrder.pairTargetSpreadProfit, pairOrder.pairTargetSpread, pairRealSpread);
                            }
                        }

                        if (pairOrder.tradingTypeOffset == stra::OPEN_SHORT || pairOrder.tradingTypeOffset == stra::CLOSE_LONG) {
                            ttSpread = 0.8 * ttSpread  + 0.2 * (pairOrder.pairTargetSpread - pairRealSpread);
                            if (ttSpread > 0.0002 && ttSpread > pairOrder.pairTargetSpreadProfit / 2) {
                                ttSpreadFlag = true;
                                LOG_INFO("TargetSpreadStop TAKERTAKER tradingTypeOffset:%s strategyName:%s algoOrderId:%ld ttSpread:%.13f pairTargetSpreadProfit:%.13f pairTargetSpread:%.13f  pairRealSpread:%.13f", stra::TradingTypeEnum2Str[pairOrder.tradingTypeOffset].c_str(), algoStrategyName, algoOrderId, ttSpread, pairOrder.pairTargetSpreadProfit, pairOrder.pairTargetSpread, pairRealSpread);
                            }
                        }
                    } else if (pairOrder.tradingTypeOrder == stra::MAKER_TAKER) {
                        if (pairOrder.tradingTypeOffset == stra::OPEN_LONG) {
                            pairRealSpread += makerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::CLOSE_SHORT) {
                            pairRealSpread += makerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::OPEN_SHORT) {
                            pairRealSpread -= makerTakerFs;
                        } else if (pairOrder.tradingTypeOffset == stra::CLOSE_LONG) {
                            pairRealSpread -= makerTakerFs;
                        }

                        if (pairOrder.tradingTypeOffset == stra::OPEN_LONG || pairOrder.tradingTypeOffset == stra::CLOSE_SHORT) {
                            mtSpread = 0.8 * mtSpread  + 0.2 * (pairRealSpread - pairOrder.pairTargetSpread);
                            if (mtSpread > 0.0002 && mtSpread > pairOrder.pairTargetSpreadProfit / 2) {
                                mtSpreadFlag = true;
                                LOG_INFO("TargetSpreadStop MAKERTAKER tradingTypeOffset:%s strategyName:%s algoOrderId:%ld mtSpread:%.13f pairTargetSpreadProfit:%.13f pairTargetSpread:%.13f  pairRealSpread:%.13f", stra::TradingTypeEnum2Str[pairOrder.tradingTypeOffset].c_str(), algoStrategyName, algoOrderId, mtSpread, pairOrder.pairTargetSpreadProfit, pairOrder.pairTargetSpread, pairRealSpread);
                            }
                        }

                        if (pairOrder.tradingTypeOffset == stra::OPEN_SHORT || pairOrder.tradingTypeOffset == stra::CLOSE_LONG) {
                            mtSpread = 0.8 * mtSpread  + 0.2 * (pairOrder.pairTargetSpread - pairRealSpread);
                            if (mtSpread > 0.0002 && mtSpread > pairOrder.pairTargetSpreadProfit / 2) {
                                mtSpreadFlag = true;
                                LOG_INFO("TargetSpreadStop MAKERTAKER tradingTypeOffset:%s strategyName:%s algoOrderId:%ld mtSpread:%.13f pairTargetSpreadProfit:%.13f pairTargetSpread:%.13f  pairRealSpread:%.13f", stra::TradingTypeEnum2Str[pairOrder.tradingTypeOffset].c_str(), algoStrategyName, algoOrderId, mtSpread, pairOrder.pairTargetSpreadProfit, pairOrder.pairTargetSpread, pairRealSpread);
                            }
                        }
                    }
                }
                
            }
            pairOrderMgr.DeletePairOrderByPairOrder(pairOrder);
        }
    } else {
        // 这时候pairOrder未完结，进行后续交易
        bool verify = AccountManager::Instance().FundVerify(quant_order, pairOrder.passiveInfo);
        if (verify) {
            // 通过验资正常报单
            bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
            string pairInstrumentKey = string(pairOrder.activeInstrumentKey) + "|" + string(pairOrder.passiveInstrumentKey);
            dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
            WriteQuantOrder(quant_order, pdata);
            if (orderFlag) {
                UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
            }
        } else {
            char msg[stra::MSG_LEN];
            sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey);
            rLarkMsg.Push(msg);
        }
    }
}

string BaseAlgoOrder::GetLastestStatusInfo() {
    dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pairInstrumentKey);
    double lockSpread = pairPassiveTotalPrice / pairActiveTotalPrice - 1;
    posMgrMakerTaker.CalcualtePnl(activeInstrumentKey);
    posMgrTakerTaker.CalcualtePnl(activeInstrumentKey);
    double makerTakerTotalPnl = posMgrMakerTaker.GetTotalPnl();
    double takerTakerTotalPnl = posMgrTakerTaker.GetTotalPnl();
    char s[stra::STR_LEN];
    sprintf(s, 
            "strategyName: %s  algoOrderId: %ld\n"
            "pairInstrumentKey:%s \n"
            "pairTotalVolume:%f  lockSpread:%f \n"
            "pairActiveTotalPrice:%f  pairPassiveTotalPrice:%f \n"
            "makerTakerPnl:%f \n"
            "takerTakerPnl:%f \n"
            "spreadBidAskTema:%f \n"
            "spreadBidBidTema:%f \n"
            "spreadAskBidTema:%f \n"
            "spreadAskAskTema:%f \n"
            "passiveAskPrice1:%f \n"
            "passiveAskVolume1:%f \n"
            "passiveBidPrice1:%f \n"
            "passiveBidVolume1:%f \n"
            "activeAskPrice1:%f \n"
            "activeAskVolume1:%f \n"
            "activeBidPrice1:%f \n"
            "activeBidVolume1:%f \n", 
            algoStrategyName, algoOrderId, pairInstrumentKey, pairTotalVolume, lockSpread, pairActiveTotalPrice, pairPassiveTotalPrice, makerTakerTotalPnl, takerTakerTotalPnl, 
            pdata->spreadBidAskTema, pdata->spreadBidBidTema, pdata->spreadAskBidTema, pdata->spreadAskAskTema, 
            pdata->passiveAskPrice[0], pdata->passiveAskVolume[0], pdata->passiveBidPrice[0], pdata->passiveBidVolume[0],
            pdata->activeAskPrice[0], pdata->activeAskVolume[0], pdata->activeBidPrice[0], pdata->activeBidVolume[0]);
    return string(s);
}

void BaseAlgoOrder::LoadFromFile(string filePath) {
  
}

void BaseAlgoOrder::SaveToFile() {

}

string BaseAlgoOrder::GeneratePubStr() {
    /*
    json pub;
    pub["sccId"] = string(sccId);
    pub["toAec"] = string(toAec);
    pub["clientOrderId"] = string(clientOrderId);
    pub["commandType"] = int(commandType);
    pub["insertTime"] = insertTime;

    json body;
    
    body["algoOrderId"] = algoOrderId;
    body["algoType"] = int(algoType);
    body["algoOrderStatus"] = int(algoOrderStatus);
    body["pairActiveTotalPrice"] = pairActiveTotalPrice;
    body["pairTotalVolume"] = pairTotalVolume;
    body["pairPassiveTotalPrice"] = pairPassiveTotalPrice;


    stra::QuantAccount& mtAcc = posMgrMakerTaker.GetAccount();

    json mtAccount;
    mtAccount["strategyAccountId"] = mtAcc.strategyAccountId;
    mtAccount["physicalAccountId"] = mtAcc.physicalAccountId;
    mtAccount["accountType"] = int(mtAcc.accountType);
    mtAccount["openRealLeverage"] = mtAcc.openRealLeverage;
    mtAccount["maxRealLeverage"] = mtAcc.maxRealLeverage;

    json mtAssetInfo;
    for (auto iter = mtAcc.mAsset.begin(); iter != mtAcc.mAsset.end(); ++iter) {
        json asset;
        asset["asset"] = iter->second.asset;
        asset["baseAsset"] = iter->second.baseAsset;
        asset["initAmount"] = iter->second.initAmount;
        asset["totalAmount"] = iter->second.totalAmount;
        asset["transferAmount"] = iter->second.transferAmount;
        asset["frozenAmount"] = iter->second.frozenAmount;
        asset["marginAmount"] = iter->second.marginAmount;
        asset["openMarginAmount"] = iter->second.openMarginAmount;
        asset["feeAmount"] = iter->second.feeAmount;
        asset["fundAmount"] = iter->second.fundAmount;
        asset["loanAmount"] = iter->second.loanAmount;
        asset["interestAmount"] = iter->second.interestAmount;
        asset["closeAmount"] = iter->second.closeAmount;
        asset["floatAmount"] = iter->second.floatAmount;
        asset["positionValue"] = iter->second.positionValue;
        mtAssetInfo[iter->second.asset] = asset;
    }

    mtAccount["assets"] = mtAssetInfo;

    
    json mtPositionInfo;
    for (auto iter = mtAcc.mPosition.begin(); iter != mtAcc.mPosition.end(); ++iter) {
        json position;
        position["instrumentKey"] = iter->second.instrumentKey;
        position["baseAsset"] = iter->second.baseAsset;
        position["longPosition"] = iter->second.longPosition;
        position["longAvgPrice"] = iter->second.longAvgPrice;
        position["shortPosition"] = iter->second.shortPosition;
        position["shortAvgPrice"] = iter->second.shortAvgPrice;
        position["floatAmount"] = iter->second.floatAmount;
        position["closeAmount"] = iter->second.closeAmount;
        position["positionValue"] = iter->second.positionValue;
        position["frozenLongPosition"] = iter->second.frozenLongPosition;
        position["frozenLongPrice"] = iter->second.frozenLongPrice;
        position["frozenShortPosition"] = iter->second.frozenShortPosition;
        position["frozenShortPrice"] = iter->second.frozenShortPrice;
        position["lastFloatAmount"] = iter->second.lastFloatAmount;
        position["lastPositionValue"] = iter->second.lastPositionValue;
        mtPositionInfo[iter->second.instrumentKey] = position;
    }


    mtAccount["positions"] = mtPositionInfo;

    posMgrMakerTaker.CalcualtePnl(activeInstrumentKey);
    unordered_map<string, double>& mMakerTakerPnl = posMgrMakerTaker.GetPnl();
    json mtPnl;
    for (auto iter = mMakerTakerPnl.begin(); iter != mMakerTakerPnl.end(); ++iter) {
        mtPnl[iter->first] = iter->second;
    }

    double totalMakerTakerPnl = posMgrMakerTaker.GetTotalPnl();
    mtAccount["totalPnl"] = totalMakerTakerPnl;
    mtAccount["pnl"] = mtPnl;

    body["MakerTakerPosition"] = mtAccount;

    stra::QuantAccount& ttAcc = posMgrTakerTaker.GetAccount();

    json ttAccount;
    ttAccount["strategyAccountId"] = ttAcc.strategyAccountId;
    ttAccount["physicalAccountId"] = ttAcc.physicalAccountId;
    ttAccount["accountType"] = int(ttAcc.accountType);
    ttAccount["openRealLeverage"] = ttAcc.openRealLeverage;
    ttAccount["maxRealLeverage"] = ttAcc.maxRealLeverage;

    json ttAssetInfo;
    for (auto iter = ttAcc.mAsset.begin(); iter != ttAcc.mAsset.end(); ++iter) {
        json asset;
        asset["asset"] = iter->second.asset;
        asset["baseAsset"] = iter->second.baseAsset;
        asset["initAmount"] = iter->second.initAmount;
        asset["totalAmount"] = iter->second.totalAmount;
        asset["transferAmount"] = iter->second.transferAmount;
        asset["frozenAmount"] = iter->second.frozenAmount;
        asset["marginAmount"] = iter->second.marginAmount;
        asset["openMarginAmount"] = iter->second.openMarginAmount;
        asset["feeAmount"] = iter->second.feeAmount;
        asset["fundAmount"] = iter->second.fundAmount;
        asset["loanAmount"] = iter->second.loanAmount;
        asset["interestAmount"] = iter->second.interestAmount;
        asset["closeAmount"] = iter->second.closeAmount;
        asset["floatAmount"] = iter->second.floatAmount;
        asset["positionValue"] = iter->second.positionValue;
        ttAssetInfo[iter->second.asset] = asset;
    }

    ttAccount["assets"] = ttAssetInfo;

    

    json ttPositionInfo;
    for (auto iter = ttAcc.mPosition.begin(); iter != ttAcc.mPosition.end(); ++iter) {
        json position;
        position["instrumentKey"] = iter->second.instrumentKey;
        position["baseAsset"] = iter->second.baseAsset;
        position["longPosition"] = iter->second.longPosition;
        position["longAvgPrice"] = iter->second.longAvgPrice;
        position["shortPosition"] = iter->second.shortPosition;
        position["shortAvgPrice"] = iter->second.shortAvgPrice;
        position["floatAmount"] = iter->second.floatAmount;
        position["closeAmount"] = iter->second.closeAmount;
        position["positionValue"] = iter->second.positionValue;
        position["frozenLongPosition"] = iter->second.frozenLongPosition;
        position["frozenLongPrice"] = iter->second.frozenLongPrice;
        position["frozenShortPosition"] = iter->second.frozenShortPosition;
        position["frozenShortPrice"] = iter->second.frozenShortPrice;
        position["lastFloatAmount"] = iter->second.lastFloatAmount;
        position["lastPositionValue"] = iter->second.lastPositionValue;
        ttPositionInfo[iter->second.instrumentKey] = position;
    }

    ttAccount["positions"] = ttPositionInfo;
    posMgrTakerTaker.CalcualtePnl(activeInstrumentKey);

    unordered_map<string, double>& mTakerTakerPnl = posMgrTakerTaker.GetPnl();
    json ttPnl;
    for (auto iter = mTakerTakerPnl.begin(); iter != mTakerTakerPnl.end(); ++iter) {
        ttPnl[iter->first] = iter->second;
    }

    double totalTakerTakerPnl = posMgrTakerTaker.GetTotalPnl();
    ttAccount["totalPnl"] = totalTakerTakerPnl;
    ttAccount["pnl"] = ttPnl;
    

    body["TakerTakerPosition"] = ttAccount;

    body["DB_mdChannels"] = json::array({pairInstrumentKey});

    pub["jsonBody"] = body;

    return pub.dump();
    */
    return "";
}

string BaseAlgoOrder::GeneratePubStrOnUpdate() {
    /*
    json pub;
    pub["sccId"] = string(sccId);
    pub["toAec"] = string(toAec);
    pub["clientOrderId"] = string(clientOrderId);
    pub["commandType"] = int(stra::CommandType_UPDATE);
    pub["insertTime"] = insertTime;

    json body;
    
    body["algoOrderId"] = algoOrderId;
    body["algoType"] = int(algoType);
    body["pairActiveTotalPrice"] = pairActiveTotalPrice;
    body["pairTotalVolume"] = pairTotalVolume;
    body["pairPassiveTotalPrice"] = pairPassiveTotalPrice;


    stra::QuantAccount& mtAcc = posMgrMakerTaker.GetAccount();

    json mtAccount;
    mtAccount["strategyAccountId"] = mtAcc.strategyAccountId;
    mtAccount["physicalAccountId"] = mtAcc.physicalAccountId;
    mtAccount["accountType"] = int(mtAcc.accountType);
    mtAccount["openRealLeverage"] = mtAcc.openRealLeverage;
    mtAccount["maxRealLeverage"] = mtAcc.maxRealLeverage;

    json mtAssetInfo;
    for (auto iter = mtAcc.mAsset.begin(); iter != mtAcc.mAsset.end(); ++iter) {
        json asset;
        asset["asset"] = iter->second.asset;
        asset["baseAsset"] = iter->second.baseAsset;
        asset["initAmount"] = iter->second.initAmount;
        asset["totalAmount"] = iter->second.totalAmount;
        asset["transferAmount"] = iter->second.transferAmount;
        asset["frozenAmount"] = iter->second.frozenAmount;
        asset["marginAmount"] = iter->second.marginAmount;
        asset["openMarginAmount"] = iter->second.openMarginAmount;
        asset["feeAmount"] = iter->second.feeAmount;
        asset["fundAmount"] = iter->second.fundAmount;
        asset["loanAmount"] = iter->second.loanAmount;
        asset["interestAmount"] = iter->second.interestAmount;
        asset["closeAmount"] = iter->second.closeAmount;
        asset["floatAmount"] = iter->second.floatAmount;
        asset["positionValue"] = iter->second.positionValue;
        mtAssetInfo[iter->second.asset] = asset;
    }

    mtAccount["assets"] = mtAssetInfo;

    
    json mtPositionInfo;
    for (auto iter = mtAcc.mPosition.begin(); iter != mtAcc.mPosition.end(); ++iter) {
        json position;
        position["instrumentKey"] = iter->second.instrumentKey;
        position["baseAsset"] = iter->second.baseAsset;
        position["longPosition"] = iter->second.longPosition;
        position["longAvgPrice"] = iter->second.longAvgPrice;
        position["shortPosition"] = iter->second.shortPosition;
        position["shortAvgPrice"] = iter->second.shortAvgPrice;
        position["floatAmount"] = iter->second.floatAmount;
        position["closeAmount"] = iter->second.closeAmount;
        position["positionValue"] = iter->second.positionValue;
        position["frozenLongPosition"] = iter->second.frozenLongPosition;
        position["frozenLongPrice"] = iter->second.frozenLongPrice;
        position["frozenShortPosition"] = iter->second.frozenShortPosition;
        position["frozenShortPrice"] = iter->second.frozenShortPrice;
        position["lastFloatAmount"] = iter->second.lastFloatAmount;
        position["lastPositionValue"] = iter->second.lastPositionValue;
        mtPositionInfo[iter->second.instrumentKey] = position;
    }


    mtAccount["positions"] = mtPositionInfo;

    posMgrMakerTaker.CalcualtePnl(activeInstrumentKey);
    unordered_map<string, double>& mMakerTakerPnl = posMgrMakerTaker.GetPnl();
    json mtPnl;
    for (auto iter = mMakerTakerPnl.begin(); iter != mMakerTakerPnl.end(); ++iter) {
        mtPnl[iter->first] = iter->second;
    }

    double totalMakerTakerPnl = posMgrMakerTaker.GetTotalPnl();
    mtAccount["totalPnl"] = totalMakerTakerPnl;
    mtAccount["pnl"] = mtPnl;

    body["MakerTakerPosition"] = mtAccount;

    stra::QuantAccount& ttAcc = posMgrTakerTaker.GetAccount();

    json ttAccount;
    ttAccount["strategyAccountId"] = ttAcc.strategyAccountId;
    ttAccount["physicalAccountId"] = ttAcc.physicalAccountId;
    ttAccount["accountType"] = int(ttAcc.accountType);
    ttAccount["openRealLeverage"] = ttAcc.openRealLeverage;
    ttAccount["maxRealLeverage"] = ttAcc.maxRealLeverage;

    json ttAssetInfo;
    for (auto iter = ttAcc.mAsset.begin(); iter != ttAcc.mAsset.end(); ++iter) {
        json asset;
        asset["asset"] = iter->second.asset;
        asset["baseAsset"] = iter->second.baseAsset;
        asset["initAmount"] = iter->second.initAmount;
        asset["totalAmount"] = iter->second.totalAmount;
        asset["transferAmount"] = iter->second.transferAmount;
        asset["frozenAmount"] = iter->second.frozenAmount;
        asset["marginAmount"] = iter->second.marginAmount;
        asset["openMarginAmount"] = iter->second.openMarginAmount;
        asset["feeAmount"] = iter->second.feeAmount;
        asset["fundAmount"] = iter->second.fundAmount;
        asset["loanAmount"] = iter->second.loanAmount;
        asset["interestAmount"] = iter->second.interestAmount;
        asset["closeAmount"] = iter->second.closeAmount;
        asset["floatAmount"] = iter->second.floatAmount;
        asset["positionValue"] = iter->second.positionValue;
        ttAssetInfo[iter->second.asset] = asset;
    }

    ttAccount["assets"] = ttAssetInfo;

    

    json ttPositionInfo;
    for (auto iter = ttAcc.mPosition.begin(); iter != ttAcc.mPosition.end(); ++iter) {
        json position;
        position["instrumentKey"] = iter->second.instrumentKey;
        position["baseAsset"] = iter->second.baseAsset;
        position["longPosition"] = iter->second.longPosition;
        position["longAvgPrice"] = iter->second.longAvgPrice;
        position["shortPosition"] = iter->second.shortPosition;
        position["shortAvgPrice"] = iter->second.shortAvgPrice;
        position["floatAmount"] = iter->second.floatAmount;
        position["closeAmount"] = iter->second.closeAmount;
        position["positionValue"] = iter->second.positionValue;
        position["frozenLongPosition"] = iter->second.frozenLongPosition;
        position["frozenLongPrice"] = iter->second.frozenLongPrice;
        position["frozenShortPosition"] = iter->second.frozenShortPosition;
        position["frozenShortPrice"] = iter->second.frozenShortPrice;
        position["lastFloatAmount"] = iter->second.lastFloatAmount;
        position["lastPositionValue"] = iter->second.lastPositionValue;
        ttPositionInfo[iter->second.instrumentKey] = position;
    }

    ttAccount["positions"] = ttPositionInfo;
    posMgrTakerTaker.CalcualtePnl(activeInstrumentKey);

    unordered_map<string, double>& mTakerTakerPnl = posMgrTakerTaker.GetPnl();
    json ttPnl;
    for (auto iter = mTakerTakerPnl.begin(); iter != mTakerTakerPnl.end(); ++iter) {
        ttPnl[iter->first] = iter->second;
    }

    double totalTakerTakerPnl = posMgrTakerTaker.GetTotalPnl();
    ttAccount["totalPnl"] = totalTakerTakerPnl;
    ttAccount["pnl"] = ttPnl;
    

    body["TakerTakerPosition"] = ttAccount;

    body["DB_mdChannels"] = json::array({pairInstrumentKey});

    pub["jsonBody"] = body;

    return pub.dump();
    */
    return "";
}

PairOrder BaseAlgoOrder::GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId) {
    PairOrder pairOrder;
    return pairOrder;
}

PairOrder BaseAlgoOrder::CreatePairOrder(stra::TradingType tradingType) {
    PairOrder pairOrder;
    return pairOrder;
}

PairOrder BaseAlgoOrder::CreatePairOrder(stra::TradingType tradingType, Direction activeDirection) {
    PairOrder pairOrder;
    return pairOrder;
}

bool BaseAlgoOrder::passInfoParameterCheck() {
    bool pass = true;
    if (activeInfo.tickSize < stra::MIN_FLOAT || activeInfo.lotSize < stra::MIN_FLOAT || activeInfo.minSize < stra::MIN_FLOAT) {
        pass = false;
    }

    if (passiveInfo.tickSize < stra::MIN_FLOAT || passiveInfo.lotSize < stra::MIN_FLOAT || passiveInfo.minSize < stra::MIN_FLOAT) {
        pass = false;
    } 
    
    return pass;
}
