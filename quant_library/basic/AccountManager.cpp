#include "AccountManager.h"
#include "DataManager.h"
#include "SpreadManager.h"
#include "StrategyConfig.h"

AccountManager::AccountManager() {
    check = true;
}

AccountManager::~AccountManager() {
    mTransfer.clear();
    mAccount.clear();
    mLending.clear();
}

void AccountManager::Init() {
    auto& mAccountInfo = StrategyConfig::GetInstance().GetAccountInfo();
    for (auto iter = mAccountInfo.begin(); iter != mAccountInfo.end(); ++iter) {
    	stra::QuantAccount account;
	    int accountId = iter->second.accountId;
    	account.openRealLeverage = iter->second.openRealLeverage;
    	account.maxRealLeverage = iter->second.maxRealLeverage;
    	account.passiveOpenRealLeverage = iter->second.passiveOpenRealLeverage;
    	account.passiveMaxRealLeverage = iter->second.passiveMaxRealLeverage;
    	account.openActiveMgnRatio = iter->second.openActiveMgnRatio;
    	account.openPassiveMgnRatio = iter->second.openPassiveMgnRatio;
    	account.accountType = iter->second.accountType;
    	mAccount[accountId] = account;
    }
}

AccountManager& AccountManager::Instance() {
    static AccountManager accountManager;
    return accountManager;
}

void AccountManager::OnBalance(const pubsub::Balance& balance) {
    auto& quantAccount = mAccount[balance.accountId];
    auto& ass = quantAccount.mAsset[balance.currency];
    ass.totalAmount = balance.total;
    ass.frozenAmount = balance.frozen;
    ass.floatAmount = balance.unrealizedPnl;
}

void AccountManager::OnTotalAccount(const pubsub::TotalAccount& totalAccount) {
    auto& quantAccount = mAccount[totalAccount.accountId];
    quantAccount.totalEquity = totalAccount.totalEquity;
    quantAccount.adjEquity = totalAccount.adjEquity;
    quantAccount.mmr = totalAccount.mmr;
    quantAccount.mgnRatio = totalAccount.mgnRatio;
}

void AccountManager::OnPosition(const pubsub::Position& position) {
    auto& quantAccount = mAccount[position.accountId];
    string instrumentKey = ExchangeTypeEnum2Str[position.exchangeTypeEnum] + "." + InstTypeEnum2Str[position.instTypeEnum] + "." + position.instId;
    stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);

    stra::QuantMarketDepth depth = DataManager::Instance().GetLastDepth(instrumentKey);
    const Bbo& bbo = SpreadManager::Instance().GetBbo(instrumentKey)
    double lastPrice = (bbo.askPrice + bbo.bidPrice) / 2;
    if (lastPrice < stra::MIN_FLOAT) { // 没有行情取markPrice保证有价格
        lastPrice = position.markPrice;
    }
    auto& pos = quantAccount.mPosition[instrumentKey];
    pos.floatAmount = position.unrealizedPnl;
    if (position.direction == DT_LONG) {
        pos.longPosition = position.volume;
        pos.shortPosition = 0;
        pos.longAvgPrice = position.avgPrice;
        pos.shortAvgPrice = -1;
    } else if (position.direction == DT_SHORT) {
        pos.longPosition = 0;
        pos.shortPosition = position.volume;
        pos.longAvgPrice = -1;
        pos.shortAvgPrice = position.avgPrice;
    }
    if (position.instTypeEnum == USDT_SWAP || position.instTypeEnum == USDT_FUTURES || position.instTypeEnum == BUSD_SWAP || position.instTypeEnum == C_SWAP || position.instTypeEnum == C_FUTURES) {
        double positionValue = 0.0;
        if (info.calculateType == 0) {
            positionValue = (fabs(pos.longPosition) + fabs(pos.shortPosition)) * lastPrice * info.multiple;
        } else if (info.calculateType == 1) {
            if (lastPrice > 0) {
                positionValue = (fabs(pos.longPosition) + fabs(pos.shortPosition)) / lastPrice * info.multiple;
            }
        }

        LOG_INFO("OnPosition  instrument:{} longPosition:{} shortPosition:{} pos.positionValue:{}  cal positionValue:{}", position.instrument, pos.longPosition, pos.shortPosition, pos.positionValue, positionValue);
        auto& ass = quantAccount.mAsset[info.margin];
        ass.positionValue += positionValue - pos.positionValue;
        pos.positionValue = positionValue;
    }   
}

