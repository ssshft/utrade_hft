#include "OrderManager.h"
#include "Utility.h"
#include "BasicInfoMgr.h"

OrderManager::OrderManager() {
}

OrderManager::~OrderManager() {
    mOrder.clear();
}

// void OrderManager::Init(ShmManager* shm) {
//     shmManager = shm;
// }

// void OrderManager::Recovery() {
//     int64_t currentTime = GetCurrentTimeUs();
//     int64_t oneDayUs = 24 * 60 * 60 * 1000 * 1000;

//     unordered_map<int64_t, stra::QuantOrder> mOrd;
//     auto shmQuantOrder = shmManager->GetShmQuantOrder();
//     auto orderHead = shmQuantOrder->beginFrame();
//     for (int i = 1; i <= shmQuantOrder->header()->frameCount; ++i) {
//         auto& quantOrder = orderHead[i];
//         mOrd[quantOrder.strategyOrderId] = quantOrder;
//     }

//     for (auto iter = mOrd.begin(); iter != mOrd.end(); ++iter) {
//         auto& quantOrder = iter->second;
//         if ((quantOrder.orderStatus == stra::OrderStatus_PENDING_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED || quantOrder.orderStatus == stra::OrderStatus_CANCELLING) && currentTime - quantOrder.updateTime < oneDayUs) {
//             InsertOrderByOrder(quantOrder);
//         }
//     }
// }

void OrderManager::InsertOrderByOrder(const stra::QuantOrder& order) {
    mOrder[order.strategyOrderId] = order;
}

// void OrderManager::UpdateOrderByOrder(const stra::QuantOrder& order) {
//     mOrder[order.strategyOrderId] = order;
// }

void OrderManager::DeleteOrderByOrder(const stra::QuantOrder& order) {
    auto it = mOrder.find(order.strategyOrderId);
    if (it != mOrder.end()) {
        mOrder.erase(it);
    }
}

unordered_map<int64_t, stra::QuantOrder>& OrderManager::GetAllOrders() {
    return mOrder;
}

stra::QuantOrder OrderManager::SelectOrderByStrategyOrderId(int64_t strategyOrderId) {
    stra::QuantOrder order;
    auto it = mOrder.find(strategyOrderId);
    if (it != mOrder.end()) {
        order = it->second;
    }
    return order;
}

void OrderManager::DeleteOrdrByStrategyOrderId(int64_t strategyOrderId) {
    auto it = mOrder.find(strategyOrderId);
    if (it != mOrder.end()) {
        mOrder.erase(it);
    }
}

stra::QuantOrder OrderManager::UpdateOrderOnOrder(const pubsub::OrderResponse& orderResponse) { //区分回测和实盘:eventTime
    stra::QuantOrder ord;
    int64_t strategyOrderId = orderResponse.clientOrderId;
    auto it = mOrder.find(strategyOrderId);
    if (it != mOrder.end()) {
        auto& order = it->second;
        ord = order.UpdateOrderOnOrder(orderResponse);
    }
    return ord;
}

stra::QuantOrder OrderManager::UpdateOrderOnQueryOrder(const pubsub::OrderResponse& orderResponse) { //区分回测和实盘:eventTime
    stra::QuantOrder ord;
    int64_t strategyOrderId = orderResponse.clientOrderId;
    auto it = mOrder.find(strategyOrderId);
    if (it != mOrder.end()) {
        auto& order = it->second;
        ord = order.UpdateOrderOnQueryOrder(orderResponse);
    }
    return ord;
}

void OrderManager::UpdateOrderOnCancel(const stra::QuantOrder& order) {  //eventTime GetCurrentTime
    int64_t nowTime = crypto::getCurrentTime();
    auto it = mOrder.find(order.strategyOrderId);
    if (it != mOrder.end()) {
        auto& ord = it->second;
        ord.orderStatus = OS_CANCEL;
        ord.updateTime = nowTime;
    }
}

void OrderManager::CalculateTotalPriceVolume() {
    totalPrice = 0.0;
    totalVolume = 0.0;
    for (auto it = mOrder.begin(); it != mOrder.end(); ++it) {
        auto& order = it->second;
        md::InstrumentInfo info;
        smc->get_instrument_info(order.exchangeType, order.instType, order.instrument, info);
        if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES) {
            if (info.calcType == 0) {
                totalPrice = (totalPrice * totalVolume + order.price * order.volume) / (totalVolume + order.volume);
                totalVolume += order.volume;
            } else if (info.calcType == 1) {
                totalPrice = (totalVolume + order.volume) / (1 / totalPrice * totalVolume + 1 / order.price * order.volume);
                totalVolume += order.volume;
            }
        } else {
            totalPrice = (totalPrice * totalVolume + order.price * order.volume) / (totalVolume + order.volume);
            totalVolume += order.volume;
        }
    }
}
