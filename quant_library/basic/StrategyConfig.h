#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "Utility.h"
#include "DataStruct.h"

using namespace std;


class StrategyConfig {
public:
    static StrategyConfig& GetInstance();
    ~StrategyConfig();
    void LoadConfig();
    void LoadStrategy();
    unordered_map<int, AccountInfo>& GetAccountInfo();
    string GetStrategyIdByAccountId(int accountId);
    string GetMdAddr();
    int GetMdPort();
    string GetMdPassword();
    string GetLarkUrl();
    double GetMinOrderAmount();

    int GetCurSpreadDelay();
    int GetCurSpreadDepthDelay();
    int GetCurSpreadTradesDelay();
    int GetTradesThreshold();
    int GetOnTimerTrade();
    
private:
    StrategyConfig();
    unordered_map<int, AccountInfo> mAccountInfo;

    string mdAddr;
    int mdPort;
    string mdPassword;

    int curSpreadDelay;
    int curSpreadDepthDelay;
    int curSpreadTradesDelay;

    string larkUrl;

    double minOrderAmount;

    int tradesThreshold;
    int onTimerTrade;
};
