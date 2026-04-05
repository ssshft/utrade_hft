#ifndef LIMIT_MANAGER_H
#define LIMIT_MANAGER_H

#include "LimitBoard.h"

class LimitManager {
public:    
    static LimitManager& Instance();
    void Init();
    void OnOrder(const stra::QuantOrder& order);
    bool PassLimit(int accountId);
    bool PassCancelLimit(int accountId);
    LimitCalcBoard& GetAccountBoard(int accountId);
    ~LimitManager();

private:
    LimitManager();
    unordered_map<int, LimitCalcBoard> mAccountLimitBoard;
    bool check;
};

#endif
