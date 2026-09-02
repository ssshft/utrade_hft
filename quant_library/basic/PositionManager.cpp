#include "PositionManager.h"
#include "BasicInfoMgr.h"
#include "Utility.h"
#include "SpreadManager.h"

PositionManager::PositionManager() {

}

PositionManager::~PositionManager() {

}

void PositionManager::Init(sm::SecurityManager* s) {
    smc = s;
}

void PositionManager::SetBaseAsset(char* ass) {
    strncpy(baseAsset, ass, stra::ASSET_LEN);
}

void PositionManager::OnInsertOrder(const stra::QuantOrder& order) {
    string instrumentKey = order.instrumentKey;
    md::InstrumentInfo info;
    smc->get_instrument_info(order.exchangeType, order.instType, order.instrument, info);
    auto& pos = account.mPosition[instrumentKey];
    strncpy(pos.instrumentKey, order.instrumentKey, stra::INST_KEY_LEN);
    if (order.instType == SPOT) {
        if (order.direction == DT_LONG) {
            auto& ass = account.mAsset[info.quote];
            strncpy(ass.asset, info.quote, stra::ASSET_LEN);
            ass.frozenAmount += order.price * order.volume;
            pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
            pos.frozenLongPosition += order.volume;
        } else if (order.direction == DT_SHORT) {
            auto& ass = account.mAsset[info.base];
            strncpy(ass.asset, info.base, stra::ASSET_LEN);
            ass.frozenAmount += order.volume;
            pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
            pos.frozenShortPosition += order.volume;
        }
    } else if (order.instType == MARGIN) {
        if (order.direction == DT_LONG) {
            auto& ass = account.mAsset[info.quote];
            strncpy(ass.asset, info.quote, stra::ASSET_LEN);
            ass.frozenAmount += order.price * order.volume;
            pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
            pos.frozenLongPosition += order.volume;
        } else if (order.direction == DT_SHORT) {
            auto& ass = account.mAsset[info.base];
            strncpy(ass.asset, info.base, stra::ASSET_LEN);
            ass.frozenAmount += order.volume;
            pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
            pos.frozenShortPosition += order.volume;
        }
    } else if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES || order.instType == BUSD_SWAP || order.instType == C_SWAP || order.instType == C_FUTURES) {
        auto& ass = account.mAsset[info.margin];
        strncpy(ass.asset, info.margin, stra::ASSET_LEN);
        if (order.direction == DT_LONG) {
            if (info.calcType == 0) {
                ass.openMarginAmount += order.price * order.volume * info.value / account.openRealLeverage;
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
            } else if (info.calcType == 1) {
                ass.openMarginAmount += order.volume * info.value / order.price / account.openRealLeverage;
                if (pos.frozenLongPrice > stra::MIN_FLOAT) {
                    pos.frozenLongPrice = 1 / ((1 / pos.frozenLongPrice * pos.frozenLongPosition + 1 / order.price * order.volume) / (pos.frozenLongPosition + order.volume));
                } else {
                    pos.frozenLongPrice = order.price;
                } 
            }
            pos.frozenLongPosition += order.volume;
        } else if (order.direction == DT_SHORT) {
            if (info.calcType == 0) {
                ass.openMarginAmount += order.price * order.volume * info.value / account.openRealLeverage;
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
            } else if (info.calcType == 1) {
                ass.openMarginAmount += order.volume * info.value / order.price / account.openRealLeverage;
                if (pos.frozenShortPrice > stra::MIN_FLOAT) {
                    pos.frozenShortPrice = 1 / ((1 / pos.frozenShortPrice * pos.frozenShortPosition + 1 / order.price * order.volume) / (pos.frozenShortPosition + order.volume));
                } else {
                    pos.frozenShortPrice = order.price;
                }
            }
            pos.frozenShortPosition += order.volume;
        }
    }
}

