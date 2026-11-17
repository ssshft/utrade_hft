#include "LimitBoard.h"
#include "log_engine.h"


void LimitCalcBoard::SetLimitUnit(const LimitUnit& unit) {
    limitval = unit;
}

void LimitCalcBoard::PruneWindow(std::list<int64_t>& times, int64_t nowMs) {
    while (!times.empty()) {
        if (nowMs > times.front() + 1000) {
            times.pop_front();
        }
        else {
            break;
        }
    }
}

bool LimitCalcBoard::CheckWindow(std::list<int64_t>& times, int limit, const char* what) {
    auto curtime = crypto::getCurrentTimeMilli();
    PruneWindow(times, curtime);

    if ((int)times.size() >= limit) {
        LOG_INFO("accountId: {} {}:{} limit:{}", limitval.accountId, what, times.size(), limit);
        return false;
    }
    return true;
}

bool LimitCalcBoard::CheckPersec() {
    return CheckWindow(orderTimes, limitval.maxPersec, "ordertimes.size");
}

bool LimitCalcBoard::CheckCancelPersec() {
    return CheckWindow(cancelTimes, limitval.maxCancelPersec, "canceltimes.size");
}

bool LimitCalcBoard::CheckQueryPersec() {
    return CheckWindow(queryTimes, limitval.maxQueryPersec, "querytimes.size");
}

bool LimitCalcBoard::CheckNum() {
    bool pass = true;
    if (orderNum >= limitval.orderNum){
        pass = false;
        //LOG_INFO("accountId: %d orderNum:%d limitval.orderNum:%d", limitval.accountId, orderNum, limitval.orderNum);
    }

    return pass;
}

void LimitCalcBoard::OnCreate() {
    ++orderNum;
    auto now = crypto::getCurrentTimeMilli();
    orderTimes.push_back(now);
    PruneWindow(orderTimes, now);
}

void LimitCalcBoard::OnCancel() {
    auto now = crypto::getCurrentTimeMilli();
    cancelTimes.push_back(now);
    PruneWindow(cancelTimes, now);
}

void LimitCalcBoard::OnQuery() {
    auto now = crypto::getCurrentTimeMilli();
    queryTimes.push_back(now);
    PruneWindow(queryTimes, now);
}

int LimitCalcBoard::GetOrderNum() {
    return orderNum;
}

int LimitCalcBoard::GetPerSec() {
    return (int)orderTimes.size();
}