void AccountManager::OnInsertOrder(const stra::QuantOrder& order) {
    auto it = mAccount.find(order.strategyAccountId);
    if (it != mAccount.end()) {
        string instrumentKey = order.instrumentKey;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        auto& pos = it->second.mPosition[instrumentKey];
        if (order.instType == SPOT) {
            if (order.direction == DT_LONG) {
                auto& ass = it->second.mAsset[info.right];
                ass.frozenAmount += order.price * order.volume;
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
                pos.frozenLongPosition += order.volume;
            } else if (order.direction == DT_SHORT {
                auto& ass = it->second.mAsset[info.left];
                ass.frozenAmount += order.volume;
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
                pos.frozenShortPosition += order.volume;
            }
        } else if (order.instType == MARGIN) {
            if (order.direction == DT_LONG) {
                auto& ass = it->second.mAsset[info.right];
                ass.frozenAmount += order.price * order.volume;
                pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
                pos.frozenLongPosition += order.volume;
            } else if (order.direction == DT_SHORT) {
                auto& ass = it->second.mAsset[info.left];
                ass.frozenAmount += order.volume;
                pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
                pos.frozenShortPosition += order.volume;
            }
        } else if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES || order.instType == BUSD_SWAP || order.instType == C_SWAP || order.instType == C_FUTURES) {
            auto& ass = it->second.mAsset[info.margin];
            strncpy(ass.asset, info.margin.c_str(), stra::ASSET_LEN);
            if (order.direction == DT_LONG) {
                if (info.calculateType == 0) {
                    ass.openMarginAmount += order.price * order.volume * info.multiple / it->second.openRealLeverage;
                    pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition + order.price * order.volume) / (pos.frozenLongPosition + order.volume);
                } else if (info.calculateType == 1) {
                    ass.openMarginAmount += order.volume * info.multiple / order.price / it->second.openRealLeverage;
                    pos.frozenLongPrice = 1 / ((1 / pos.frozenLongPrice * pos.frozenLongPosition + 1 / order.price * order.volume) / (pos.frozenLongPosition + order.volume));
                }
                pos.frozenLongPosition += order.volume;
            } else if (order.direction == DT_SHORT) {
                if (info.calculateType == 0) {
                    ass.openMarginAmount += order.price * order.volume * info.multiple / it->second.openRealLeverage;
                    pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition + order.price * order.volume) / (pos.frozenShortPosition + order.volume);
                } else if (info.calculateType == 1) {
                    ass.openMarginAmount += order.volume * info.multiple / order.price / it->second.openRealLeverage;
                    pos.frozenShortPrice = 1 / ((1 / pos.frozenShortPrice * pos.frozenShortPosition + 1 / order.price * order.volume) / (pos.frozenShortPosition + order.volume));
                }
                pos.frozenShortPosition += order.volume;
            }
        }
    }
}

