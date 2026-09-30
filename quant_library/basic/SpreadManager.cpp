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
    mSpread.clear();
    mInstEntry.clear();
}

void SpreadManager::AddSpreadPara(const std::string& pairInstrumentKey) {
    auto iter = mSpread.find(pairInstrumentKey);
    if (iter != mSpread.end()) {
        return;
    }

    dbp::DbpData* p = nullptr;
    // ⚠️ 这里原本是
    //        auto pdata = std::make_unique<dbp::DbpData>();
    //        dbp::DbpData* p = pdata.get();
    //        mSpread.emplace(pairInstrumentKey, std::move(p));
    //    `get()` 只是取一个**非拥有**的裸指针，`pdata` 仍然是所有者；而 `emplace` 又用
    //    这个裸指针构造了**第二个** unique_ptr 塞进 map。于是函数返回时 `pdata` 析构，
    //    把 map 里那个 unique_ptr 指的对象 delete 掉 —— map 里留下一个悬垂指针。
    //    后果有三层：
    //      1. 之后每一次 OnMarketSpread 的 memcpy 都写进已释放的内存（use-after-free），
    //         而 OnMarketSpread 是每一笔价差行情都会走的路径；
    //      2. GetBbo 读的是同一块已释放内存；
    //      3. DeleteSpread / 进程退出时的 clear() 触发 double free。
    //    触发点是 AlgoContext::SubmitAlgoOrder -> AddSpreadPara，
    //    也就是**每创建一个算法单就会踩一次**。
    //    修法：把所有权真正转移给 map（release 之后本地不再持有）。
    //    用局部块而不是裸 release()，是为了 emplace 抛异常时也不会泄漏。
    {
        auto owned = std::make_unique<dbp::DbpData>();
        p = owned.get();
        mSpread.emplace(pairInstrumentKey, std::move(owned));
    }

    std::vector<std::string> v;
    splitString(pairInstrumentKey, v, "|");
    if (v.size() >= 2) {
        mInstEntry[v[0]].push_back({p, LegType::ACTIVE});
        mInstEntry[v[1]].push_back({p, LegType::PASSIVE});
    }

    LOG_INFO("Add new spread --- instrumentKey: {}", pairInstrumentKey);
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
            ref.erase(std::remove_if(ref.begin(), ref.end(), [p](const SpreadEntry& entry) { return entry.pdata == p; }));

            if (ref.empty()) {
                mInstEntry.erase(itActive);
            }
        }

        auto itPassive = mInstEntry.find(v[1]);
        if (itPassive != mInstEntry.end()) {
            auto& ref = itPassive->second;
            ref.erase(std::remove_if(ref.begin(), ref.end(), [p](const SpreadEntry& entry) { return entry.pdata == p; }));

            if (ref.empty()) {
                mInstEntry.erase(itPassive);
            }
        }
    }

    mSpread.erase(iter);
}

void SpreadManager::OnMarketSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
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