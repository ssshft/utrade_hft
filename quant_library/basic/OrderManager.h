#ifndef _ORDER_MANAGER_H
#define _ORDER_MANAGER_H

#include "DataStruct.h"
#include "pubsub_protocol.h"
#include <vector>
#include <unordered_map>
#include <string>

using namespace std;

// transfer order装换：回报到自定义对象
class OrderManager {
    public:
        OrderManager();
        ~OrderManager();
        // void Init(ShmManager* shm);
        // void Recovery();
        void InsertOrderByOrder(const stra::QuantOrder& order);
        void UpdateOrderOnCancel(const stra::QuantOrder& order);
        void DeleteOrderByOrder(const stra::QuantOrder& order);
        stra::QuantOrder UpdateOrderOnOrder(const pubsub::OrderResponse& orderResponse);
        stra::QuantOrder UpdateOrderOnQueryOrder(const pubsub::OrderResponse& orderResponse);
 
        unordered_map<int64_t, stra::QuantOrder>& GetAllOrders();
        stra::QuantOrder SelectOrderByStrategyOrderId(int64_t strategyOrderId);
        void DeleteOrdrByStrategyOrderId(int64_t strategyOrderId);
        void CalculateTotalPriceVolume();

    private:
        // ShmManager* shmManager;

        unordered_map<int64_t, stra::QuantOrder> mOrder;

        double totalPrice;
        double totalVolume;
};

#endif