void PositionManager::OnDeleteOrder(const stra::QuantOrder& order) {
    string instrumentKey = order.instrumentKey;
    auto iter = account.mPosition.find(instrumentKey);
    if (iter == account.mPosition.end()) {
        //LOG_INFO("OnDeleteOrder: can not find this order! %s", order.GetStr().c_str());
        return;
    }

    md::InstrumentInfo info;
    smc->get_instrument_info(order.exchangeType, order.instType, order.instrument, info);

    double leftVolume = order.volume - order.totalVolumeOnOrder;
    auto& pos = account.mPosition[instrumentKey];
    if (order.instType == SPOT) {
        if (order.direction == DT_LONG) {
            auto& ass = account.mAsset[info.quote];
            ass.frozenAmount -= order.price * leftVolume;
            if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                pos.frozenLongPrice = -1;
                pos.frozenLongPosition = 0;
            } else {
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * leftVolume) / (pos.frozenLongPosition - leftVolume);
                pos.frozenLongPosition -= leftVolume;
            }
        } else if (order.direction == DT_SHORT) {
            auto& ass = account.mAsset[info.base];
            ass.frozenAmount -= leftVolume;
	    //LOG_INFO("OnDeleteOrder left:%s, totalAmount:%f frozenAmount: %f leftVolume: %f", info.left.c_str(), ass.totalAmount, ass.frozenAmount, leftVolume);
            if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                pos.frozenShortPrice = -1;
                pos.frozenShortPosition = 0;
            } else {
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.price * leftVolume) / (pos.frozenShortPosition - leftVolume);
                pos.frozenShortPosition -= leftVolume;
            }
        }
    } else if (order.instType == MARGIN) {
        if (order.direction == DT_LONG) {
            auto& ass = account.mAsset[info.quote];
            ass.frozenAmount -= order.price * leftVolume;
            if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                pos.frozenLongPrice = -1;
                pos.frozenLongPosition = 0;
            } else {
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * leftVolume) / (pos.frozenLongPosition - leftVolume);
                pos.frozenLongPosition -= leftVolume; 
            }
        } else if (order.direction == DT_SHORT) {
            auto& ass = account.mAsset[info.base];
            ass.frozenAmount -= leftVolume;
            if (fabs(pos.frozenShortPosition - leftVolume) <= stra::MIN_FLOAT) {
                pos.frozenShortPrice = -1;
                pos.frozenShortPosition = 0;
            } else {
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.price * leftVolume) / (pos.frozenShortPosition - leftVolume);
                pos.frozenShortPosition -= leftVolume;
            }
        }
    } else if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES || order.instType == BUSD_SWAP || order.instType == C_SWAP || order.instType == C_FUTURES) {
        auto& ass = account.mAsset[info.margin];
        if (order.direction == DT_LONG) {
            if (info.calcType == 0) {
                ass.openMarginAmount -= order.price * leftVolume * info.value / account.openRealLeverage;
                if (fabs(ass.openMarginAmount) <= stra::MIN_FLOAT) {
                    ass.openMarginAmount = 0;
                }
                if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                } else {
                    pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * leftVolume) / (pos.frozenLongPosition - leftVolume);
                    pos.frozenLongPosition -= leftVolume;
                }
            } else if (info.calcType == 1) {
                ass.openMarginAmount -= leftVolume * info.value / order.price / account.openRealLeverage;
                if (fabs(ass.openMarginAmount) <= stra::MIN_FLOAT) {
                    ass.openMarginAmount = 0;
                }
                if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                } else {
                    pos.frozenLongPrice = 1 / ((1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * leftVolume) / (pos.frozenLongPosition - leftVolume));
                    pos.frozenLongPosition -= leftVolume;
                }
            }
        } else if (order.direction == DT_SHORT) {
            if (info.calcType == 0) {
                ass.openMarginAmount -= order.price * leftVolume * info.value / account.openRealLeverage;
                if (fabs(ass.openMarginAmount) <= stra::MIN_FLOAT) {
                    ass.openMarginAmount = 0;
                }
                if (fabs(pos.frozenShortPosition - leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenShortPrice = -1;
                    pos.frozenShortPosition = 0;
                } else {
                    pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.price * leftVolume) / (pos.frozenShortPosition - leftVolume);
                    pos.frozenShortPosition -= leftVolume;
                }
            } else if (info.calcType == 1) {
                ass.openMarginAmount -= leftVolume * info.value / order.price / account.openRealLeverage;
                if (fabs(ass.openMarginAmount) <= stra::MIN_FLOAT) {
                    ass.openMarginAmount = 0;
                }
                if (fabs(pos.frozenShortPosition - leftVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenShortPrice = -1;
                    pos.frozenShortPosition = 0;
                } else {
                    pos.frozenShortPrice = 1 / ((1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * leftVolume) / (pos.frozenShortPosition - leftVolume));
                    pos.frozenShortPosition -= leftVolume;
                }
            }
        }
    }
}