void AccountManager::OnDeleteOrder(const stra::QuantOrder& order) {
    auto it = mAccount.find(order.strategyAccountId);
    if (it != mAccount.end()) {
        string instrumentKey = order.instrumentKey;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        double leftVolume = order.volume - order.totalVolumeOnOrder;
        auto& pos = it->second.mPosition[instrumentKey];
        if (order.instType == SPOT) {
            if (order.direction == DT_LONG) {
                auto& ass = it->second.mAsset[info.right];
                ass.frozenAmount -= order.price * leftVolume;
                if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                } else {
                    pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * leftVolume) / (pos.frozenLongPosition - leftVolume);
                    pos.frozenLongPosition -= leftVolume;
                }
            } else if (order.direction == DT_SHORT) {
                auto& ass = it->second.mAsset[info.left];
                ass.frozenAmount -= leftVolume;
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
                auto& ass = it->second.mAsset[info.right];
                ass.frozenAmount -= order.price * leftVolume;
                if (fabs(pos.frozenLongPosition - leftVolume) <= stra::MIN_FLOAT) {
                    pos.frozenLongPrice = -1;
                    pos.frozenLongPosition = 0;
                } else {
                    pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.price * leftVolume) / (pos.frozenLongPosition - leftVolume);
                    pos.frozenLongPosition -= leftVolume; 
                }
            } else if (order.direction == DT_SHORT) {
                auto& ass = it->second.mAsset[info.left];
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
            auto& ass = it->second.mAsset[info.margin];
            if (order.direction == DT_LONG) {
                if (info.calculateType == 0) {
                    ass.openMarginAmount -= order.price * leftVolume * info.multiple / it->second.openRealLeverage;
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
                } else if (info.calculateType == 1) {
                    ass.openMarginAmount -= leftVolume * info.multiple / order.price / it->second.openRealLeverage;
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
                if (info.calculateType == 0) {
                    ass.openMarginAmount -= order.price * leftVolume * info.multiple / it->second.openRealLeverage;
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
                } else if (info.calculateType == 1) {
                    ass.openMarginAmount -= leftVolume * info.multiple / order.price / it->second.openRealLeverage;
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
}

void AccountManager::OnOrder(const stra::QuantOrder& order) {
    auto it = mAccount.find(order.strategyAccountId);
    if (it != mAccount.end()) {
        string instrumentKey = order.instrumentKey;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        auto& pos = it->second.mPosition[instrumentKey];
        if (order.instType == SPOT) {
            if (order.direction == DT_LONG) {
                string asset = order.tradeLongFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeLongFee.amount;
                    ass.totalAmount -= order.tradeLongFee.amount;
                }

                auto leftAss = it->second.mAsset[info.left];
                leftAss.totalAmount += order.tradeVolume;

                auto rightAss = it->second.mAsset[info.right];
                rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
                rightAss.frozenAmount -= order.price * order.tradeVolume;
                double closeVolume = min(order.tradeVolume, pos.shortPosition);
                double openVolume = order.tradeVolume - closeVolume;
                if (closeVolume > stra::MIN_FLOAT) {
                    pos.shortPosition -= closeVolume;
                    if (fabs(pos.shortPosition) <= stra::MIN_FLOAT) {
                        pos.shortPosition = 0;
                        pos.shortAvgPrice = -1;
                    }
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
                string asset = order.tradeShortFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeShortFee.amount;
                    ass.totalAmount -= order.tradeShortFee.amount;
                }

                auto leftAss = it->second.mAsset[info.left];
                leftAss.totalAmount -= order.tradeVolume;
                leftAss.frozenAmount -= order.tradeVolume;

                auto rightAss = it->second.mAsset[info.right];
                rightAss.totalAmount += order.tradePrice * order.tradeVolume;
                
                double closeVolume = min(order.tradeVolume, pos.longPosition);
                double openVolume = order.tradeVolume - closeVolume;
                if (closeVolume > stra::MIN_FLOAT) {
                    pos.longPosition -= closeVolume;
                    if (fabs(pos.longPosition) <= stra::MIN_FLOAT) {
                        pos.longPosition = 0;
                        pos.longAvgPrice = -1;
                    }
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
                string asset = order.tradeLongFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeLongFee.amount;
                    ass.totalAmount -= order.tradeLongFee.amount;
                }

                if (order.marginType == stra::MarginType_NORMAL) {
                    auto& leftAss = it->second.mAsset[info.left];
                    leftAss.totalAmount += order.tradeVolume;

                    auto& rightAss = it->second.mAsset[info.right];
                    rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
                    rightAss.frozenAmount -= order.price * order.tradeVolume;
                } else if (order.marginType == stra::MarginType_BORROW) {
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    if (rightAss.totalAmount > order.tradePrice * order.tradeVolume) {
                        leftAss.totalAmount += order.tradeVolume;
                        rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
                        rightAss.frozenAmount -= order.price * order.tradeVolume;
                    } else {
                        double loanAmount = order.tradePrice * order.tradeVolume - rightAss.totalAmount;
                        leftAss.totalAmount += order.tradeVolume;
                        rightAss.totalAmount = 0;
                        rightAss.loanAmount += loanAmount;
                        rightAss.frozenAmount -= order.price * order.tradeVolume;
                    }
                } else if (order.marginType == stra::MarginType_REPAY) {
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    if (fabs(leftAss.loanAmount) <= stra::MIN_FLOAT) {
                        leftAss.totalAmount += order.tradeVolume;
                        rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
                        rightAss.frozenAmount -= order.price * order.tradeVolume;
                    } else {
                        double returnAmount = min(leftAss.loanAmount, order.tradeVolume);
                        leftAss.loanAmount -= returnAmount;
                        leftAss.totalAmount += order.tradeVolume - returnAmount;
                        rightAss.totalAmount -= order.tradePrice * order.tradeVolume;
                        rightAss.frozenAmount -= order.price * order.tradeVolume;
                    }
                }

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
                string asset = order.tradeShortFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeShortFee.amount;
                    ass.totalAmount -= order.tradeShortFee.amount;
                }

                if (order.marginType == stra::MarginType_NORMAL) {
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    leftAss.totalAmount -= order.tradeVolume;
                    leftAss.frozenAmount -= order.tradeVolume;
                    rightAss.totalAmount += order.tradePrice * order.tradeVolume;
                } else if (order.marginType == stra::MarginType_BORROW) {
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    if (leftAss.totalAmount > order.tradeVolume) {
                        leftAss.totalAmount -= order.tradeVolume;
                        leftAss.frozenAmount -= order.tradeVolume;
                        rightAss.totalAmount += order.tradePrice * order.tradeVolume;
                    } else {
                        double loanAmount = order.tradeVolume - leftAss.totalAmount;
                        leftAss.totalAmount = 0;
                        leftAss.loanAmount += loanAmount;
                        leftAss.frozenAmount -= order.tradeVolume;
                        rightAss.totalAmount = order.tradePrice * order.tradeVolume;
                    }
                } else if (order.marginType == stra::MarginType_REPAY) {
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    if (fabs(rightAss.loanAmount) <= stra::MIN_FLOAT) {
                        leftAss.totalAmount -= order.tradeVolume;
                        leftAss.frozenAmount -= order.tradeVolume;
                        rightAss.totalAmount += order.tradePrice * order.tradeVolume;
                    } else {
                        double returnAmount = min(rightAss.loanAmount, order.tradeVolume * order.tradePrice);
                        rightAss.loanAmount -= returnAmount;
                        rightAss.totalAmount += order.tradePrice * order.tradeVolume - returnAmount;
                        leftAss.totalAmount -= order.tradeVolume;
                        leftAss.frozenAmount -= order.tradeVolume;
                    }
                }

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
                string asset = order.tradeLongFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeLongFee.amount;
                    ass.totalAmount -= order.tradeLongFee.amount;
                }

                double closeVolume = min(order.tradeVolume , pos.shortPosition);
                double openVolume = order.tradeVolume - closeVolume;
                if (closeVolume > stra::MIN_FLOAT) {
                    pos.shortPosition -= closeVolume;
                    auto& leftAss = it->second.mAsset[info.left];
                    if (info.calculateType == 0) {
                        double closeAmount = (pos.shortAvgPrice - order.tradePrice) * closeVolume * info.multiple;
                        leftAss.totalAmount += closeAmount;
                        leftAss.closeAmount += closeAmount;
                        pos.closeAmount += closeAmount;
                    } else if (info.calculateType == 1) {
                        double closeAmount = (1 / order.tradePrice - 1 / pos.shortAvgPrice) * closeVolume * info.multiple;
                        leftAss.totalAmount += closeAmount;
                        leftAss.closeAmount += closeAmount;
                        pos.closeAmount += closeAmount;
                    }

                    if (fabs(pos.shortPosition) <= stra::MIN_FLOAT) {
                        pos.shortPosition = 0;
                        pos.shortAvgPrice = -1;
                    }
                }

                if (openVolume > stra::MIN_FLOAT) {
                    if (info.calculateType == 0) {
                        pos.longAvgPrice = (pos.longAvgPrice * pos.longPosition + order.tradePrice * openVolume) / (pos.longPosition + openVolume);
                        pos.longPosition += openVolume;
                    } else if (info.calculateType == 1) {
                        pos.longAvgPrice = 1 / ((1 / pos.longAvgPrice * pos.longPosition + 1 / order.tradePrice * openVolume) / (pos.longPosition + openVolume));
                        pos.longPosition += openVolume;
                    }
                }

                if (info.calculateType == 0) {
                    auto& ass = it->second.mAsset[info.margin];
                    if (fabs(pos.frozenLongPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                        pos.frozenLongPrice = (pos.frozenLongPrice * pos.frozenLongPosition - order.tradeVolume * order.price) / (pos.frozenLongPosition - order.tradeVolume);
                        pos.frozenLongPosition -= order.tradeVolume;
                    } else {
                        pos.frozenLongPrice = -1;
                        pos.frozenLongPosition = 0;
                    }
                    ass.openMarginAmount -= order.price * order.tradeVolume * info.multiple / it->second.openRealLeverage;
                } else if (info.calculateType == 1) {
                    auto& ass = it->second.mAsset[info.margin];
                    if (fabs(pos.frozenLongPosition - order.tradeVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * order.tradeVolume) <= stra::MIN_FLOAT) {
                        pos.frozenLongPrice = -1;
                        pos.frozenLongPosition = 0;
                    } else {
                        pos.frozenLongPrice = 1 / ((1 / pos.frozenLongPrice * pos.frozenLongPosition - 1 / order.price * order.tradeVolume) / (pos.frozenLongPosition - order.tradeVolume));
                        pos.frozenLongPosition -= order.tradeVolume;
                    }
                    ass.openMarginAmount -= order.tradeVolume * info.multiple / order.price / it->second.openRealLeverage;
                }
            } else if (order.direction == DT_SHORT) {
                string asset = order.tradeShortFee.asset;
                auto iter = it->second.mAsset.find(asset);
                if (iter != it->second.mAsset.end()) {
                    auto& ass = it->second.mAsset[asset];
                    ass.feeAmount += order.tradeShortFee.amount;
                    ass.totalAmount -= order.tradeShortFee.amount;
                }

                double closeVolume = min(order.tradeVolume , pos.longPosition);
                double openVolume = order.tradeVolume - closeVolume;
                if (closeVolume > stra::MIN_FLOAT) {
                    pos.longPosition -= closeVolume;
                    auto& leftAss = it->second.mAsset[info.left];
                    auto& rightAss = it->second.mAsset[info.right];
                    if (info.calculateType == 0) {
                        double closeAmount = (order.tradePrice - pos.longAvgPrice) * closeVolume * info.multiple;
                        leftAss.totalAmount += closeAmount;
                        rightAss.closeAmount += closeAmount;
                        pos.closeAmount += closeAmount;
                    } else if (info.calculateType == 1) {
                        double closeAmount = (1 / pos.longAvgPrice - 1 / order.tradePrice) * closeVolume * info.multiple;
                        rightAss.totalAmount += closeAmount;
                        rightAss.closeAmount += closeAmount;
                        pos.closeAmount += closeAmount;
                    }

                    if (fabs(pos.longPosition) <= stra::MIN_FLOAT) {
                        pos.longPosition = 0;
                        pos.longAvgPrice = -1;
                    }
                }

                if (openVolume > stra::MIN_FLOAT) {
                    if (info.calculateType == 0) {
                        pos.shortAvgPrice = (pos.shortAvgPrice * pos.shortPosition + order.tradePrice * openVolume) / (pos.shortPosition + openVolume);
                        pos.shortPosition += openVolume;
                    } else if (info.calculateType == 1) {
                        pos.shortAvgPrice = 1 / ((1 / pos.shortAvgPrice * pos.shortPosition + 1 / order.tradePrice * openVolume) / (pos.shortPosition + openVolume));
                        pos.shortPosition += openVolume;
                    }
                }

                if (info.calculateType == 0) {
                    auto& ass = it->second.mAsset[info.margin];
                    if (fabs(pos.frozenShortPosition - order.tradeVolume) > stra::MIN_FLOAT) {
                        pos.frozenShortPrice = (pos.frozenShortPrice * pos.frozenShortPosition - order.tradeVolume * order.price) / (pos.frozenShortPosition - order.tradeVolume);
                        pos.frozenShortPosition -= order.tradeVolume;
                    } else {
                        pos.frozenShortPrice = -1;
                        pos.frozenShortPosition = 0;
                    }
                    ass.openMarginAmount -= order.price * order.tradeVolume * info.multiple / it->second.openRealLeverage;
                } else if (info.calculateType == 1) {
                    auto& ass = it->second.mAsset[info.margin];
                    if (fabs(pos.frozenShortPosition - order.tradeVolume) <= stra::MIN_FLOAT || fabs(1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * order.tradeVolume) <= stra::MIN_FLOAT) {
                        pos.frozenShortPrice = -1;
                        pos.frozenShortPosition = 0;
                    } else {
                        pos.frozenShortPrice = 1 / ((1 / pos.frozenShortPrice * pos.frozenShortPosition - 1 / order.price * order.tradeVolume) / (pos.frozenShortPosition - order.tradeVolume));
                        pos.frozenShortPosition -= order.tradeVolume;
                    }
                    ass.openMarginAmount -= order.tradeVolume * info.multiple / order.price / it->second.openRealLeverage;
                }
            }
        }
    }
}

void AccountManager::UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth) {
    if (depth.instType == USDT_SWAP || depth.instType == USDT_FUTURES || depth.instType == BUSD_SWAP || depth.instType == C_SWAP || depth.instType == C_FUTURES) {
        double lastPrice = (depth.vAskPrice[0] + depth.vBidPrice[0]) / 2;
        string instrumentKey = stra::ExchangeTypeEnum2Str[depth.exchangeType] + "." + stra::InstTypeEnum2Str[depth.instType] + "." + depth.instrument;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        string affectAsset = info.margin;
        for (auto it = mAccount.begin(); it != mAccount.end(); ++it) {
            auto& ass = it->second.mAsset[affectAsset];
            ass.floatAmount = 0;
            ass.positionValue = 0;
            auto iu = it->second.mPosition.find(instrumentKey);
            if (iu != it->second.mPosition.end()) {
                auto& pos = it->second.mPosition[instrumentKey];
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

                if (info.calculateType == 0) {
                    pos.positionValue = (pos.longPosition + pos.shortPosition) * lastPrice * info.multiple;
                } else if (info.calculateType == 1) {
                    pos.positionValue = (pos.longPosition + pos.shortPosition) / lastPrice * info.multiple;
                }
  
                for (auto iw = it->second.mPosition.begin(); iw != it->second.mPosition.end(); ++iw) {
                    string instrumentKey = iw->second.instrumentKey;
                    stra::InstrumentInfo& in = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
                    vector<string> v;
                    splitString(instrumentKey, v, ".");
                    stra::InstType instType = stra::InstTypeStr2Enum[v[2]];
                    if ((instType == stra::FUTURES || instType == stra::SWAP || instType == stra::InstType_USDT_SWAP || instType == stra::InstType_BUSD_SWAP || instType == stra::InstType_USDT_FUTURES || instType == stra::InstType_C_SWAP || instType == stra::InstType_C_FUTURES) && in.margin == affectAsset) {
                        ass.floatAmount += iw->second.floatAmount;
                        ass.positionValue += iw->second.positionValue;
                    }
                }
                ass.marginAmount = ass.positionValue / it->second.maxRealLeverage;
            }
        }
    }
}

