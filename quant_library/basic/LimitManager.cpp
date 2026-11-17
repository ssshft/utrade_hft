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
        // maxQueryPersec 没有配置入口，保持 LimitUnit 的默认值（30/秒）
    	mAccountLimitBoard[accountId].SetLimitUnit(unit);
    }
}

LimitCalcBoard& LimitManager::GetAccountBoard(int accountId) {
    return mAccountLimitBoard[accountId];
}

void LimitManager::OnCreateOrder(const stra::QuantOrder& order) {
    GetAccountBoard(order.strategyAccountId).OnCreate();
}

void LimitManager::OnCancelOrder(const stra::QuantOrder& order) {
    GetAccountBoard(order.strategyAccountId).OnCancel();
}

void LimitManager::OnQueryOrder(const stra::QuantOrder& order) {
    GetAccountBoard(order.strategyAccountId).OnQuery();
}

bool LimitManager::PassLimit(int accountId) {
    bool pass = true;
    if (check) {
        pass = GetAccountBoard(accountId).CheckPersec();
    }
    return pass;
}

bool LimitManager::PassCancelLimit(int accountId) {
    bool pass = true;
    if (check) {
        pass = GetAccountBoard(accountId).CheckCancelPersec();
    }
    return pass;
}

bool LimitManager::PassQueryLimit(int accountId) {
    bool pass = true;
    if (check) {
        pass = GetAccountBoard(accountId).CheckQueryPersec();
    }
    return pass;
}
