#ifndef LIMIT_MANAGER_H
#define LIMIT_MANAGER_H

#include "LimitBoard.h"

class LimitManager {
public:    
    static LimitManager& Instance();
    void Init();

    // 记账：三本账分开，别混。
    // 之前只有一个 OnOrder，CreateOrder / CancelOrder / QueryOrder 全都往同一个
    // 链表里记 ⇒ 查单和撤单会吃掉报单配额（详见 LimitBoard.h 的注释）。
    void OnCreateOrder(const stra::QuantOrder& order);
    void OnCancelOrder(const stra::QuantOrder& order);
    void OnQueryOrder(const stra::QuantOrder& order);

    // 查配额：同样分开。
    bool PassLimit(int accountId);        // 报单
    bool PassCancelLimit(int accountId);  // 撤单
    bool PassQueryLimit(int accountId);   // 查单

    LimitCalcBoard& GetAccountBoard(int accountId);
    ~LimitManager();

private:
    LimitManager();
    unordered_map<int, LimitCalcBoard> mAccountLimitBoard;
    bool check;
};

#endif
