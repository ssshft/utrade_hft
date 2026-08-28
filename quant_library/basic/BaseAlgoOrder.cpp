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

void BaseAlgoOrder::Init() {
    activeInfo = BasicInfoMgr::GetInstance().GetBasicInfo(activeInstrumentKey);
    passiveInfo = BasicInfoMgr::GetInstance().GetBasicInfo(passiveInstrumentKey);
    InitPositionMgr();

    double minAmount = StrategyConfig::GetInstance().GetMinOrderAmount();
    if (minAmount > 0) {
        minOrderAmount = minAmount;
    }

    tradesDelayThreshold = StrategyConfig::GetInstance().GetTradesThreshold() * 1000;
}

void BaseAlgoOrder::InitPositionMgr() {
    posMgrMakerTaker.SetBaseAsset(baseAsset);
    //posMgrMakerTaker.LoadFromFile("maker_taker.json");

    posMgrTakerTaker.SetBaseAsset(baseAsset);
    //posMgrTakerTaker.LoadFromFile("taker_taker.json");
}

void BaseAlgoOrder::Update() {
    activeInfo = BasicInfoMgr::GetInstance().GetBasicInfo(activeInstrumentKey);
    passiveInfo = BasicInfoMgr::GetInstance().GetBasicInfo(passiveInstrumentKey);
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
            pairOrder.updateTime = crypto::getCurentTime();
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
        double assetTick = 0.0;
        stra::InstrumentInfo info = pairOrder.passiveInfo;
        bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
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
    std::ifstream algoOrderFile(filePath.c_str());
	if (!algoOrderFile) {
		LOG_INFO("File: %s does not exist!", filePath.c_str());
		return;
	}

    json algoOrderInfo;
	algoOrderFile >> algoOrderInfo;

    auto i = algoOrderInfo.find("algoType");
    if (i != algoOrderInfo.end()) {
        algoType =  stra::AlgoTypeStr2Enum[string(i.value())];
    }
    
    i = algoOrderInfo.find("algoStrategyName");
    if (i != algoOrderInfo.end()) {
        strncpy(algoStrategyName, string(i.value()).c_str(), stra::NAME_LEN);
    }

    i = algoOrderInfo.find("algoOrderId");
    if (i != algoOrderInfo.end()) {
        algoOrderId = int64_t(i.value());
    }

    i = algoOrderInfo.find("pairInstrumentKey");
    if (i != algoOrderInfo.end()) {
        strncpy(pairInstrumentKey, string(i.value()).c_str(), stra::INST_KEY_LEN);
    }

    i = algoOrderInfo.find("baseAsset");
    if (i != algoOrderInfo.end()) {
        strncpy(baseAsset, string(i.value()).c_str(), stra::ASSET_LEN);
    }

    i = algoOrderInfo.find("algoOrderStatus");
    if (i != algoOrderInfo.end()) {
        algoOrderStatus =  stra::OrderStatusStr2Enum[string(i.value())];
    }


    i = algoOrderInfo.find("activeInstrumentKey");
    if (i != algoOrderInfo.end()) {
        strncpy(activeInstrumentKey, string(i.value()).c_str(), stra::INST_KEY_LEN);
    }

    i = algoOrderInfo.find("activePriceTakerPct");
    if (i != algoOrderInfo.end()) {
        activePriceTakerPct = double(i.value());
    }

    i = algoOrderInfo.find("activePriceMakerPct");
    if (i != algoOrderInfo.end()) {
        activePriceMakerPct = double(i.value());
    }

    i = algoOrderInfo.find("activeAccountId");
    if (i != algoOrderInfo.end()) {
        activeAccountId = int(i.value());
    }

    i = algoOrderInfo.find("activeDriveType");
    if (i != algoOrderInfo.end()) {
        activeDriveType =  stra::DriveTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("activeDepthMakerCheck");
    if (i != algoOrderInfo.end()) {
        activeDepthMakerCheck = bool(i.value());
    }

    i = algoOrderInfo.find("activeDepthTakerCheck");
    if (i != algoOrderInfo.end()) {
        activeDepthTakerCheck = bool(i.value());
    }

    i = algoOrderInfo.find("activeDepthMakerCheckType");
    if (i != algoOrderInfo.end()) {
        activeDepthMakerCheckType =  stra::CheckTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("activeDepthTakerCheckType");
    if (i != algoOrderInfo.end()) {
        activeDepthTakerCheckType =  stra::CheckTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("activeOrderType");
    if (i != algoOrderInfo.end()) {
        activeOrderType =  stra::OrderTypeStr2Enum[string(i.value())];
    }


    i = algoOrderInfo.find("passiveInstrumentKey");
    if (i != algoOrderInfo.end()) {
        strncpy(passiveInstrumentKey, string(i.value()).c_str(), stra::INST_KEY_LEN);
    }

    i = algoOrderInfo.find("passivePriceTakerPct");
    if (i != algoOrderInfo.end()) {
        passivePriceTakerPct = double(i.value());
    }

    i = algoOrderInfo.find("passivePriceMakerPct");
    if (i != algoOrderInfo.end()) {
        passivePriceMakerPct = double(i.value());
    }

    i = algoOrderInfo.find("passiveAccountId");
    if (i != algoOrderInfo.end()) {
        passiveAccountId = int(i.value());
    }

    i = algoOrderInfo.find("passiveDriveType");
    if (i != algoOrderInfo.end()) {
        passiveDriveType =  stra::DriveTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("passiveDepthMakerCheck");
    if (i != algoOrderInfo.end()) {
        passiveDepthMakerCheck = bool(i.value());
    }

    i = algoOrderInfo.find("passiveDepthTakerCheck");
    if (i != algoOrderInfo.end()) {
        passiveDepthTakerCheck = bool(i.value());
    }

    i = algoOrderInfo.find("passiveDepthMakerCheckType");
    if (i != algoOrderInfo.end()) {
        passiveDepthMakerCheckType =  stra::CheckTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("passiveDepthTakerCheckType");
    if (i != algoOrderInfo.end()) {
        passiveDepthTakerCheckType =  stra::CheckTypeStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("passiveOrderType");
    if (i != algoOrderInfo.end()) {
        passiveOrderType =  stra::OrderTypeStr2Enum[string(i.value())];
    }


    i = algoOrderInfo.find("passiveVolumePct");
    if (i != algoOrderInfo.end()) {
        passiveVolumePct = double(i.value());
    }

    i = algoOrderInfo.find("activeMakerCancelOrderTime");
    if (i != algoOrderInfo.end()) {
        activeMakerCancelOrderTime = int64_t(i.value());
    }

    i = algoOrderInfo.find("activeTakerCancelOrderTime");
    if (i != algoOrderInfo.end()) {
        activeTakerCancelOrderTime = int64_t(i.value());
    }

    i = algoOrderInfo.find("passiveMakerCancelOrderTime");
    if (i != algoOrderInfo.end()) {
        passiveMakerCancelOrderTime = int64_t(i.value());
    }

    i = algoOrderInfo.find("passiveTakerCancelOrderTime");
    if (i != algoOrderInfo.end()) {
        passiveTakerCancelOrderTime = int64_t(i.value());
    }

    i = algoOrderInfo.find("activePassiveCancelOrderPct");
    if (i != algoOrderInfo.end()) {
        activePassiveCancelOrderPct = double(i.value());
    }

    i = algoOrderInfo.find("activeMakerCancelOrderPct");
    if (i != algoOrderInfo.end()) {
        activeMakerCancelOrderPct = double(i.value());
    }

    i = algoOrderInfo.find("activeTakerCancelOrderPct");
    if (i != algoOrderInfo.end()) {
        activeTakerCancelOrderPct = double(i.value());
    }

    i = algoOrderInfo.find("passiveMakerCancelOrderPct");
    if (i != algoOrderInfo.end()) {
        passiveMakerCancelOrderPct = double(i.value());
    }

    i = algoOrderInfo.find("passiveTakerCancelOrderPct");
    if (i != algoOrderInfo.end()) {
        passiveTakerCancelOrderPct = double(i.value());
    }

    i = algoOrderInfo.find("activeMakerFeeRate");
    if (i != algoOrderInfo.end()) {
        activeMakerFeeRate = double(i.value());
    }

    i = algoOrderInfo.find("activeTakerFeeRate");
    if (i != algoOrderInfo.end()) {
        activeTakerFeeRate = double(i.value());
    }

    i = algoOrderInfo.find("passiveMakerFeeRate");
    if (i != algoOrderInfo.end()) {
        passiveMakerFeeRate = double(i.value());
    }

    i = algoOrderInfo.find("passiveTakerFeeRate");
    if (i != algoOrderInfo.end()) {
        passiveTakerFeeRate = double(i.value());
    }

    i = algoOrderInfo.find("activeTakerSlippage");
    if (i != algoOrderInfo.end()) {
        activeTakerSlippage = double(i.value());
    }

    i = algoOrderInfo.find("activeMakerSlippage");
    if (i != algoOrderInfo.end()) {
        activeMakerSlippage = double(i.value());
    }

    i = algoOrderInfo.find("passiveTakerSlippage");
    if (i != algoOrderInfo.end()) {
        passiveTakerSlippage = double(i.value());
    }

    i = algoOrderInfo.find("passiveMakerSlippage");
    if (i != algoOrderInfo.end()) {
        passiveMakerSlippage = double(i.value());
    }


    i = algoOrderInfo.find("pairActiveTotalPrice");
    if (i != algoOrderInfo.end()) {
        pairActiveTotalPrice = double(i.value());
    }

    i = algoOrderInfo.find("pairTotalVolume");
    if (i != algoOrderInfo.end()) {
        pairTotalVolume = double(i.value());
    }

    i = algoOrderInfo.find("pairPassiveTotalPrice");
    if (i != algoOrderInfo.end()) {
        pairPassiveTotalPrice = double(i.value());
    }

    i = algoOrderInfo.find("makerTakerFs");
    if (i != algoOrderInfo.end()) {
        makerTakerFs = double(i.value());
    }

    i = algoOrderInfo.find("takerTakerFs");
    if (i != algoOrderInfo.end()) {
        takerTakerFs = double(i.value());
    }

    i = algoOrderInfo.find("maxMTOrderSize");
    if (i != algoOrderInfo.end()) {
        maxMTOrderSize = double(i.value());
    }

    i = algoOrderInfo.find("maxTTOrderSize");
    if (i != algoOrderInfo.end()) {
        maxMTOrderSize = double(i.value());
    }


    i = algoOrderInfo.find("targetSpreadType");
    if (i != algoOrderInfo.end()) {
        targetSpreadType =  stra::TargetSpredPriceStr2Enum[string(i.value())];
    }

    i = algoOrderInfo.find("activeVolumeCalcualteType");
    if (i != algoOrderInfo.end()) {
        activeVolumeCalcualteType =  stra::ActiveVolumeCalcualteTypeStr2Enum[string(i.value())];
    }


    i = algoOrderInfo.find("ttTargetVolume");
    if (i != algoOrderInfo.end()) {
        ttTargetVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtTargetVolume");
    if (i != algoOrderInfo.end()) {
        mtTargetVolume = double(i.value());
    }

    i = algoOrderInfo.find("profitSwitch");
    if (i != algoOrderInfo.end()) {
        profitSwitch = bool(i.value());
    }

    i = algoOrderInfo.find("profitPct");
    if (i != algoOrderInfo.end()) {
        profitPct = double(i.value());
    }


    i = algoOrderInfo.find("ttOLStartSpread");
    if (i != algoOrderInfo.end()) {
        ttOLStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttOLEndSpread");
    if (i != algoOrderInfo.end()) {
        ttOLEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttOLStartVolume");
    if (i != algoOrderInfo.end()) {
        ttOLStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttOLEndVolume");
    if (i != algoOrderInfo.end()) {
        ttOLEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttOLSwitch");
    if (i != algoOrderInfo.end()) {
        ttOLSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("ttCLStartSpread");
    if (i != algoOrderInfo.end()) {
        ttCLStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttCLEndSpread");
    if (i != algoOrderInfo.end()) {
        ttCLEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttCLStartVolume");
    if (i != algoOrderInfo.end()) {
        ttCLStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttCLEndVolume");
    if (i != algoOrderInfo.end()) {
        ttCLEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttCLSwitch");
    if (i != algoOrderInfo.end()) {
        ttCLSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("ttOSStartSpread");
    if (i != algoOrderInfo.end()) {
        ttOSStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttOSEndSpread");
    if (i != algoOrderInfo.end()) {
        ttOSEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttOSStartVolume");
    if (i != algoOrderInfo.end()) {
        ttOSStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttOSEndVolume");
    if (i != algoOrderInfo.end()) {
        ttOSEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttOSSwitch");
    if (i != algoOrderInfo.end()) {
        ttOSSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("ttCSStartSpread");
    if (i != algoOrderInfo.end()) {
        ttCSStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttCSEndSpread");
    if (i != algoOrderInfo.end()) {
        ttCSEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("ttCSStartVolume");
    if (i != algoOrderInfo.end()) {
        ttCSStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttCSEndVolume");
    if (i != algoOrderInfo.end()) {
        ttCSEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("ttCSSwitch");
    if (i != algoOrderInfo.end()) {
        ttCSSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("mtOLStartSpread");
    if (i != algoOrderInfo.end()) {
        mtOLStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtOLEndSpread");
    if (i != algoOrderInfo.end()) {
        mtOLEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtOLStartVolume");
    if (i != algoOrderInfo.end()) {
        mtOLStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtOLEndVolume");
    if (i != algoOrderInfo.end()) {
        mtOLEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtOLSwitch");
    if (i != algoOrderInfo.end()) {
        mtOLSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("mtCLStartSpread");
    if (i != algoOrderInfo.end()) {
        mtCLStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtCLEndSpread");
    if (i != algoOrderInfo.end()) {
        mtCLEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtCLStartVolume");
    if (i != algoOrderInfo.end()) {
        mtCLStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtCLEndVolume");
    if (i != algoOrderInfo.end()) {
        mtCLEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtCLSwitch");
    if (i != algoOrderInfo.end()) {
        mtCLSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("mtOSStartSpread");
    if (i != algoOrderInfo.end()) {
        mtOSStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtOSEndSpread");
    if (i != algoOrderInfo.end()) {
        mtOSEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtOSStartVolume");
    if (i != algoOrderInfo.end()) {
        mtOSStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtOSEndVolume");
    if (i != algoOrderInfo.end()) {
        mtOSEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtOSSwitch");
    if (i != algoOrderInfo.end()) {
        mtOSSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("mtCSStartSpread");
    if (i != algoOrderInfo.end()) {
        mtCSStartSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtCSEndSpread");
    if (i != algoOrderInfo.end()) {
        mtCSEndSpread = double(i.value());
    }

    i = algoOrderInfo.find("mtCSStartVolume");
    if (i != algoOrderInfo.end()) {
        mtCSStartVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtCSEndVolume");
    if (i != algoOrderInfo.end()) {
        mtCSEndVolume = double(i.value());
    }

    i = algoOrderInfo.find("mtCSSwitch");
    if (i != algoOrderInfo.end()) {
        mtCSSwitch = bool(i.value());
    }


    i = algoOrderInfo.find("mtRebalanceSwitch");
    if (i != algoOrderInfo.end()) {
        mtRebalanceSwitch = bool(i.value());
    }

    i = algoOrderInfo.find("ttRebalanceSwitch");
    if (i != algoOrderInfo.end()) {
        ttRebalanceSwitch = bool(i.value());
    }

    i = algoOrderInfo.find("mtRebalanceFlag");
    if (i != algoOrderInfo.end()) {
        mtRebalanceFlag = bool(i.value());
    }

    i = algoOrderInfo.find("ttRebalanceFlag");
    if (i != algoOrderInfo.end()) {
        ttRebalanceFlag = bool(i.value());
    }


    i = algoOrderInfo.find("mtPriceTrendProtectFlag");
    if (i != algoOrderInfo.end()) {
        mtPriceTrendProtectFlag = bool(i.value());
    }

    i = algoOrderInfo.find("ttPriceTrendProtectFlag");
    if (i != algoOrderInfo.end()) {
        ttPriceTrendProtectFlag = bool(i.value());
    }


    i = algoOrderInfo.find("activePriceTickFlag");
    if (i != algoOrderInfo.end()) {
        activePriceTickFlag = bool(i.value());
    }

    i = algoOrderInfo.find("activePriceTickNum");
    if (i != algoOrderInfo.end()) {
        activePriceTickNum = int(i.value());
    }


    i = algoOrderInfo.find("passivePriceTickFlag");
    if (i != algoOrderInfo.end()) {
        passivePriceTickFlag = bool(i.value());
    }

    i = algoOrderInfo.find("passivePriceTickNum");
    if (i != algoOrderInfo.end()) {
        passivePriceTickNum = int(i.value());
    }


    i = algoOrderInfo.find("systemDelayTimeSpan");
    if (i != algoOrderInfo.end()) {
        systemDelayTimeSpan = int64_t(i.value());
    }

    i = algoOrderInfo.find("exchangeDelayTimeSpan");
    if (i != algoOrderInfo.end()) {
        exchangeDelayTimeSpan = int64_t(i.value());
    } 


    i = algoOrderInfo.find("systemDelayFlag");
    if (i != algoOrderInfo.end()) {
        systemDelayFlag = bool(i.value());
    }

    i = algoOrderInfo.find("exchangeDelayFlag");
    if (i != algoOrderInfo.end()) {
        exchangeDelayFlag = bool(i.value());
    }


    i = algoOrderInfo.find("mtSlipage");
    if (i != algoOrderInfo.end()) {
        mtSlipage = double(i.value());
    }

    i = algoOrderInfo.find("ttSlipage");
    if (i != algoOrderInfo.end()) {
        ttSlipage = double(i.value());
    }

    i = algoOrderInfo.find("mtSlipageFlag");
    if (i != algoOrderInfo.end()) {
        mtSlipageFlag = bool(i.value());
    }

    i = algoOrderInfo.find("ttSlipageFlag");
    if (i != algoOrderInfo.end()) {
        ttSlipageFlag = bool(i.value());
    }
}

void BaseAlgoOrder::SaveToFile() {
    json algoOrderInfo;
    algoOrderInfo["algoType"] = stra::AlgoTypeEnum2Str[algoType];
    algoOrderInfo["algoStrategyName"] = string(algoStrategyName);
    algoOrderInfo["algoOrderId"] = algoOrderId;
    algoOrderInfo["pairInstrumentKey"] = string(pairInstrumentKey);
    algoOrderInfo["baseAsset"] = string(baseAsset);
    algoOrderInfo["algoOrderStatus"] = stra::OrderStatusEnum2Str[algoOrderStatus];

    algoOrderInfo["activeInstrumentKey"] = string(activeInstrumentKey);
    algoOrderInfo["activePriceTakerPct"] = activePriceTakerPct;
    algoOrderInfo["activePriceMakerPct"] = activePriceMakerPct;
    algoOrderInfo["activeAccountId"] = activeAccountId;
    algoOrderInfo["activeDriveType"] = stra::DriveTypeEnum2Str[activeDriveType];
    algoOrderInfo["activeDepthMakerCheck"] = activeDepthMakerCheck;
    algoOrderInfo["activeDepthTakerCheck"] = activeDepthTakerCheck;
    algoOrderInfo["activeDepthMakerCheckType"] = stra::CheckTypeEnum2Str[activeDepthMakerCheckType];
    algoOrderInfo["activeDepthTakerCheckType"] = stra::CheckTypeEnum2Str[activeDepthTakerCheckType];
    algoOrderInfo["activeOrderType"] = stra::OrderTypeEnum2Str[activeOrderType];


    algoOrderInfo["passiveInstrumentKey"] = string(passiveInstrumentKey);
    algoOrderInfo["passivePriceTakerPct"] = passivePriceTakerPct;
    algoOrderInfo["passivePriceMakerPct"] = passivePriceMakerPct;
    algoOrderInfo["passiveAccountId"] = passiveAccountId;
    algoOrderInfo["passiveDriveType"] = stra::DriveTypeEnum2Str[passiveDriveType];
    algoOrderInfo["passiveDepthMakerCheck"] = passiveDepthMakerCheck;
    algoOrderInfo["passiveDepthTakerCheck"] = passiveDepthTakerCheck;
    algoOrderInfo["passiveDepthMakerCheckType"] = stra::CheckTypeEnum2Str[passiveDepthMakerCheckType];
    algoOrderInfo["passiveDepthTakerCheckType"] = stra::CheckTypeEnum2Str[passiveDepthTakerCheckType];
    algoOrderInfo["passiveOrderType"] = stra::OrderTypeEnum2Str[passiveOrderType];


    algoOrderInfo["passiveVolumePct"] = passiveVolumePct;

    algoOrderInfo["activeMakerCancelOrderTime"] = activeMakerCancelOrderTime;
    algoOrderInfo["activeTakerCancelOrderTime"] = activeTakerCancelOrderTime;
    algoOrderInfo["passiveMakerCancelOrderTime"] = passiveMakerCancelOrderTime;
    algoOrderInfo["passiveTakerCancelOrderTime"] = passiveTakerCancelOrderTime;
    algoOrderInfo["activePassiveCancelOrderPct"] = activePassiveCancelOrderPct;
    algoOrderInfo["activeMakerCancelOrderPct"] = activeMakerCancelOrderPct;
    algoOrderInfo["activeTakerCancelOrderPct"] = activeTakerCancelOrderPct;
    algoOrderInfo["passiveMakerCancelOrderPct"] = passiveMakerCancelOrderPct;
    algoOrderInfo["passiveTakerCancelOrderPct"] = passiveTakerCancelOrderPct;

    
    algoOrderInfo["activeMakerFeeRate"] = activeMakerFeeRate;
    algoOrderInfo["activeTakerFeeRate"] = activeTakerFeeRate;
    algoOrderInfo["passiveMakerFeeRate"] = passiveMakerFeeRate;
    algoOrderInfo["passiveTakerFeeRate"] = passiveTakerFeeRate;
    algoOrderInfo["activeTakerSlippage"] = activeTakerSlippage;
    algoOrderInfo["activeMakerSlippage"] = activeMakerSlippage;
    algoOrderInfo["passiveTakerSlippage"] = passiveTakerSlippage;
    algoOrderInfo["passiveMakerSlippage"] = passiveMakerSlippage;


    algoOrderInfo["pairActiveTotalPrice"] = pairActiveTotalPrice;
    algoOrderInfo["pairTotalVolume"] = pairTotalVolume;
    algoOrderInfo["pairPassiveTotalPrice"] = pairPassiveTotalPrice;


    algoOrderInfo["makerTakerFs"] = makerTakerFs;
    algoOrderInfo["takerTakerFs"] = takerTakerFs;
    algoOrderInfo["maxMTOrderSize"] = maxMTOrderSize;
    algoOrderInfo["maxTTOrderSize"] = maxTTOrderSize;

    algoOrderInfo["targetSpreadType"] = stra::TargetSpredPriceEnum2Str[targetSpreadType];
    algoOrderInfo["activeVolumeCalcualteType"] = stra::ActiveVolumeCalcualteTypeEnum2Str[activeVolumeCalcualteType];


    algoOrderInfo["ttTargetVolume"] = ttTargetVolume;
    algoOrderInfo["mtTargetVolume"] = mtTargetVolume;
    algoOrderInfo["profitSwitch"] = profitSwitch;
    algoOrderInfo["profitPct"] = profitPct;


    algoOrderInfo["ttOLStartSpread"] = ttOLStartSpread;
    algoOrderInfo["ttOLEndSpread"] = ttOLEndSpread;
    algoOrderInfo["ttOLStartVolume"] = ttOLStartVolume;
    algoOrderInfo["ttOLEndVolume"] = ttOLEndVolume;
    algoOrderInfo["ttOLSwitch"] = ttOLSwitch;


    algoOrderInfo["ttCLStartSpread"] = ttCLStartSpread;
    algoOrderInfo["ttCLEndSpread"] = ttCLEndSpread;
    algoOrderInfo["ttCLStartVolume"] = ttCLStartVolume;
    algoOrderInfo["ttCLEndVolume"] = ttCLEndVolume;
    algoOrderInfo["ttCLSwitch"] = ttCLSwitch;


    algoOrderInfo["ttOSStartSpread"] = ttOSStartSpread;
    algoOrderInfo["ttOSEndSpread"] = ttOSEndSpread;
    algoOrderInfo["ttOSStartVolume"] = ttOSStartVolume;
    algoOrderInfo["ttOSEndVolume"] = ttOSEndVolume;
    algoOrderInfo["ttOSSwitch"] = ttOSSwitch;


    algoOrderInfo["ttCSStartSpread"] = ttCSStartSpread;
    algoOrderInfo["ttCSEndSpread"] = ttCSEndSpread;
    algoOrderInfo["ttCSStartVolume"] = ttCSStartVolume;
    algoOrderInfo["ttCSEndVolume"] = ttCSEndVolume;
    algoOrderInfo["ttCSSwitch"] = ttCSSwitch;


    algoOrderInfo["mtOLStartSpread"] = mtOLStartSpread;
    algoOrderInfo["mtOLEndSpread"] = mtOLEndSpread;
    algoOrderInfo["mtOLStartVolume"] = mtOLStartVolume;
    algoOrderInfo["mtOLEndVolume"] = mtOLEndVolume;
    algoOrderInfo["mtOLSwitch"] = mtOLSwitch;


    algoOrderInfo["mtCLStartSpread"] = mtCLStartSpread;
    algoOrderInfo["mtCLEndSpread"] = mtCLEndSpread;
    algoOrderInfo["mtCLStartVolume"] = mtCLStartVolume;
    algoOrderInfo["mtCLEndVolume"] = mtCLEndVolume;
    algoOrderInfo["mtCLSwitch"] = mtCLSwitch;


    algoOrderInfo["mtOSStartSpread"] = mtOSStartSpread;
    algoOrderInfo["mtOSEndSpread"] = mtOSEndSpread;
    algoOrderInfo["mtOSStartVolume"] = mtOSStartVolume;
    algoOrderInfo["mtOSEndVolume"] = mtOSEndVolume;
    algoOrderInfo["mtOSSwitch"] = mtOSSwitch;


    algoOrderInfo["mtCSStartSpread"] = mtCSStartSpread;
    algoOrderInfo["mtCSEndSpread"] = mtCSEndSpread;
    algoOrderInfo["mtCSStartVolume"] = mtCSStartVolume;
    algoOrderInfo["mtCSEndVolume"] = mtCSEndVolume;
    algoOrderInfo["mtCSSwitch"] = mtCSSwitch;


    algoOrderInfo["mtRebalanceSwitch"] = mtRebalanceSwitch;
    algoOrderInfo["ttRebalanceSwitch"] = ttRebalanceSwitch;

    algoOrderInfo["mtRebalanceFlag"] = mtRebalanceFlag;
    algoOrderInfo["ttRebalanceFlag"] = ttRebalanceFlag;

    algoOrderInfo["mtPriceTrendProtectFlag"] = mtPriceTrendProtectFlag;
    algoOrderInfo["ttPriceTrendProtectFlag"] = ttPriceTrendProtectFlag;

    algoOrderInfo["activePriceTickFlag"] = activePriceTickFlag;
    algoOrderInfo["activePriceTickNum"] = activePriceTickNum;

    algoOrderInfo["passivePriceTickFlag"] = passivePriceTickFlag;
    algoOrderInfo["passivePriceTickNum"] = passivePriceTickNum;

    
    algoOrderInfo["systemDelayTimeSpan"] = systemDelayTimeSpan;
    algoOrderInfo["exchangeDelayTimeSpan"] = exchangeDelayTimeSpan;
    algoOrderInfo["systemDelayFlag"] = systemDelayFlag;
    algoOrderInfo["exchangeDelayFlag"] = exchangeDelayFlag;
    algoOrderInfo["mtSlipage"] = mtSlipage;
    algoOrderInfo["ttSlipage"] = ttSlipage;
    algoOrderInfo["mtSlipageFlag"] = mtSlipageFlag;
    algoOrderInfo["ttSlipageFlag"] = ttSlipageFlag;


    stringstream ss;
    ss << algoOrderId << "_algoPairOrder.json";
    string filePath = ss.str();
	std::ofstream o(filePath.c_str());
	o << std::setw(4) << algoOrderInfo;
}

string BaseAlgoOrder::GeneratePubStr() {
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
}

string BaseAlgoOrder::GeneratePubStrOnUpdate() {
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
}

PairOrder BaseAlgoOrder::GetTargetPairOrder(stra::TradingType tradingTypeOrder, stra::TradingType tradingTypeOffset, int64_t pairOrderId) {
    PairOrder pairOrder;
    return pairOrder;
}

PairOrder BaseAlgoOrder::CreatePairOrder(stra::TradingType tradingType) {
    PairOrder pairOrder;
    return pairOrder;
}

PairOrder BaseAlgoOrder::CreatePairOrder(stra::TradingType tradingType, stra::Direction activeDirection) {
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
