#ifndef _SPREAD_MANAGER_H
#define _SPREAD_MANAGER_H

#include "DataStruct.h"
#include "DataArray.h"
#include <unordered_map>
#include <vector>
#include <string>
#include "dbp/include.h"

using namespace std;

enum class LegType : uint8_t {
    ACTIVE,
    PASSIVE
};

struct Bbo {
    double bidPrice{0.0};
    double bidVol{0.0};
    double askPrice{0.0};
    double askVol{0.0};
};

struct SpreadEntry {
    dbp::DbpData* pdata;
    LegType legType = LegType::ACTIVE;
};

class SpreadManager {
    public:
        static SpreadManager& Instance();
        ~SpreadManager();

        void AddSpreadPara(const std::string& pairInstrumentKey);
        void OnMarketSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata);
        void DeleteSpread(const std::string& pairInstrumentKey);
        dbp::DbpData* GetSpread(const std::string& pairInstrumentKey);
        bool IsPairInstrumentKeyExist(const std::string& pairInstrumentKey);
        std::vector<SpreadEntry> GetSpreadEntry(const std::string& instKey);
        Bbo GetBbo(const std::string& instKey);

    private:
        SpreadManager();

        std::unordered_map<std::string, std::unique_ptr<dbp::DbpData>> mSpread;
        std::unordered_map<std::string, std::vector<SpreadEntry>> mInstEntry;
};

#endif