bool AccountManager::FundVerifyClassic(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info) {
    auto it = mAccount.find(order.strategyAccountId);
    if (it != mAccount.end()) {
        if (order.instType == SPOT || order.instType == MARGIN) {
            if (order.direction == DT_LONG) {
                stra::AssetUnit& ass = it->second.mAsset[info.instRight];
                double availableAssetAmount = ass.totalAmount + ass.floatAmount - ass.frozenAmount - ass.marginAmount - ass.openMarginAmount;
                double requireAmount = 1.1 * order.price * order.volume - availableAssetAmount;
                LOG_INFO("FundVerify strategyAccountId:%d instrumentKey:%s instRight:%s totalAmount:%f floatAmount:%f frozenAmount:%f availableAssetAmount:%f  requireAmount:%f", order.strategyAccountId, order.instrumentKey, info.instRight.c_str(), ass.totalAmount, ass.floatAmount, ass.frozenAmount, availableAssetAmount, requireAmount);
                if (requireAmount < stra::MIN_FLOAT) {
                    return true;
                } else {
                    return false;
                }
            } else {
                stra::AssetUnit& ass = it->second.mAsset[info.instLeft];
                double availableAssetAmount = ass.totalAmount + ass.floatAmount - ass.frozenAmount - ass.marginAmount - ass.openMarginAmount;
                double requireAmount = 1.1 * order.price * order.volume - availableAssetAmount;
                LOG_INFO("FundVerify strategyAccountId:%d instrumentKey:%s instRight:%s totalAmount:%f floatAmount:%f frozenAmount:%f availableAssetAmount:%f  requireAmount:%f", order.strategyAccountId, order.instrumentKey, info.instRight.c_str(), ass.totalAmount, ass.floatAmount, ass.frozenAmount, availableAssetAmount, requireAmount);
                if (requireAmount < stra::MIN_FLOAT) {
                    return true;
                } else {
                    return false;
                }
            }
        } else if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES || order.instType == BUSD_SWAP || order.instType == C_SWAP || order.instType == C_FUTURES) {
            if (order.direction == DT_LONG) {
                stra::AssetUnit& ass = it->second.mAsset[info.margin];
                double availableAssetAmount = ass.totalAmount + ass.floatAmount - ass.frozenAmount - ass.marginAmount - ass.openMarginAmount;
                if (order.isActiveOrder){
                    availableAssetAmount = min(ass.totalAmount + ass.floatAmount - ass.positionValue / it->second.maxRealLeverage, availableAssetAmount);
                } else{
                    availableAssetAmount = min(ass.totalAmount + ass.floatAmount - ass.positionValue / it->second.passiveMaxRealLeverage, availableAssetAmount);
                }
                stra::PositionUnit& pos = it->second.mPosition[order.instrumentKey];
                double closeVolume = min(order.volume, pos.shortPosition);
                double openVolume = order.volume - closeVolume;
                double requireMargin = 0.0;
                if (info.calculateType == 0) {
                    requireMargin = openVolume * order.price * info.multiple / it->second.openRealLeverage;
                } else if (info.calculateType == 1) {
                    requireMargin = openVolume / order.price * info.multiple / it->second.openRealLeverage;
                } else {
                    requireMargin = 0.0;
                }
                double requireAmount = 1.1 * requireMargin - availableAssetAmount;
                if (openVolume < closeVolume){
                    // 如果订单主要是在平仓，则无论可用是多少都允许平仓
                    requireAmount = 0;
                }
                LOG_INFO("FundVerify strategyAccountId:%d margin:%s instrumentKey:%s order.volume:%f pos.shortPosition:%f totalAmount:%f floatAmount:%f frozenAmount:%f positionValue:%f availableAssetAmount:%f requireMargin:%f requireAmount:%f", order.strategyAccountId, info.margin.c_str(), order.instrumentKey, order.volume, pos.shortPosition, ass.totalAmount, ass.floatAmount, ass.frozenAmount, ass.positionValue, availableAssetAmount, requireMargin, requireAmount);
                if (requireAmount < stra::MIN_FLOAT) {
                    return true;
                } else {
                    return false;
                }
            } else {
                stra::AssetUnit& ass = it->second.mAsset[info.margin];
                double availableAssetAmount = ass.totalAmount + ass.floatAmount - ass.frozenAmount - ass.marginAmount - ass.openMarginAmount;
		        if (order.isActiveOrder) {
                    availableAssetAmount = min(ass.totalAmount + ass.floatAmount - ass.positionValue / it->second.maxRealLeverage, availableAssetAmount);
                } else {
                    availableAssetAmount = min(ass.totalAmount + ass.floatAmount - ass.positionValue / it->second.passiveMaxRealLeverage, availableAssetAmount);
                }
                stra::PositionUnit& pos = it->second.mPosition[order.instrumentKey];
                double closeVolume = min(order.volume, pos.longPosition);
                double openVolume = order.volume - closeVolume;
                double requireMargin = 0.0;
                if (info.calculateType == 0) {
                    requireMargin = openVolume * order.price * info.multiple / it->second.openRealLeverage;
                } else if (info.calculateType == 1) {
                    requireMargin = openVolume / order.price * info.multiple / it->second.openRealLeverage;
                } else {
                    requireMargin = 0.0;
                }
                double requireAmount = 1.1 * requireMargin - availableAssetAmount;
                if (openVolume < closeVolume){
                    // 如果订单主要是在平仓，则无论可用是多少都允许平仓
                    requireAmount = 0;
                }
                LOG_INFO("FundVerify strategyAccountId:%d margin:%s instrumentKey:%s order.volume:%f pos.longPosition:%f pos.shortPosition:%f totalAmount:%f floatAmount:%f frozenAmount:%f positionValue:%f availableAssetAmount:%f  requireMargin:%f requireAmount:%f", order.strategyAccountId, info.margin.c_str(), order.instrumentKey, order.volume, pos.longPosition, pos.shortPosition, ass.totalAmount, ass.floatAmount, ass.frozenAmount, ass.positionValue, availableAssetAmount, requireMargin, requireAmount);
                if (requireAmount < stra::MIN_FLOAT) {
                    return true;
                } else {
                    return false;
                }
            }
        }
    } else {
        LOG_INFO("FundVerify cannot find accountId:%d", order.strategyAccountId);
    }
    return false;
}

