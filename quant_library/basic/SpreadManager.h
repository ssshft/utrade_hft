#ifndef _SPREAD_MANAGER_H
#define _SPREAD_MANAGER_H

#include "DataStruct.h"
#include "DataArray.h"
#include <unordered_map>
#include <vector>
#include <string>

using namespace std;


class SpreadManager {
    public:
        static SpreadManager& Instance();
        ~SpreadManager();
        void AddSpreadPara(string pairInstrumentKey, int length);
        void OnMarketSpread(const stra::QuantSpread& quantSpread, int64_t eventTime);
        void DeleteQuantSpread(string pairInstrumentKey);
        stra::QuantSpread GetLastSpread(string pairInstrumentKey);
        bool IsPairInstrumentKeyExist(string pairInstrumentKey);

    private:
        SpreadManager();
        // para
        unordered_map<string, int> mSpreadPara; // vector[0]=period, vector[1]=length
        unordered_map<string, DataArray<stra::QuantSpread>> mQuantSpread;
};

#endif