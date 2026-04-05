#include "LimitBoard.h"


void LimitCalcBoard::SetLimitUnit(const LimitUnit& unit) {
    limitval = unit;
}

bool LimitCalcBoard::CheckPersec() {
    bool pass = true;
    auto curtime = gettickcount();
    while(!ordertimes.empty()){
        auto begin = ordertimes.front();
        if (curtime > begin + 1000){
            ordertimes.pop_front();
        }
        else break;
    }

    if (ordertimes.size() >= limitval.maxPersec) {
        pass = false;
        LOG_INFO("accountId: %d ordertimes.size:%d limitval.maxPersec:%d", limitval.accountId, ordertimes.size(), limitval.maxPersec);
    }

    return pass;
}

bool LimitCalcBoard::CheckNum() {
    bool pass = true;
    if (orderNum >= limitval.orderNum){
        pass = false;
        //LOG_INFO("accountId: %d orderNum:%d limitval.orderNum:%d", limitval.accountId, orderNum, limitval.orderNum);
    }

    return pass;
}

bool LimitCalcBoard::CheckCancelPersec() {
    bool pass = true;
    auto curtime = gettickcount();
    while(!ordertimes.empty()){
        auto begin = ordertimes.front();
        if (curtime > begin + 1000){
            ordertimes.pop_front();
        }
        else break;
    }

    if (ordertimes.size() >= limitval.maxCancelPersec) {
        pass = false;
        LOG_INFO("accountId: %d ordertimes.size:%d limitval.maxCancelPersec:%d", limitval.accountId, ordertimes.size(), limitval.maxCancelPersec);
    }

    return pass;
}

void LimitCalcBoard::OnOrder(const stra::QuantOrder& order) {
    ++orderNum;
    ordertimes.push_back(gettickcount());
}

int LimitCalcBoard::GetOrderNum() {
    return orderNum;
}

int LimitCalcBoard::GetPerSec() {
    return ordertimes.size();
}