bool AccountManager::FundVerify(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info) {
    if (!check) {
        return true;
    }
    auto it = mAccount.find(order.strategyAccountId);
    if (it != mAccount.end()) {
        if (it->second.accountType == stra::AT_UNIFIED) {
            return FundVerifyUnified(order, assetTick, info);
        } else {
            return FundVerifyClassic(order, assetTick, info);
        }
    } else {
        return false;
    }
}

bool AccountManager::FundVerifyUnified(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info) {
    auto it = mAccount.find(order.strategyAccountId); // 找到对应的物理账号quantAccount
    if (it != mAccount.end()) {
        double orderValue = 0.0;
        // 计算order的交易价值
        if (info.calculateType == 0) {
            orderValue = order.price * order.volume * info.multiple;
        } else {
            orderValue = order.volume * info.multiple;
        }
        double totalEquity = it->second.totalEquity;
        double adjEquity = it->second.adjEquity;
        double mmr = it->second.mmr;
        double mgnRatio = it->second.mgnRatio;
	//LOG_INFO("FundVerifyUnified  orderValue:%f adjEquity:%f mmr:%f mgnRatio:%f openActiveMgnRatio:%f openPassiveMgnRatio:%f", orderValue, adjEquity, mmr, mgnRatio, it->second.openActiveMgnRatio, it->second.openPassiveMgnRatio);
        if (mmr <= stra::MIN_FLOAT){
            // 如果交易所推送错误的mmr, 如何规避风险
            for (auto itPos = it->second.mPosition.begin(); itPos != it->second.mPosition.end(); ++itPos) {
                if (itPos->second.positionValue > 0) {
		            LOG_INFO("FundVerifyUnified  exchange error! mmr < 0 when position value > 0");
                    return false;
                }
            }
        }

        // 平仓不需要验资
        if (order.tradingTypeOffset == stra::CLOSE_LONG || order.tradingTypeOffset == stra::CLOSE_SHORT) {
            return true;
        }

        if (order.isActiveOrder){
            if (adjEquity / (mmr + orderValue) > it->second.openActiveMgnRatio && mgnRatio > it->second.openActiveMgnRatio) {
                return true;
            } else {
                return false;
            }
        } else{
            if (adjEquity / (mmr + orderValue) > it->second.openPassiveMgnRatio && mgnRatio > it->second.openPassiveMgnRatio) {
                return true;
            } else {
                return false;
            }
        }
    } else {
        LOG_INFO("FundVerify cannot find accountId:%d", order.strategyAccountId);
    }
    return false;
}

