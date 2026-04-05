#ifndef _ALGO_ORDER_MANAGER_H
#define _ALGO_ORDER_MANAGER_H

#include "BaseAlgoOrder.h"

class AlgoOrderManager {
public:
    AlgoOrderManager();
    ~AlgoOrderManager();
    void InsertAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder);
    void UpdateAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder);
    void DeleteAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder);
    BaseAlgoOrder* SeletAlgoOrderByAlgoOrderId(int64_t algoOrderId);
    unordered_map<int64_t, BaseAlgoOrder*>& GetAllAlgoOrders();

private:
    unordered_map<int64_t, BaseAlgoOrder*> mAlgoOrder;
};



#endif