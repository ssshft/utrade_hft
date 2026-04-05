#ifndef _POSITION_MANAGER_H
#define _POSITION_MANAGER_H

#include <string>
#include <unordered_map>
#include "DataStruct.h"
#include "DataManager.h"
#include "json/nlohmann/json.hpp"
#include <fstream>
#include <iomanip>


using namespace  std;
using json = nlohmann::json;


class PositionManager {
    public:
        PositionManager();
        ~PositionManager();
        void RecoveryFromFile(string assetFilePath, string positionFilePath);
        void SetBaseAsset(char* ass);
        void OnInsertTransfer(const stra::QuantTransfer& transfer);
        void UpdateTransferOnTransfer(const stra::QuantTransfer& transfer);
        void OnInsertOrder(const stra::QuantOrder& order);
        void OnDeleteOrder(const stra::QuantOrder& order);
        void OnOrder(const stra::QuantOrder& order);
        void UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth);
        void CalcualteFloatPnl();
        void CalcualtePnl(string activeInstrumentKey = "");
        stra::QuantAccount& GetAccount();
        unordered_map<string, double>& GetPnl();
        double GetTotalPnl();
        void LoadFromFile(string filePath);
        void SaveToFile(string filePath);

    private:
        char baseAsset[stra::ASSET_LEN];
        stra::QuantAccount account;
        unordered_map<string, double> mPnl;
        double totalPnl;
};

#endif