void AccountManager::LoadFromFile(string filePath) {
    std::ifstream accountFile(filePath.c_str());
    if (!accountFile) {
        LOG_INFO("File: %s does not exist!", filePath.c_str());
        return;
    }

    json accountJson;
    accountFile >> accountJson;

    for (auto j = accountJson.begin(); j != accountJson.end(); ++j) {
        stra::QuantAccount account;
        json accountInfo = j.value();
        auto i = accountInfo.find("strategyAccountId");
        if (i != accountInfo.end()) {
            account.strategyAccountId = int(i.value());
        }

        string strategyAccountName = "";
        i = accountInfo.find("strategyAccountName");
        if (i != accountInfo.end()) {
            strategyAccountName = string(i.value());
        }
        
        i = accountInfo.find("systemAccountId");
        if (i != accountInfo.end()) {
            account.systemAccountId = int(i.value());
        }

        i = accountInfo.find("physicalAccountId");
        if (i != accountInfo.end()) {
            account.physicalAccountId = int(i.value());
        }

        i = accountInfo.find("accountType");
        if (i != accountInfo.end()) {
            account.accountType = stra::AccountTypeStr2Enum[string(i.value())];
        }

        i = accountInfo.find("marginType");
        if (i != accountInfo.end()) {
            account.marginType = stra::AccountMarginTypeStr2Enum[string(i.value())];
        }

        i = accountInfo.find("accountMarginType");
        if (i != accountInfo.end()) {
            account.accountMarginType = stra::AccountMarginTypeStr2Enum[string(i.value())];
        }

        i = accountInfo.find("openRealLeverage");
        if (i != accountInfo.end()) {
            account.openRealLeverage = double(i.value());
        }

        mAccount[account.strategyAccountId] = account;
        mAccountNameAccountId[strategyAccountName] = account.strategyAccountId;
    }
}

void AccountManager::SaveToFile(string filePath) {
}