void PositionManager::OnOrder(const stra::QuantOrder& order) {
    string instrumentKey = order.instrumentKey;
    md::InstrumentInfo info;
    smc->get_instrument_info(order.exchangeType, order.instType, order.instrument, info);
    auto& pos = account.mPosition[instrumentKey];
    if (order.instType == SPOT) {
        if (order.direction == DT_LONG) {
            auto& leftAss = account.mAsset[info.base];
            leftAss.totalAmount += order.tradeVolume;
	        //LOG_INFO("PositionManager OnOrder  leftAss:%s tradeVolume:%f", info.left.c_str(), order.tradeVolume);

            auto& rightAss = account.mAsset[info.quote];
            rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
            rightAss.frozenAmount -= order.price * order.tradeVolume;
            double closeVolume = min(order.tradeVolume, pos.shortPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.shortPosition -= closeVolume;
            }

            if (openVolume > stra::MIN_FLOAT) {
                pos.longAvgPrice = (pos.longAvgPrice * pos.longPosition + order.tradePrice * openVolume) / (pos.longPosition + openVolume);
                pos.longPosition += openVolume;
            }
            if (fabs(pos.frozenLongPosition - order.tradeVolume) > 0) {
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * order.tradeVolume) / (pos.frozenLongPosition - order.tradeVolume);
                pos.frozenLongPosition -= order.tradeVolume;
            } else {
                pos.frozenLongPrice = -1;
                pos.frozenLongPosition = 0;
            }
        } else if (order.direction == DT_SHORT) {
            auto& leftAss = account.mAsset[info.base];
            leftAss.totalAmount -= order.tradeVolume;
            leftAss.frozenAmount -= order.tradeVolume;
	        //LOG_INFO("PositionManager OnOrder  leftAss:%s totalAmount:%f frozenAmount:%f tradeVolume:%f", info.left.c_str(), leftAss.totalAmount, leftAss.frozenAmount, order.tradeVolume);

            auto& rightAss = account.mAsset[info.quote];
            rightAss.totalAmount += order.tradePrice * order.tradeVolume;
                
            double closeVolume = min(order.tradeVolume, pos.longPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.longPosition -= closeVolume;
            }

            if (openVolume > stra::MIN_FLOAT) {
                pos.shortAvgPrice = (pos.shortAvgPrice * pos.shortPosition + order.tradePrice * openVolume) / (pos.shortPosition + openVolume);
                pos.shortPosition += openVolume;
            }
            if (fabs(pos.frozenShortPosition - order.tradeVolume) > 0) {
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.price * order.tradeVolume) / (pos.frozenShortPosition - order.tradeVolume);
                pos.frozenShortPosition -= order.tradeVolume;
            } else {
                pos.frozenShortPrice = -1;
                pos.frozenShortPosition = 0;
            }
        }
    } else if (order.instType == MARGIN) {
        if (order.direction == DT_LONG) {
            double closeVolume = min(order.tradeVolume, pos.shortPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.shortPosition -= closeVolume;
            }
            if (openVolume > stra::MIN_FLOAT) {
                pos.longAvgPrice = (pos.longAvgPrice * pos.longPosition + order.tradePrice * openVolume) / (pos.longPosition + openVolume);
                pos.longPosition += openVolume;
            }
            if (fabs(pos.frozenLongPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * order.tradeVolume) / (pos.frozenLongPosition - order.tradeVolume);
                pos.frozenLongPosition -= order.tradeVolume;
            } else {
                pos.frozenLongPrice = -1;
                pos.frozenLongPosition = 0;
            }
        } else if (order.direction == DT_SHORT) {
            double closeVolume = min(order.tradeVolume, pos.longPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.longPosition -= closeVolume;
            }
            if (openVolume > stra::MIN_FLOAT) {
                pos.shortAvgPrice = (pos.shortAvgPrice * pos.shortPosition + order.tradePrice * openVolume) / (pos.shortPosition + openVolume);
                pos.shortPosition += openVolume;
            }
            if (fabs(pos.frozenShortPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.price * order.tradeVolume) / (pos.frozenShortPosition - order.tradeVolume);
                pos.frozenShortPosition -= order.tradeVolume;
            } else {
                pos.frozenShortPrice = -1;
                pos.frozenShortPosition = 0;
            }
        }
    } else if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES || order.instType == BUSD_SWAP || order.instType == C_SWAP || order.instType == C_FUTURES) {
        if (order.direction == DT_LONG) {
            double closeVolume = min(order.tradeVolume , pos.shortPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.shortPosition -= closeVolume;
                auto& leftAss = account.mAsset[info.margin];
                if (info.calcType == 0) {
                    double closeAmount = (pos.shortAvgPrice - order.tradePrice) * closeVolume * info.value;
                    leftAss.totalAmount += closeAmount;
                    leftAss.closeAmount += closeAmount;
                    pos.closeAmount += closeAmount;
                } else if (info.calcType == 1) {
                    double closeAmount = (1 / order.tradePrice - 1 / pos.shortAvgPrice) * closeVolume * info.value;
                    leftAss.totalAmount += closeAmount;
                    leftAss.closeAmount += closeAmount;
                    pos.closeAmount += closeAmount;
                }
            }

            if (openVolume > stra::MIN_FLOAT) {
                if (info.calcType == 0) {
                    pos.longAvgPrice = (pos.longAvgPrice * pos.longPosition + order.tradePrice * openVolume) / (pos.longPosition + openVolume);
                    pos.longPosition += openVolume;
                } else if (info.calcType == 1) {
		            //LOG_INFO("AvgPrice pos.longAvgPrice:%f pos.longPosition:%f order.tradePrice:%f openVolume:%f", pos.longAvgPrice, pos.longPosition, order.tradePrice, openVolume);
                    pos.longAvgPrice = 1 / ((1 / pos.longAvgPrice * pos.longPosition + 1 / order.tradePrice * openVolume) / (pos.longPosition + openVolume));
                    pos.longPosition += openVolume;
                }
            }

            if (info.calcType == 0) {
                auto& ass = account.mAsset[info.margin];
                if (fabs(pos.frozenLongPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                    pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.tradeVolume * order.price) / (pos.frozenLongPosition - order.tradeVolume);
                    pos.frozenLongPosition -= order.tradeVolume;
                } else {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                }
                ass.openMarginAmount -= order.price * order.tradeVolume * info.value / account.openRealLeverage;
            } else if (info.calcType == 1) {
                auto& ass = account.mAsset[info.margin];
                if (fabs(pos.frozenLongPosition - order.tradeVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * order.tradeVolume) <= stra::MIN_FLOAT) {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                } else {
                    pos.frozenLongPrice = 1 / ((1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * order.tradeVolume) / (pos.frozenLongPosition - order.tradeVolume));
                    pos.frozenLongPosition -= order.tradeVolume;
                }
                ass.openMarginAmount -= order.tradeVolume * info.value / order.price / account.openRealLeverage;
            }
        } else if (order.direction == DT_SHORT) {
            double closeVolume = min(order.tradeVolume , pos.longPosition);
            double openVolume = order.tradeVolume - closeVolume;
            if (closeVolume > stra::MIN_FLOAT) {
                pos.longPosition -= closeVolume;
                auto& leftAss = account.mAsset[info.margin];
                auto& rightAss = account.mAsset[info.margin];
                if (info.calcType == 0) {
                    double closeAmount = (order.tradePrice - pos.longAvgPrice) * closeVolume * info.value;
                    leftAss.totalAmount += closeAmount;
                    rightAss.closeAmount += closeAmount;
                    pos.closeAmount += closeAmount;
                } else if (info.calcType == 1) {
                    double closeAmount = (1 / pos.longAvgPrice - 1 / order.tradePrice) * closeVolume * info.value;
                    rightAss.totalAmount += closeAmount;
                    rightAss.closeAmount += closeAmount;
                    pos.closeAmount += closeAmount;
                }
            }

            if (openVolume > stra::MIN_FLOAT) {
                if (info.calcType == 0) {
                    pos.shortAvgPrice = (pos.shortAvgPrice * pos.shortPosition + order.tradePrice * openVolume) / (pos.shortPosition + openVolume);
                    pos.shortPosition += openVolume;
                } else if (info.calcType == 1) {
		        //LOG_INFO("AvgPrice pos.shortAvgPrice:%f pos.shortPosition:%f order.tradePrice:%f openVolume:%f", pos.shortAvgPrice, pos.shortPosition, order.tradePrice, openVolume);
                    pos.shortAvgPrice = 1 / ((1 / pos.shortAvgPrice * pos.shortPosition + 1 / order.tradePrice * openVolume) / (pos.shortPosition + openVolume));
                    pos.shortPosition += openVolume;
                }
            }

            if (info.calcType == 0) {
                auto& ass = account.mAsset[info.margin];
                if (fabs(pos.frozenShortPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                    pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.tradeVolume * order.price) / (pos.frozenShortPosition - order.tradeVolume);
                    pos.frozenShortPosition -= order.tradeVolume;
                } else {
                    pos.frozenShortPrice = -1;
                    pos.frozenShortPosition = 0;
                }
                ass.openMarginAmount -= order.price * order.tradeVolume * info.value / account.openRealLeverage;
            } else if (info.calcType == 1) {
                auto& ass = account.mAsset[info.margin];
                if (fabs(pos.frozenShortPosition - order.tradeVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * order.tradeVolume) <= stra::MIN_FLOAT) {
                    pos.frozenShortPrice = -1;
                    pos.frozenShortPosition = 0;
                } else {
                    pos.frozenShortPrice = 1 / ((1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * order.tradeVolume) / (pos.frozenShortPosition - order.tradeVolume));
                    pos.frozenShortPosition -= order.tradeVolume;
                }
                ass.openMarginAmount -= order.tradeVolume * info.value / order.price / account.openRealLeverage;
            }
        }
    }
}

