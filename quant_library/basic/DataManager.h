#ifndef _DATA_MANAGER_H
#define _DATA_MANAGER_H

#include "DataStruct.h"
#include "DataArray.h"
#include <unordered_map>
#include <vector>
#include <string>

using namespace std;


class DataManager {
    public:
        static DataManager& Instance();
        ~DataManager();
        void AddDKlinePara(string instrumentKey, int period, int length);
        void AddKlinePara(string instrumentKey, int period, int length);
        void AddDepthPara(string instrumentKey, int length);
        void AddTradePara(string instrumentKey, int length);
        void AddFratePara(string instrumentKey, int length);
        void InitMarketKline();
        void OnMarketKline(string channel, const stra::MdKline& srcKline);
        void OnMarketDepth(string channel, const stra::MdDepth& srcDepth, int64_t eventTime);
        void OnFundingRate(string channel, const stra::MdFrate& srcFrate);
        void OnMarketTrade(string channel, const stra::MdTrade& srcTrade, int64_t eventTime);
        void OnClock();
        void OnMarketDepth(const stra::QuantMarketDepth& depth, int64_t eventTime);
        stra::QuantMarketDepth GetLastDepth(string instrumentKey);
        void GetLastTrade(string instrumentKey);
        double GetMidPrice(string instrumentKey);
        double GetWeightedMidPrice(string instrumentKey);
        double GetAssetPrice(string asset);
        void DeleteDepth(string instrumentKey);

    private:
        DataManager();
        // para
        unordered_map<string, unordered_map<int, int>> mDKlinePara;
        unordered_map<string, unordered_map<int, int>> mKlinePara;
        unordered_map<string, int> mDepthPara;
        unordered_map<string, int> mTradePara;
        unordered_map<string, int> mFratePara;

        // data
        unordered_map<string, DataArray<stra::QuantFrate>> mFrate;
        unordered_map<string, unordered_map<int, DataArray<stra::QuantKline>>> mKline;
        unordered_map<string, unordered_map<int, DataArray<stra::QuantKline>>> mDKline;
        unordered_map<string, DataArray<stra::QuantMarketDepth>> mDepth;
        unordered_map<string, DataArray<stra::QuantMarketTrade>> mTrade;
};

#endif