#ifndef LIMIT_BOARD_H
#define LIMIT_BOARD_H

#include "DataStruct.h"

/*for limit units */
struct LimitUnit{
    int accountId{0};
    int maxPersec{20};
    int maxCancelPersec{30};
    int orderNum{0};
};

class LimitCalcBoard
{
public:
    LimitCalcBoard() {}
    ~LimitCalcBoard() {}
    void SetLimitUnit(const LimitUnit& unit);
    bool CheckPersec();
    bool CheckNum();
    bool CheckCancelPersec();
    void OnOrder(const stra::QuantOrder& order);
    LimitUnit& GetLimitUnit() {return limitval;}
    int GetOrderNum();
    int GetPerSec();

private:

    int orderNum{0};
    std::list<int64_t> ordertimes;
    LimitUnit limitval;
};

#endif