/*
void PositionManager::UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth) {
    if (depth.instType == USDT_SWAP || depth.instType == USDT_FUTURES || depth.instType == BUSD_SWAP || depth.instType == C_SWAP || depth.instType == C_FUTURES) {
        double lastPrice = (depth.vAskPrice[0] + depth.vBidPrice[0]) / 2;
        string instrumentKey = ExchangeTypeEnum2StrMap[depth.exchangeType] + "." + InstTypeEnum2StrMap[depth.instType] + "." + depth.instrument;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        string affectAsset = info.margin;
        auto& ass = account.mAsset[affectAsset];
        auto iu = account.mPosition.find(instrumentKey);
        if (iu != account.mPosition.end()) {
            auto& pos = iu->second;
            pos.lastFloatAmount = pos.floatAmount;
            pos.lastPositionValue = pos.positionValue;
            pos.floatAmount = 0;
            if (pos.longPosition > stra::MIN_FLOAT) {
                if (info.calculateType == 0) {
                    pos.floatAmount += (lastPrice - pos.longAvgPrice) * pos.longPosition * info.multiple;
                } else if (info.calculateType == 1) {
                    pos.floatAmount += (1 / pos.longAvgPrice - 1 / lastPrice) * pos.longPosition * info.multiple;
                }
            }
            if (pos.shortPosition > stra::MIN_FLOAT) {
                if (info.calculateType == 0) {
                    pos.floatAmount += (pos.shortAvgPrice - lastPrice) * pos.shortPosition * info.multiple;
                } else if (info.calculateType == 1) {
                    pos.floatAmount += (1 / lastPrice - 1 / pos.shortAvgPrice) * pos.shortPosition * info.multiple;
                }
            }
            if (account.marginType == stra::AccountMarginType_ISOLATED) {
                if (info.calculateType == 0) {
                    pos.positionValue = (pos.longPosition + pos.shortPosition) * lastPrice * info.multiple;
                } else if (info.calculateType == 1) {
                    pos.positionValue = (pos.longPosition + pos.shortPosition) / lastPrice * info.multiple;
                }
            }

            ass.floatAmount = ass.floatAmount - pos.lastFloatAmount + pos.floatAmount;
            ass.positionValue = ass.positionValue - pos.lastPositionValue + pos.positionValue;
            ass.marginAmount = ass.positionValue / account.maxRealLeverage;
        }
    }
}
*/

