#include "DataManager.h"
#include "Utility.h"


DataManager& DataManager::Instance() {
    static DataManager dataManager;
    return dataManager;
}

DataManager::DataManager() {

}

DataManager::~DataManager() {
    mDKlinePara.clear();
    mKlinePara.clear();
    mFrate.clear();
    mKline.clear();
    mDKline.clear();
    mDepth.clear();
    mTrade.clear();
}

void DataManager::AddDKlinePara(string instrumentKey, int period, int length) {
    auto it = mDKlinePara.find(instrumentKey);
    if (it != mDKlinePara.end()) {
        it->second[period] = length;
    } else {
        unordered_map<int, int> m;
        m[period] = length;
        mDKlinePara[instrumentKey] = m;
    }
}

void DataManager::AddKlinePara(string instrumentKey, int period, int length) {
    auto it = mKlinePara.find(instrumentKey);
    if (it != mKlinePara.end()) {
        it->second[period] = length;
    } else {
        unordered_map<int, int> m;
        m[period] = length;
        mKlinePara[instrumentKey] = m;
    }
}

void DataManager::AddDepthPara(string instrumentKey, int length) {
    auto iter = mDepthPara.find(instrumentKey);
    if (iter != mDepthPara.end()) {
        return;
    }
    
    mDepthPara[instrumentKey] = length;
    mDepth[instrumentKey] = DataArray<stra::QuantMarketDepth>(length);
}

void DataManager::AddTradePara(string instrumentKey, int length) {
    mTradePara[instrumentKey] = length;
}

void DataManager::AddFratePara(string instrumentKey, int length) {
    mFratePara[instrumentKey] = length;
}

