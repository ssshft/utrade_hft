#include "SpreadManager.h"
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

void SpreadManager::AddSpreadPara(const std::string& pairInstrumentKey) {
    auto iter = mSpread.find(pairInstrumentKey);
    if (iter != mSpread.end()) {
        return;
    }

    auto pdata = std::make_unique<dbp::DbpData>();
    dbp::DbpData* p = pdata.get();

    mSpread.emplace(pairInstrumentKey, std::move(p));

    std::vector<std::string> v;
    splitString(pairInstrumentKey, v, "|");
    if (v.size() >= 2) {
        mInstEntry[v[0]].push_back({p, LegType::ACTIVE});
        mInstEntry[v[1]].push_back({p, LegType::PASSIVE});
    }

    LOG_INFO("Add new spread --- instrumentKey: %s", pairInstrumentKey.c_str());
}

void SpreadManager::DeleteSpread(const std::string& pairInstrumentKey) {
    auto iter = mSpread.find(pairInstrumentKey);
    if (iter == mSpread.end()) {
        return;
    }

    dbp::DbpData* p = iter->second.get();

    std::vector<std::string> v;
    splitString(pairInstrumentKey, v, "|");
    if (v.size() >= 2) {
        auto itActive = mInstEntry.find(v[0]);
        if (itActive != mInstEntry.end()) {
            auto& ref = itActive->second;
            ref.erase(std::remove_if(ref.begin(), ref.end(), [p](const SpreadEntry& entry) { return entry.pdata == p}));

            if (ref.empty()) {
                mInstEntry.erase(itActive);
            }
        }

        auto itPassive = mInstEntry.find(v[1]);
        if (itPassive != mInstEntry.end()) {
            auto& ref = itPassive->second;
            ref.erase(std::remove_if(ref.begin(), ref.end(), [p](const SpreadEntry& entry) { return entry.pdata == p}));

            if (ref.empty()) {
                mInstEntry.erase(itPassive);
            }
        }
    }

    mSpread.erase(iter);
}

void SpreadManager::OnMarketSpread(dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    auto iter = mSpread.find(topic->__name);
    if (iter != mSpread.end()) {
        dbp::DbpData* p = iter->second.get();
        std::memcpy(p, pdata, sizeof(dbp::DbpData));
    }
}

dbp::DbpData* SpreadManager::GetSpread(const std::string& pairInstrumentKey){
    auto iter = mSpread.find(pairInstrumentKey);
    if (iter != mSpread.end()) {
        return iter->second.get();
    }

    return nullptr;
}

bool SpreadManager::IsPairInstrumentKeyExist(const std::string& pairInstrumentKey) {
    auto iter = mSpread.find(pairInstrumentKey);
    if (iter != mSpread.end()) {
        return true;
    }

    return false;
}

std::vector<SpreadEntry> SpreadManager::GetSpreadEntry(const std::string& instKey) {
    auto iter = mInstEntry.find(instKey);
    if (iter != mInstEntry.end()) {
        return iter->second;
    }

    return {};
}

Bbo SpreadManager::GetBbo(const std::string& instKey) {
    Bbo bbo;
    auto iter = mInstEntry.find(instKey);
    if (iter != mInstEntry.end()) {
        const auto& ref = iter->second.front();
        if (ref.legType == LegType::ACTIVE) {
            bbo.bidPrice = ref.pdata->activeBidPrice[0];
            bbo.bidVol = ref.pdata->activeBidVolume[0];
            bbo.askPrice = ref.pdata->activeAskPrice[0];
            bbo.askVol = ref.pdata->activeAskVolume[0];
        }
        else {
            bbo.bidPrice = ref.pdata->passiveBidPrice[0];
            bbo.bidVol = ref.pdata->passiveBidVolume[0];
            bbo.askPrice = ref.pdata->passiveAskPrice[0];
            bbo.askVol = ref.pdata->passiveAskVolume[0];   
        }
    }

    return bbo;
}