void PositionManager::CalcualteFloatPnl() {
    unordered_map<string, double> mFloat;
    for (auto it = account.mPosition.begin(); it != account.mPosition.end(); ++it) {
        string instrumentKey = it->first;
        vector<string> v;
        splitString(instrumentKey, v, ".");
        auto& pos = it->second;
        
        InstType instType = InstTypeStr2EnumMap[v[1]];

        md::InstrumentInfo info;
        smc->get_instrument_info(v[0].c_str(), v[1].c_str(), v[2].c_str(), info);

        const Bbo& bbo = SpreadManager::Instance().GetBbo(instrumentKey);
        double lastPrice = (bbo.askPrice + bbo.bidPrice) / 2;
        
        if (lastPrice > 0) {
            if (instType == USDT_SWAP || instType == USDT_FUTURES || instType == BUSD_SWAP || instType == C_SWAP || instType == C_FUTURES) {
                if (pos.longPosition > stra::MIN_FLOAT) {
                    if (info.calcType == 0) {
                        double floatAmount = (lastPrice - pos.longAvgPrice) * pos.longPosition * info.value;
                        mFloat[info.margin] += floatAmount;
                        pos.floatAmount = floatAmount;
                    } else if (info.calcType == 1) {
                        double floatAmount = (1 / pos.longAvgPrice - 1 / lastPrice) * pos.longPosition * info.value;
                        mFloat[info.margin] += floatAmount;
                        pos.floatAmount = floatAmount;
                    }
                }

                if (pos.shortPosition > stra::MIN_FLOAT) {
                    if (info.calcType == 0) {
                        double floatAmount = (pos.shortAvgPrice - lastPrice) * pos.shortPosition * info.value;
                        mFloat[info.margin] += floatAmount;
                        pos.floatAmount = floatAmount;
                    } else if (info.calcType == 1) {
                        double floatAmount = (1 / lastPrice - 1 / pos.shortAvgPrice) * pos.shortPosition * info.value;
                        mFloat[info.margin] += floatAmount;
                        pos.floatAmount = floatAmount;
                    }
                }
            }
        } else {
            pos.floatAmount = 0;
        }
    }

    for (auto it = mFloat.begin(); it != mFloat.end(); ++it) {
        account.mAsset[it->first].floatAmount = it->second;
    }
}


