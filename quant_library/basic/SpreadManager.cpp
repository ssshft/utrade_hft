#include "SpreadManager.h"
#include "DataManager.h"
#include "Utility.h"


SpreadManager& SpreadManager::Instance() {
// 单例模式
    static SpreadManager spreadManager;
    return spreadManager;
}

SpreadManager::SpreadManager() {

}

SpreadManager::~SpreadManager() {
    mSpreadPara.clear();
    mQuantSpread.clear();
}

void SpreadManager::AddSpreadPara(string pairInstrumentKey, int length) {
    auto iter = mSpreadPara.find(pairInstrumentKey);
    if (iter != mSpreadPara.end()) {
        return;
    }

    LOG_INFO("Add new spread --- instrumentKey: %s", pairInstrumentKey.c_str());
    mSpreadPara[pairInstrumentKey] = length; 
    mQuantSpread[pairInstrumentKey] = DataArray<stra::QuantSpread>(length);

    vector<string> v;
    splitString(pairInstrumentKey, v, "|");
    if (v.size() >= 2) {
        DataManager::Instance().AddDepthPara(v[0], length);
        DataManager::Instance().AddDepthPara(v[1], length);
    }
}

void SpreadManager::DeleteQuantSpread(string pairInstrumentKey){
    auto it = mQuantSpread.find(pairInstrumentKey);
    if (it != mQuantSpread.end()){
        mQuantSpread.erase(it);
    }

    auto iter = mSpreadPara.find(pairInstrumentKey);
    if (iter != mSpreadPara.end()) {
        mSpreadPara.erase(iter);
    }
}

void SpreadManager::OnMarketSpread(const stra::QuantSpread& quantSpread, int64_t eventTime) {
    auto it = mQuantSpread.find(quantSpread.pairInstrumentKey);
    if (it != mQuantSpread.end()){
        it->second.Add(quantSpread);
    }

    stra::QuantMarketDepth activeDepth;
    vector<string> vActive;
    splitString(quantSpread.activeInstumentKey, vActive, ".");
    activeDepth.timestamp = quantSpread.activeDepthTs;
    activeDepth.exchangeType = stra::ExchangeTypeStr2Enum[vActive[0]];
    activeDepth.instType = stra::InstTypeStr2Enum[vActive[1]];
    strncpy(activeDepth.instrument, vActive[2].c_str(), stra::INST_ID_LEN);
    activeDepth.vAskPrice[0] = quantSpread.activeAskPrice1;
    activeDepth.vAskPrice[1] = quantSpread.activeAskPrice2;
    activeDepth.vAskPrice[2] = quantSpread.activeAskPrice3;
    activeDepth.vAskPrice[3] = quantSpread.activeAskPrice4;
    activeDepth.vAskPrice[4] = quantSpread.activeAskPrice5;

    activeDepth.vAskVolume[0] = quantSpread.activeAskVolume1;
    activeDepth.vAskVolume[1] = quantSpread.activeAskVolume2;
    activeDepth.vAskVolume[2] = quantSpread.activeAskVolume3;
    activeDepth.vAskVolume[3] = quantSpread.activeAskVolume4;
    activeDepth.vAskVolume[4] = quantSpread.activeAskVolume5;

    activeDepth.vBidPrice[0] = quantSpread.activeBidPrice1;
    activeDepth.vBidPrice[1] = quantSpread.activeBidPrice2;
    activeDepth.vBidPrice[2] = quantSpread.activeBidPrice3;
    activeDepth.vBidPrice[3] = quantSpread.activeBidPrice4;
    activeDepth.vBidPrice[4] = quantSpread.activeBidPrice5;

    activeDepth.vBidVolume[0] = quantSpread.activeBidVolume1;
    activeDepth.vBidVolume[1] = quantSpread.activeBidVolume2;
    activeDepth.vBidVolume[2] = quantSpread.activeBidVolume3;
    activeDepth.vBidVolume[3] = quantSpread.activeBidVolume4;
    activeDepth.vBidVolume[4] = quantSpread.activeBidVolume5;


    stra::QuantMarketDepth passiveDepth;
    vector<string> vPassive;
    splitString(quantSpread.passiveInstrumentKey, vPassive, ".");
    passiveDepth.timestamp = quantSpread.activeDepthTs;
    passiveDepth.exchangeType = stra::ExchangeTypeStr2Enum[vPassive[0]];
    passiveDepth.instType = stra::InstTypeStr2Enum[vPassive[1]];
    strncpy(passiveDepth.instrument, vPassive[2].c_str(), stra::INST_ID_LEN);
    passiveDepth.vAskPrice[0] = quantSpread.passiveAskPrice1;
    passiveDepth.vAskPrice[1] = quantSpread.passiveAskPrice2;
    passiveDepth.vAskPrice[2] = quantSpread.passiveAskPrice3;
    passiveDepth.vAskPrice[3] = quantSpread.passiveAskPrice4;
    passiveDepth.vAskPrice[4] = quantSpread.passiveAskPrice5;

    passiveDepth.vAskVolume[0] = quantSpread.passiveAskVolume1;
    passiveDepth.vAskVolume[1] = quantSpread.passiveAskVolume2;
    passiveDepth.vAskVolume[2] = quantSpread.passiveAskVolume3;
    passiveDepth.vAskVolume[3] = quantSpread.passiveAskVolume4;
    passiveDepth.vAskVolume[4] = quantSpread.passiveAskVolume5;

    passiveDepth.vBidPrice[0] = quantSpread.passiveBidPrice1;
    passiveDepth.vBidPrice[1] = quantSpread.passiveBidPrice2;
    passiveDepth.vBidPrice[2] = quantSpread.passiveBidPrice3;
    passiveDepth.vBidPrice[3] = quantSpread.passiveBidPrice4;
    passiveDepth.vBidPrice[4] = quantSpread.passiveBidPrice5;

    passiveDepth.vBidVolume[0] = quantSpread.passiveBidVolume1;
    passiveDepth.vBidVolume[1] = quantSpread.passiveBidVolume2;
    passiveDepth.vBidVolume[2] = quantSpread.passiveBidVolume3;
    passiveDepth.vBidVolume[3] = quantSpread.passiveBidVolume4;
    passiveDepth.vBidVolume[4] = quantSpread.passiveBidVolume5;

    DataManager::Instance().OnMarketDepth(activeDepth, eventTime);
    DataManager::Instance().OnMarketDepth(passiveDepth, eventTime);

}

stra::QuantSpread SpreadManager::GetLastSpread(string pairInstrumentKey){
    stra::QuantSpread quantSpread;
    auto it = mQuantSpread.find(pairInstrumentKey);
    if (it != mQuantSpread.end()){
        quantSpread = it->second.GetEndValue();
    }
    return quantSpread;
}

bool SpreadManager::IsPairInstrumentKeyExist(string pairInstrumentKey) {
    bool exist = false;
    auto iter = mQuantSpread.find(pairInstrumentKey);
    if (iter != mQuantSpread.end()) {
        exist = true;
    }
    return exist;
}