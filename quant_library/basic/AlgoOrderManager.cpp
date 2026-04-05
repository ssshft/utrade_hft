#include "AlgoOrderManager.h"


AlgoOrderManager::AlgoOrderManager() {

}

AlgoOrderManager::~AlgoOrderManager() {
    for (auto iter = mAlgoOrder.begin(); iter != mAlgoOrder.end(); ++iter) {
        if (iter->second) {
            delete iter->second;
            iter->second = nullptr;
        }
    }
    mAlgoOrder.clear();
}

void AlgoOrderManager::InsertAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder) {
    if (pOrder) {
        mAlgoOrder[pOrder->algoOrderId] = pOrder;
    }
}

void AlgoOrderManager::UpdateAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder) {
    if (pOrder) {
        mAlgoOrder[pOrder->algoOrderId] = pOrder;
    }
}
    
void AlgoOrderManager::DeleteAlgoOrderByAlgoOrder(BaseAlgoOrder* pOrder) {
    if (pOrder) {
        auto it = mAlgoOrder.find(pOrder->algoOrderId);
        if (it != mAlgoOrder.end()) {
            delete it->second;
            it->second = nullptr;
            mAlgoOrder.erase(it);
        }
    }
}

BaseAlgoOrder* AlgoOrderManager::SeletAlgoOrderByAlgoOrderId(int64_t algoOrderId) {
    BaseAlgoOrder* pOrder = nullptr;
    auto iter = mAlgoOrder.find(algoOrderId);
    if (iter != mAlgoOrder.end()) {
        pOrder = iter->second;
    }
    return pOrder;
}

unordered_map<int64_t, BaseAlgoOrder*>& AlgoOrderManager::GetAllAlgoOrders() {
    return mAlgoOrder;
}