// 不需要计算
void PositionManager::CalcualtePnl(string activeInstrumentKey) { // profit = totalAmount - initAmount - loanAmount - transferAmount; profit * rate
    totalPnl = 0.0;
    // CalcualteFloatPnl();  // 暂时不需要调用
    for (auto it = account.mAsset.begin(); it != account.mAsset.end(); ++it) {
        double assetPnl = it->second.totalAmount + it->second.floatAmount - it->second.loanAmount - it->second.initAmount - it->second.transferAmount;

        string ass = it->first;
        //if (strcmp(ass.c_str(), "USD") == 0 || strcmp(ass.c_str(), "USDC") == 0 || strcmp(ass.c_str(), "BUSD") == 0 || strcmp(ass.c_str(), "TUSD") == 0) {
        //    ass = "USDT";
        //}
        mPnl[ass] += assetPnl;
        if (strcmp(ass.c_str(), baseAsset) == 0) {
            totalPnl += assetPnl;
	    }
	    else {
            // string instrumentKey = "BINANCE.SPOT." + string(it->second.asset) + "-" + string(baseAsset);
            const Bbo& bbo = SpreadManager::Instance().GetBbo(activeInstrumentKey);
            double price = (bbo.askPrice + bbo.bidPrice) / 2;
            stra::InstrumentInfo info; //= BasicInfoMgr::GetInstance().GetBasicInfo(activeInstrumentKey);
            if (strcmp(ass.c_str(), info.instLeft.c_str()) == 0) {
                totalPnl += assetPnl * price;
            } else {
                totalPnl += assetPnl / price;
            }
        }
    }
}

stra::QuantAccount& PositionManager::GetAccount() {
    return account;
}

unordered_map<string, double>& PositionManager::GetPnl() {
    return mPnl;
}

double PositionManager::GetTotalPnl() {
    return totalPnl;
}