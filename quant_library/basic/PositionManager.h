#ifndef _POSITION_MANAGER_H
#define _POSITION_MANAGER_H

#include <string>
#include <unordered_map>
#include "DataStruct.h"
#include "json/nlohmann/json.hpp"
#include <fstream>
#include <iomanip>


using namespace  std;
using json = nlohmann::json;


class PositionManager {
    public:
        PositionManager();
        ~PositionManager();
        void SetBaseAsset(char* ass);
        void OnInsertOrder(const stra::QuantOrder& order);
        void OnDeleteOrder(const stra::QuantOrder& order);
        void OnOrder(const stra::QuantOrder& order);
        //void UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth);
        void CalcualteFloatPnl();
        void CalcualtePnl(string activeInstrumentKey = "");
        stra::QuantAccount& GetAccount();
        unordered_map<string, double>& GetPnl();
        double GetTotalPnl();

    private:
        char baseAsset[stra::ASSET_LEN];
        stra::QuantAccount account;
        unordered_map<string, double> mPnl;
        double totalPnl;
};

#endif