void DataManager::InitMarketKline() {
    for (auto it = mDKline.begin(); it != mDKline.end(); ++it) {
        string instrumentKey = it->first;
        auto iu = it->second.find(60);
        if (iu != it->second.end()) {
            auto iw = mDKlinePara.find(instrumentKey);
            if (iw != mDKlinePara.end()) {
                for (auto ix = iw->second.begin(); ix != iw->second.end(); ++ix) {
                    int period = ix->first;
                    if (period != 60) {
                        for (int i = iu->second.GetStartIndex(); i >= iu->second.GetEndIndex(); ++i) {
                            stra::QuantKline srcKline = iu->second[i];
                            int64_t barTime = srcKline.timestamp / period * period;
                            if (it->second[period].IsEmpty()) {
                                stra::QuantKline kline;
                                kline.timestamp = barTime;
                                kline.exchangeType = srcKline.exchangeType;
                                strncpy(kline.instrument, srcKline.instrument, stra::INST_ID_LEN);
                                kline.instType = srcKline.instType;
                                kline.open = srcKline.open;
                                kline.high = srcKline.high;
                                kline.low = srcKline.low;
                                kline.close = srcKline.close;
                                kline.volume = srcKline.volume;
                                kline.amount = srcKline.amount;
                                kline.count = srcKline.count;
                                kline.buyVolume = srcKline.buyVolume;
                                kline.buyAmount = srcKline.buyAmount;
                                kline.buyCount = 0;
                                kline.sellVolume = srcKline.sellVolume;
                                kline.sellAmount = srcKline.sellAmount;
                                kline.sellCount = 0;
                                kline.updateTime = srcKline.updateTime;
                                kline.process = min((srcKline.updateTime - barTime) / (double)period, 1.0);
                                it->second[period].Add(kline);
                            } else {
                                int endIndex = it->second[period].GetEndIndex();
                                stra::QuantKline& endKline = it->second[period][endIndex];
                                if (barTime == endKline.timestamp) {
                                    endKline.high = max(endKline.high, srcKline.high);
                                    endKline.low = min(endKline.low, srcKline.low);
                                    endKline.count += srcKline.count;
                                    endKline.volume += srcKline.volume;
                                    endKline.amount += srcKline.amount;
                                    endKline.buyVolume += srcKline.buyVolume;
                                    endKline.buyAmount += srcKline.buyAmount;
                                    endKline.sellVolume += srcKline.sellVolume;
                                    endKline.sellAmount += srcKline.sellAmount;
                                    endKline.updateTime = srcKline.updateTime;
                                    endKline.process = min((srcKline.updateTime - barTime) / (double)period, 1.0);
                                } else {
                                    stra::QuantKline kline;
                                    kline.timestamp = barTime;
                                    kline.exchangeType = srcKline.exchangeType;
                                    strncpy(kline.instrument, srcKline.instrument, stra::INST_ID_LEN);
                                    kline.instType = srcKline.instType;
                                    kline.open = srcKline.open;
                                    kline.high = srcKline.high;
                                    kline.low = srcKline.low;
                                    kline.close = srcKline.close;
                                    kline.volume = srcKline.volume;
                                    kline.amount = srcKline.amount;
                                    kline.count = srcKline.count;
                                    kline.buyVolume = srcKline.buyVolume;
                                    kline.buyAmount = srcKline.buyAmount;
                                    kline.buyCount = 0;
                                    kline.sellVolume = srcKline.sellVolume;
                                    kline.sellAmount = srcKline.sellAmount;
                                    kline.sellCount = 0;
                                    kline.updateTime = srcKline.updateTime;
                                    kline.process = min((srcKline.updateTime - barTime) / (double)period, 1.0);
                                    it->second[period].Add(kline);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void DataManager::OnMarketKline(string channel, const stra::MdKline& srcKline) {
    vector<string> v;
    splitString(channel, v, ".");
    if (v.size() <= 4) {
        return;
    }

    int64_t currentTime = GetCurrentTime();
    string exchangeId = v[0];
    string instrumentType = v[1];
    string instrument = v[3];
    string marketType = v[4];
    string instrumentKey = exchangeId + "|" + instrument + "|" + instrumentType;
    
    int64_t oneTT = 1000000;
    int64_t oneTT60 = 60 * 1000000;
    int64_t barTime = srcKline.tsNet / oneTT;  // ? barTime从哪里获取
    int64_t endTime = (srcKline.tsNet + oneTT60) / 1000000;
    int64_t exchangeTime = srcKline.ts / oneTT;
    
    vector<string> vPeriod;
    splitString(marketType, vPeriod, "_");
    if (vPeriod.size() <= 2) {
        return;
    }
    string periodStr = vPeriod[1].substr(1, 1);
    string periodNum = vPeriod[1].substr(0, 1);
    string periodType = vPeriod[1].substr(1, 1);
    int period = 0;
    if (periodType == "d") {
        period = stoi(periodNum) * 1440 * 60;
    } else if (periodType == "m") {
        period = stoi(periodNum) * 60;
    } else if (periodType == "s") {
        period = stoi(periodNum);
    }

    auto it = mDKline.find(instrumentKey);
    if (it != mDKline.end()) {
        auto iu = it->second.find(period);
        if (iu != it->second.end()) {
            if (iu->second.IsEmpty() || (barTime - iu->second.GetEndValue().timestamp) > 0) {
                stra::QuantKline kline;
                kline.timestamp = barTime;
                kline.exchangeType = stra::ExchangeTypeStr2Enum[exchangeId];
                strncpy(kline.instrument, srcKline.instrument, stra::INST_ID_LEN);
                kline.instType = stra::InstTypeStr2Enum[instrumentType];
                kline.open = srcKline.openPrice;
                kline.high = srcKline.highPrice;
                kline.low = srcKline.lowPrice;
                kline.close = srcKline.closePrice;
                kline.volume = srcKline.totalVolume;
                kline.amount = srcKline.totalAmount;
                kline.count = srcKline.numOfTrade;
                kline.buyVolume = srcKline.takerLongVolume;
                kline.buyAmount = srcKline.takerLongAmount;
                kline.buyCount = 0;
                kline.sellVolume = srcKline.takerShortVolume;
                kline.sellAmount = srcKline.takerShortAmount;
                kline.sellCount = 0;
                kline.updateTime = endTime;
                kline.process = min((exchangeTime - barTime) / (double)period, 1.0);
                iu->second.Add(kline);
            } else {
                for (int i = iu->second.GetStartIndex(); i <= iu->second.GetEndIndex(); ++i) {
                    if (iu->second[i].timestamp == barTime) {
                        stra::QuantKline& indexKline = iu->second[i];
                        indexKline.open = srcKline.openPrice;
                        indexKline.high = srcKline.highPrice;
                        indexKline.low = srcKline.lowPrice;
                        indexKline.close = srcKline.closePrice;
                        indexKline.volume = srcKline.totalVolume;
                        indexKline.amount = srcKline.totalAmount;
                        indexKline.count = srcKline.numOfTrade;
                        indexKline.buyVolume = srcKline.takerLongVolume;
                        indexKline.buyAmount = srcKline.takerLongAmount;
                        indexKline.buyCount = 0;
                        indexKline.sellVolume = srcKline.takerShortVolume;
                        indexKline.sellAmount = srcKline.takerShortAmount;
                        indexKline.sellCount = 0;
                        indexKline.updateTime = currentTime;
                        indexKline.process = min((exchangeTime - barTime) / (double)period, 1.0);
                        break;
                    }
                }
            }

            if (period == 60) {
                for (auto iw = it->second.begin(); iw != it->second.end(); ++iw) {
                    int iwPeriod = iw->first;
                    stra::QuantKline minutekline; // ? 是哪个kline
                    int64_t barTime = minutekline.timestamp / iwPeriod * iwPeriod;
                    if (iwPeriod != 60) {
                        if (iw->second.IsEmpty()) {
                            stra::QuantKline kline;
                            kline.timestamp = barTime;
                            kline.exchangeType = minutekline.exchangeType;
                            strncpy(kline.instrument, minutekline.instrument, stra::INST_ID_LEN);
                            kline.instType = minutekline.instType;
                            kline.open = minutekline.open;
                            kline.high = minutekline.high;
                            kline.low = minutekline.low;
                            kline.close = minutekline.close;
                            kline.volume = minutekline.volume;
                            kline.amount = minutekline.amount;
                            kline.count = minutekline.count;
                            kline.buyVolume = minutekline.buyVolume;
                            kline.buyAmount = minutekline.buyAmount;
                            kline.buyCount = 0;
                            kline.sellVolume = minutekline.sellVolume;
                            kline.sellAmount = minutekline.sellAmount;
                            kline.sellCount = 0;
                            kline.updateTime = minutekline.updateTime;
                            kline.process = min((minutekline.updateTime - barTime) / (double)iwPeriod, 1.0);
                            iw->second.Add(kline);
                        } else {
                            int endIndex = iw->second.GetEndIndex();
                            stra::QuantKline& endKline = iw->second[endIndex];
                            if (barTime == endKline.timestamp) {
                                if (minutekline.updateTime - endKline.updateTime > 1) {
                                    endKline.open = endKline.open;
                                    endKline.high = max(endKline.high, minutekline.high);
                                    endKline.low = min(endKline.low, minutekline.low);
                                    endKline.close = minutekline.close;
                                    endKline.count += minutekline.count;
                                    endKline.volume += minutekline.volume;
                                    endKline.amount += minutekline.amount;
                                    endKline.buyVolume += minutekline.buyVolume;
                                    endKline.buyAmount += minutekline.buyAmount;
                                    endKline.sellVolume += minutekline.sellVolume;
                                    endKline.sellAmount += minutekline.sellAmount;
                                    endKline.updateTime = minutekline.updateTime;
                                    endKline.process = min((minutekline.updateTime - barTime) / (double)iwPeriod, 1.0);
                                }
                            } else {
                                endKline.process = 1;
                                stra::QuantKline kline;
                                kline.timestamp = barTime;
                                kline.exchangeType = minutekline.exchangeType;
                                strncpy(kline.instrument, minutekline.instrument, stra::INST_ID_LEN);
                                kline.instType = minutekline.instType;
                                kline.open = minutekline.open;
                                kline.high = minutekline.high;
                                kline.low = minutekline.low;
                                kline.close = minutekline.close;
                                kline.volume = minutekline.volume;
                                kline.amount = minutekline.amount;
                                kline.count = minutekline.count;
                                kline.buyVolume = minutekline.buyVolume;
                                kline.buyAmount = minutekline.buyAmount;
                                kline.buyCount = 0;
                                kline.sellVolume = minutekline.sellVolume;
                                kline.sellAmount = minutekline.sellAmount;
                                kline.sellCount = 0;
                                kline.updateTime = minutekline.updateTime;
                                kline.process = min((minutekline.updateTime - barTime) / (double)iwPeriod, 1.0);
                                iw->second.Add(kline);
                            }
                        }
                    }
                }
            }
        }
    }
}

void DataManager::OnMarketDepth(string channel, const stra::MdDepth& srcDepth, int64_t eventTime) {
    vector<string> v;
    splitString(channel, v, ".");
    if (v.size() <= 4) {
        return;
    }

    int64_t currentTime = GetCurrentTime();
    string exchangeId = v[0];
    string instrumentType = v[1];
    string instrument = v[3];
    string marketType = v[4];
    string instrumentKey = exchangeId + "|" + instrument + "|" + instrumentType;

    int64_t oneTT = 1000000;
    int64_t exchangeTime = 0;
    int64_t arriveTime = 0;
    int64_t platformTime = 0;
    
    if (srcDepth.ts > 0) {
        exchangeTime = srcDepth.ts / oneTT;
    } else {
        exchangeTime = currentTime;
    }

    if (srcDepth.tsNet > 0) {
        arriveTime = srcDepth.tsNet / oneTT;
    } else {
        arriveTime = currentTime;
    }

    if (srcDepth.tsParse > 0) {
        platformTime = srcDepth.tsParse / oneTT;
    } else {
        platformTime = currentTime;
    }
    
    auto it = mDepth.find(instrumentKey);
    if (it != mDepth.end()) {
        stra::QuantMarketDepth depth;
        depth.timestamp = currentTime;
        depth.exchangeTime = exchangeTime;
        depth.arriveTime = arriveTime;
        depth.platformTime = platformTime;
        depth.eventTime = eventTime;
        strncpy(depth.instrument, instrument.c_str(), stra::INST_ID_LEN);
        depth.instType = stra::InstTypeStr2Enum[instrumentType];
        depth.vAskPrice = srcDepth.vAskPrice;
        depth.vAskVolume = srcDepth.vAskVolume;
        depth.vBidPrice = srcDepth.vBidPrice;
        depth.vBidVolume = srcDepth.vBidVolume;

        if (!it->second.IsEmpty()) {
            int endIndex = it->second.GetEndIndex();
            stra::QuantMarketDepth& endDepth = it->second[endIndex];
            depth.avgInterval = min(max((currentTime - endDepth.timestamp) * 1000 / 50 + endDepth.avgInterval * 49 / 50, endDepth.minInterval), endDepth.maxInterval);
        }

        it->second.Add(depth);
    }
}

void DataManager::OnFundingRate(string channel, const stra::MdFrate& srcFrate) {
    vector<string> v;
    splitString(channel, v, ".");
    if (v.size() <= 4) {
        return;
    }

    int64_t currentTime = GetCurrentTime();
    string exchangeId = v[0];
    string instrumentType = v[1];
    string instrument = v[3];
    string marketType = v[4];
    string instrumentKey = exchangeId + "|" + instrument + "|" + instrumentType;

    int64_t oneTT = 1000000;
    int64_t exchangeTime = 0;
    int64_t arriveTime = 0;
    int64_t platformTime = 0;
    
    if (srcFrate.ts > 0) {
        exchangeTime = srcFrate.ts / oneTT;
    } else {
        exchangeTime = currentTime;
    }

    if (srcFrate.tsNet > 0) {
        arriveTime = srcFrate.tsNet / oneTT;
    } else {
        arriveTime = currentTime;
    }

    if (srcFrate.tsParse > 0) {
        platformTime = srcFrate.tsParse / oneTT;
    } else {
        platformTime = currentTime;
    }

    int64_t fundingTime = srcFrate.fundingTime / oneTT;

    auto it = mFrate.find(instrumentKey);
    if (it != mFrate.end()) {
        if (it->second.IsEmpty() || (fundingTime - it->second.GetEndValue().fundingTime) > 0) {
            stra::QuantFrate frate;
            frate.timestamp = currentTime;
            frate.exchangeTime = exchangeTime;
            frate.arriveTime = arriveTime;
            frate.platformTime = platformTime;
            frate.exchangeType = stra::ExchangeTypeStr2Enum[exchangeId];
            strncpy(frate.instrument, instrument.c_str(), stra::INST_ID_LEN);
            frate.instType = stra::InstTypeStr2Enum[instrumentType];
            frate.fundingTime = fundingTime;
            frate.fundingRate = srcFrate.fundingRate;
            frate.nextFundingRate = srcFrate.nextFundingRate;
            it->second.Add(frate);
        } else {
            for (int i = it->second.GetStartIndex(); i <= it->second.GetEndIndex(); ++i) {
                stra::QuantFrate& frate = it->second[i];
                if (it->second.GetEndValue().fundingTime == fundingTime) {
                    frate.timestamp = currentTime;
                    frate.arriveTime = arriveTime;
                    frate.platformTime = platformTime;
                    frate.fundingRate = srcFrate.fundingRate;
                    frate.nextFundingRate = srcFrate.nextFundingRate;
                }
            }
        }
    }
}

void DataManager::OnMarketTrade(string channel, const stra::MdTrade& srcTrade, int64_t eventTime) {
    vector<string> v;
    splitString(channel, v, ".");
    if (v.size() <= 4) {
        return;
    }

    int64_t currentTime = GetCurrentTime();
    string exchangeId = v[0];
    string instrumentType = v[1];
    string instrument = v[3];
    string marketType = v[4];
    string instrumentKey = exchangeId + "|" + instrument + "|" + instrumentType;

    int64_t oneTT = 1000000;
    int64_t exchangeTime = 0;
    int64_t arriveTime = 0;
    int64_t platformTime = 0;
    
    if (srcTrade.ts > 0) {
        exchangeTime = srcTrade.ts / oneTT;
    } else {
        exchangeTime = currentTime;
    }

    if (srcTrade.tsNet > 0) {
        arriveTime = srcTrade.tsNet / oneTT;
    } else {
        arriveTime = currentTime;
    }

    if (srcTrade.tsParse > 0) {
        platformTime = srcTrade.tsParse / oneTT;
    } else {
        platformTime = currentTime;
    }

    auto it = mTrade.find(instrumentKey);
    if (it != mTrade.end()) {
        stra::QuantMarketTrade trade;
        trade.timestamp = currentTime;
        trade.exchangeTime = exchangeTime;
        trade.arriveTime = arriveTime;
        trade.platformTime = platformTime;
        trade.eventTime = eventTime;
        trade.exchangeType = stra::ExchangeTypeStr2Enum[exchangeId];
        strncpy(trade.instrument, instrument.c_str(), stra::INST_ID_LEN);
        trade.instType = stra::InstTypeStr2Enum[instrumentType];
        trade.direction = srcTrade.side == "buy" ? stra::Direction_LONG : stra::Direction_SHORT;
        trade.tradePrice = srcTrade.px;
        trade.tradeVolume = srcTrade.size;
        it->second.Add(trade);
    }

    auto iu = mKline.find(instrumentKey);
    if (iu != mKline.end()) {
        for (auto iw = iu->second.begin(); iw != iu->second.end(); ++iw) {
            int period = iw->first;
            int64_t preDatetime = 0;
            if (!iw->second.IsEmpty()) {
                preDatetime = iw->second.GetEndValue().updateTime;
            } else {
                preDatetime = currentTime;
            }

            int dfp = DiffPeriod(preDatetime, currentTime, period);
            int64_t timestamp = currentTime / period * period;
            if (dfp == 0) {
                if (iw->second.IsEmpty()) {
                    stra::QuantKline kline;
                    kline.timestamp = timestamp;
                    kline.exchangeType = stra::ExchangeTypeStr2Enum[exchangeId];
                    strncpy(kline.instrument, instrument.c_str(), stra::INST_ID_LEN);
                    kline.instType = stra::InstTypeStr2Enum[instrumentType];
                    kline.open = srcTrade.px;
                    kline.high = srcTrade.px;
                    kline.low = srcTrade.px;
                    kline.low = srcTrade.px;
                    kline.close = srcTrade.px;
                    kline.volume = srcTrade.size;
                    kline.amount = srcTrade.px * srcTrade.size;
                    kline.count = 1;
                    kline.buyVolume = srcTrade.side == "buy" ? srcTrade.size : 0;
                    kline.buyAmount = srcTrade.side == "buy" ? srcTrade.px * srcTrade.size : 0;
                    kline.buyCount = srcTrade.side == "buy" ? 1 : 0;
                    kline.sellVolume = srcTrade.side == "sell" ? srcTrade.size : 0;
                    kline.sellAmount = srcTrade.side == "sell" ? srcTrade.px * srcTrade.size : 0;
                    kline.sellCount = srcTrade.side == "sell" ? 1 : 0;
                    kline.updateTime = currentTime;
                    kline.process = min(1.0, (currentTime - timestamp) / (double)period);
                    iw->second.Add(kline);
                } else {
                    int endIndex = iw->second.GetEndIndex();
                    stra::QuantKline& endKline = iw->second[endIndex];
                    endKline.open = endKline.volume == 0 ? srcTrade.px : endKline.open;
                    endKline.close = srcTrade.px;
                    endKline.high = max(endKline.high, srcTrade.px);
                    endKline.low = min(endKline.low, srcTrade.px);
                    endKline.volume += srcTrade.size;
                    endKline.amount += srcTrade.px * srcTrade.size;
                    endKline.count += 1;
                    endKline.buyVolume += srcTrade.side == "buy" ? srcTrade.size : 0;
                    endKline.buyAmount += srcTrade.side == "buy" ? srcTrade.px * srcTrade.size : 0;
                    endKline.buyCount += srcTrade.side == "buy" ? 1 : 0;
                    endKline.sellVolume += srcTrade.side == "sell" ? srcTrade.size : 0;
                    endKline.sellAmount += srcTrade.side == "sell" ? srcTrade.px * srcTrade.size : 0;
                    endKline.sellCount += srcTrade.side == "sell" ? 1 : 0;
                    endKline.updateTime = currentTime;
                    endKline.process = min(1.0, (currentTime - timestamp) / (double)period);
                }
            } else {
                // int endIndex = iw->second.GetEndIndex();  ? 为空时
                // stra::QuantKline& endKline = iw->second[endIndex];
                // endKline.process = 1;
                for (int i = 0; i < dfp - 1; ++i) {
                    int endIndex = iw->second.GetEndIndex();
                    stra::QuantKline& endKline = iw->second[endIndex];
                    int64_t timestamp = (endKline.timestamp / period + 1) * period;
                    stra::QuantKline kline;
                    kline.timestamp = timestamp;
                    kline.exchangeType = endKline.exchangeType;
                    strncpy(kline.instrument, endKline.instrument, stra::INST_ID_LEN);
                    kline.instType = endKline.instType;
                    kline.open = endKline.close;
                    kline.high = endKline.close;
                    kline.low = endKline.close;
                    kline.close = endKline.close;
                    kline.volume = 0;
                    kline.amount = 0;
                    kline.count = 0;
                    kline.buyVolume = 0;
                    kline.buyAmount = 0;
                    kline.buyCount = 0;
                    kline.sellVolume = 0;
                    kline.sellAmount = 0;
                    kline.sellCount = 0;
                    kline.updateTime = timestamp;
                    kline.process = 1;
                    iw->second.Add(kline);
                }

                stra::QuantKline kline;
                kline.timestamp = timestamp;
                kline.exchangeType = stra::ExchangeTypeStr2Enum[exchangeId];
                strncpy(kline.instrument, instrument.c_str(), stra::INST_ID_LEN);
                kline.instType = stra::InstTypeStr2Enum[instrumentType];
                kline.open = srcTrade.px;
                kline.high = srcTrade.px;
                kline.low = srcTrade.px;
                kline.low = srcTrade.px;
                kline.close = srcTrade.px;
                kline.volume = srcTrade.size;
                kline.amount = srcTrade.px * srcTrade.size;
                kline.count = 1;
                kline.buyVolume = srcTrade.side == "buy" ? srcTrade.size : 0;
                kline.buyAmount = srcTrade.side == "buy" ? srcTrade.px * srcTrade.size : 0;
                kline.buyCount = srcTrade.side == "buy" ? 1 : 0;
                kline.sellVolume = srcTrade.side == "sell" ? srcTrade.size : 0;
                kline.sellAmount = srcTrade.side == "sell" ? srcTrade.px * srcTrade.size : 0;
                kline.sellCount = srcTrade.side == "sell" ? 1 : 0;
                kline.updateTime = currentTime;
                kline.process = min(1.0, (currentTime - timestamp) / (double)period);
                iw->second.Add(kline);
            }
        }
    }
}

void DataManager::OnClock() {
    int64_t currentTime = GetCurrentTime();
    for (auto it = mKline.begin(); it != mKline.end(); ++it) {
        for (auto iu = it->second.begin(); iu != it->second.end(); ++iu) {
            int period = iu->first;
            int64_t preDatetime = 0;
            if (!iu->second.IsEmpty()) {
                preDatetime = iu->second.GetEndValue().updateTime;
            } else {
                preDatetime = currentTime;
            }
            int dfp = DiffPeriod(preDatetime, currentTime, period);
            for (int i = 0; i < dfp - 1; ++i) {
                int endIndex = iu->second.GetEndIndex();
                stra::QuantKline& endKline = iu->second[endIndex];
                int64_t timestamp = (endKline.timestamp / period + 1) * period;
                stra::QuantKline kline;
                kline.timestamp = timestamp;
                kline.exchangeType = endKline.exchangeType;
                strncpy(kline.instrument, endKline.instrument, stra::INST_ID_LEN);
                kline.instType = endKline.instType;
                kline.open = endKline.close;
                kline.high = endKline.close;
                kline.low = endKline.close;
                kline.close = endKline.close;
                kline.volume = 0;
                kline.amount = 0;
                kline.count = 0;
                kline.buyVolume = 0;
                kline.buyAmount = 0;
                kline.buyCount = 0;
                kline.sellVolume = 0;
                kline.sellAmount = 0;
                kline.sellCount = 0;
                kline.updateTime = timestamp;
                kline.process = 1;
                iu->second.Add(kline);
            }
        }
    }
}

void DataManager::OnMarketDepth(const stra::QuantMarketDepth& depth, int64_t eventTime) {
    string instKey = stra::ExchangeTypeEnum2Str[depth.exchangeType] + "." + stra::InstTypeEnum2Str[depth.instType] + "." + string(depth.instrument);
    auto it = mDepth.find(string(instKey));
    if (it != mDepth.end()) {
        it->second.Add(depth);
    }
}

stra::QuantMarketDepth DataManager::GetLastDepth(string instrumentKey) {
    stra::QuantMarketDepth  depth;
    vector<string> v;
    splitString(instrumentKey, v, ".");
    if (v.size() >= 2) {
        if (v[2] == "MARGIN") { // 应修改成enum
            instrumentKey = v[0] + ".SPOT." + v[1];
        }
    }

    auto it = mDepth.find(instrumentKey);
    if (it != mDepth.end()) {
        if (!it->second.IsEmpty()) {
            depth = it->second.GetEndValue();
        }
    }

    return depth;
}

void DataManager::GetLastTrade(string instrumentKey) {

}

double DataManager::GetMidPrice(string instrumentKey) {
	double price = 0.0;
	stra::QuantMarketDepth lastDepth = GetLastDepth(instrumentKey);
	if (lastDepth.vAskPrice.size() > 0 && lastDepth.vBidPrice.size() > 0) {
		price = (lastDepth.vAskPrice[0] + lastDepth.vBidPrice[0]) / 2;
	}
	return price;
}

double DataManager::GetWeightedMidPrice(string instrumentKey) {
    return 0.0;
}

double DataManager::GetAssetPrice(string asset) {
    if (asset == "USDT") {
        return 1;   
    }
    string key = "BINANCE.SPOT." + asset + "-USDT";
	double price = GetMidPrice(key);
	if (price <= stra::MIN_FLOAT) {
		key = "BINANCE.InstType_USDT_SWAP." + asset + "-USDT";
		price = GetMidPrice(key);
	}

	if (price <= stra::MIN_FLOAT) {
		key = "BINANCE.SPOT." + asset + "-BUSD";
		price = GetMidPrice(key);
		key = "BINANCE.SPOT.BUSD-USDT";
		double priceBUSD = GetMidPrice(key);
		price = price * priceBUSD;
	}
	return price;
}

void DataManager::DeleteDepth(string instrumentKey) {
    auto iter = mDepthPara.find(instrumentKey);
    if (iter != mDepthPara.end()) {
        mDepthPara.erase(iter);
    }

    auto it = mDepth.find(instrumentKey);
    if (it != mDepth.end()) {
        mDepth.erase(it);
    }
}
