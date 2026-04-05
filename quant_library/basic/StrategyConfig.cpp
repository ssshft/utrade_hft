#include "StrategyConfig.h"

StrategyConfig::StrategyConfig() {
}

StrategyConfig::~StrategyConfig() {
}

StrategyConfig& StrategyConfig::GetInstance() {
    static StrategyConfig strategyConfig;
    return strategyConfig;
}

void StrategyConfig::LoadConfig() {
    LoadStrategy();
}

void StrategyConfig::LoadStrategy() {
    boost::property_tree::ptree properties;
	boost::property_tree::ini_parser::read_ini("strategy.ini", properties);
    
    auto itemSummary = properties.get_child("SUMMARY");
    int accountCount = itemSummary.get<int>("accountcount");

    minOrderAmount = itemSummary.get<double>("minorderamount", 100);

    auto itemMd = properties.get_child("MD");
    mdAddr = itemMd.get<string>("addr");
    mdPort = itemMd.get<int>("port");
    mdPassword = itemMd.get<string>("password");

    curSpreadDelay = itemMd.get<int>("curspreaddelay", 1);
    curSpreadDepthDelay = itemMd.get<int>("curspreaddepthdelay", 1);
    curSpreadTradesDelay = itemMd.get<int>("curspreadtradesdelay", 1);

    tradesThreshold = itemMd.get<int>("tradesshold", 1);

    auto itemLark = properties.get_child("Lark");
    larkUrl = itemLark.get<string>("larkurl");

    for (int i = 1; i <= accountCount; ++i) {
    	AccountInfo accountInfo;
	    string tag = "ACCOUNT" + to_string(i);
        auto itemAccount = properties.get_child(tag);
        accountInfo.accountName = itemAccount.get<string>("accountname");
        accountInfo.accountId = itemAccount.get<int>("accountid");
        accountInfo.accountType = stra::AccountTypeStr2Enum[itemAccount.get<string>("accounttype")];
        accountInfo.exchangeType = stra::ExchangeTypeStr2Enum[itemAccount.get<string>("exchangetype")];

        string instType = itemAccount.get<string>("insttype");
        vector<string> v;
        splitString(instType, v, ",");
        for (size_t i = 0; i < v.size(); ++i) {
            accountInfo.vInstType.push_back(stra::InstTypeStr2Enum[v[i]]);
        }

        accountInfo.strategyId = itemAccount.get<string>("strategyid");
        accountInfo.openRealLeverage = itemAccount.get<double>("openrealleverage", 0.9);
        accountInfo.maxRealLeverage = itemAccount.get<double>("maxrealleverage", 0.95);
        accountInfo.passiveOpenRealLeverage = itemAccount.get<double>("passiveopenrealleverage", 1.0);
        accountInfo.passiveMaxRealLeverage = itemAccount.get<double>("passivemaxrealleverage", 1.05);
        accountInfo.openActiveMgnRatio = itemAccount.get<double>("openactivemgnratio", 1000000);
        accountInfo.openPassiveMgnRatio = itemAccount.get<double>("openpassivemgnratio", 1000000);

        accountInfo.maxPersec = itemAccount.get<int>("maxpersec", 300);
        accountInfo.maxCancelPersec = itemAccount.get<int>("maxcancelpersec", 300);
        accountInfo.orderNum = itemAccount.get<int>("ordernum", 100000);

        mAccountInfo[accountInfo.accountId] = accountInfo;
    }


    // log md
    LOG_INFO("md info --- mdAddr: %s  mdPort: %d  mdPassword: %s", mdAddr.c_str(), mdPort, mdPassword.c_str());

    // log lark
    LOG_INFO("lar info --- larkurl: %s", larkUrl.c_str());

    LOG_INFO("minOrderAmount: %f", minOrderAmount);

    // log account
    for (auto iter = mAccountInfo.begin(); iter != mAccountInfo.end(); ++iter) {
   	AccountInfo accountInfo = iter->second; 
	LOG_INFO("accountName: %s", accountInfo.accountName.c_str());
	LOG_INFO("accountId: %d", accountInfo.accountId);
	LOG_INFO("accountType: %s", stra::AccountTypeEnum2Str[accountInfo.accountType].c_str());
	LOG_INFO("exchangeType: %s", stra::ExchangeTypeEnum2Str[accountInfo.exchangeType].c_str());

    for (size_t i = 0; i < accountInfo.vInstType.size(); ++i) {
        LOG_INFO("instType: %s", stra::InstTypeEnum2Str[accountInfo.vInstType[i]].c_str());
    }

	LOG_INFO("strategyId: %s", accountInfo.strategyId.c_str());
	LOG_INFO("openRealLeverage: %f", accountInfo.openRealLeverage);
	LOG_INFO("maxRealLeverage: %f", accountInfo.maxRealLeverage);
	LOG_INFO("passiveOpenRealLeverage: %f", accountInfo.passiveOpenRealLeverage);
	LOG_INFO("passiveMaxRealLeverage: %f", accountInfo.passiveMaxRealLeverage);
	LOG_INFO("openActiveMgnRatio: %f", accountInfo.openActiveMgnRatio);
	LOG_INFO("openPassiveMgnRatio: %f", accountInfo.openPassiveMgnRatio);
	LOG_INFO("----------------------------------------------------------");
    }

    // init exchange accountId
    for (auto iter = mAccountInfo.begin(); iter != mAccountInfo.end(); ++iter) {
    	string accountName = iter->second.accountName;
        int accountId = iter->second.accountId;
        mAccountNameAccountId[accountName] = accountId;
    }
}

unordered_map<int, AccountInfo>& StrategyConfig::GetAccountInfo() {
	return mAccountInfo;
}
    
string StrategyConfig::GetStrategyIdByAccountId(int accountId) {
	string strategyId = "";
	auto iter = mAccountInfo.find(accountId);
	if (iter != mAccountInfo.end()) {
		strategyId = iter->second.strategyId;
	}

	return strategyId;
}

string StrategyConfig::GetMdAddr() {
    return mdAddr;
}

int StrategyConfig::GetMdPort() {
    return mdPort;
}

string StrategyConfig::GetMdPassword() {
    return mdPassword;
}

string StrategyConfig::GetLarkUrl() {
    return larkUrl;
}

double StrategyConfig::GetMinOrderAmount() {
    return minOrderAmount;
}

int StrategyConfig::GetCurSpreadDelay() {
    return curSpreadDelay;
}

int StrategyConfig::GetCurSpreadDepthDelay() {
    return curSpreadDepthDelay;
}

int StrategyConfig::GetCurSpreadTradesDelay() {
    return curSpreadTradesDelay;
}

int StrategyConfig::GetTradesThreshold() {
    return tradesThreshold;
}

int StrategyConfig::GetOnTimerTrade() {
    return onTimerTrade;
}
    