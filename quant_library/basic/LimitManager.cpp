#include "LimitManager.h"
#include "StrategyConfig.h"


LimitManager::LimitManager() {
}

LimitManager::~LimitManager() {
    mAccountLimitBoard.clear();
}

LimitManager& LimitManager::Instance() {
    static LimitManager limitManager;
    return limitManager;
}

void LimitManager::Init() {
    check = true;

    auto& mAccountInfo = StrategyConfig::GetInstance().GetAccountInfo();

    for (auto iter = mAccountNameAccountId.begin(); iter != mAccountNameAccountId.end(); ++iter) {
	    int accountId = iter->second;
        LimitUnit unit;
        unit.accountId = accountId;

        auto it = mAccountInfo.find(accountId);
        if (it != mAccountInfo.end()) {
            unit.maxPersec = it->second.maxPersec;
            unit.maxCancelPersec = it->second.maxCancelPersec;
            unit.orderNum = it->second.orderNum;
        }
        else {
            unit.maxPersec = 20;
            unit.maxCancelPersec = 30;
            unit.orderNum = 5000;  
        }
    	mAccountLimitBoard[accountId].SetLimitUnit(unit);
    }
}

LimitCalcBoard& LimitManager::GetAccountBoard(int accountId) {
    return mAccountLimitBoard[accountId];
}

void LimitManager::OnOrder(const stra::QuantOrder& order) {
    auto& accountBoard = GetAccountBoard(order.strategyAccountId);
    accountBoard.OnOrder(order);
}

bool LimitManager::PassLimit(int accountId) {
    bool pass = true;
    if (check) {
        auto& accountBoard = GetAccountBoard(accountId);
        bool flagPersec = accountBoard.CheckPersec();
        // bool flagNum = accountBoard.CheckNum();
        // flagNum = true;  // 去掉报单总数量的限制
        pass = flagPersec;
    }
    return pass;
}

bool LimitManager::PassCancelLimit(int accountId) {
    bool pass = true;
    if (check) {
        auto& accountBoard = GetAccountBoard(accountId);
        bool flagPersec = accountBoard.CheckCancelPersec();
        pass = flagPersec;
    }
    return pass;
}
