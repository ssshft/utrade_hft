#ifndef _ORDER_MANAGER_H
#define _ORDER_MANAGER_H

#include "DataStruct.h"
#include "ShmManager.h"
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
        void RecoveryFromFile(string filePath);
        void InsertTransferByTransfer(const stra::QuantTransfer& transfer);
        void UpdateTransferOnTransfer(const stra::TdTransfer& transfer, int64_t eventTime);
        void DeleteTransferByTransfer(const stra::QuantTransfer& transfer);

        void InsertOrderByOrder(const stra::QuantOrder& order);
        void UpdateOrderOnCancel(const stra::QuantOrder& order, int64_t eventTime);
        void DeleteOrderByOrder(const stra::QuantOrder& order);
        stra::QuantOrder UpdateOrderOnOrder(const stra::TdOrder& tdOrder, int64_t eventTime);
        stra::QuantOrder UpdateOrderOnQueryOrder(const stra::TdOrder& tdOrder, int64_t eventTime);
 
        unordered_map<int64_t, stra::QuantOrder>& GetAllOrders();
        stra::QuantOrder SelectOrderByStrategyOrderId(int64_t strategyOrderId);
        void DeleteOrdrByStrategyOrderId(int64_t strategyOrderId);
        unordered_map<int64_t, stra::QuantTransfer>& GetAllTransfers();
        void CalculateTotalPriceVolume();

    private:
        // ShmManager* shmManager;

        unordered_map<int64_t, stra::QuantTransfer> mTransfer;
        unordered_map<int64_t, stra::QuantOrder> mOrder;

        double totalPrice;
        double totalVolume;
};

#endif