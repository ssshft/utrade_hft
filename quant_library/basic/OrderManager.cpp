#include "OrderManager.h"
#include "Utility.h"
#include "BasicInfoMgr.h"

OrderManager::OrderManager() {
}

OrderManager::~OrderManager() {
    mTransfer.clear();
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

void OrderManager::RecoveryFromFile(string filePath) {
    unordered_map<int64_t, stra::QuantOrder> mOrd;

    ifstream f;
    f.open(filePath.c_str(), ios::in);
    if (f.is_open()) {
        string line;
        while (getline(f, line)) {
            vector<string> v;
            splitString(line, v, ",");
            if (v.size() >= 45) {
                stra::QuantOrder order;
                order.strategyOrderId = stoll(v[0]);
                order.systemOrderId = stoll(v[1]);
                strncpy(order.exchangeOrderId, v[2].c_str(), stra::ID_LEN);
                order.strategyAccountId = stoi(v[3]);
                order.pairId = stoll(v[4]);
                order.algoPairId = stoll(v[5]);
                strncpy(order.instrument, v[6].c_str(), stra::INST_ID_LEN);
                strncpy(order.instrumentKey, v[7].c_str(), stra::INST_KEY_LEN);
                strncpy(order.pairInstrumentKey, v[8].c_str(), stra::INST_KEY_LEN);
                order.exchangeType = ExchangeType(stoi(v[9]));
                order.instType = InstType(stoi(v[10]));
                order.orderType = OrderType(stoi(v[11]));
                order.posDirection = stra::PosDirection(stoi(v[12]));
                order.direction = Direction(stoi(v[13]));
                order.marginType = stra::MarginType(stoi(v[14]));
                order.orderStatus = OrderStatus(stoi(v[15]));
                order.tradingType = stra::TradingType(stoi(v[16]));
                order.price = stod(v[17]);
                order.volume = stod(v[18]);
                order.targetPrice = stod(v[19]);
                order.volumeFront = stod(v[20]);
                order.volumeAfter = stod(v[21]);
                order.totalPriceOnOrder = stod(v[22]);
                order.totalVolumeOnOrder = stod(v[23]);
                order.lastTotalPriceOnOrder = stod(v[24]);
                order.lastTotalVolumeOnOrder = stod(v[25]);
                order.tradePrice = stod(v[26]);
                order.tradeVolume = stod(v[27]);
                order.tradeFee = stod(v[28]);
                strncpy(order.tradeFeeCurrency, v[29].c_str(), stra::ASSET_LEN);

                vector<string> vShortFee;
                splitString(v[30], vShortFee, "-");
                if (vShortFee.size() >= 2) {
                    strncpy(order.tradeShortFee.asset, vShortFee[0].c_str(), stra::ASSET_LEN);
                    order.tradeShortFee.amount = stod(vShortFee[1]);
                }

                vector<string> vLongFee;
                splitString(v[31], vLongFee, "-");
                if (vLongFee.size() >= 2) {
                    strncpy(order.tradeLongFee.asset, vLongFee[0].c_str(), stra::ASSET_LEN);
                    order.tradeLongFee.amount = stod(vLongFee[1]);
                }

                vector<string> vTotalShortFee;
                splitString(v[32], vTotalShortFee, "|");
                for (size_t i = 0; i < vTotalShortFee.size(); ++i) {
                    vector<string> vShortFee;
                    splitString(vTotalShortFee[i], vShortFee, "-");
                    if (vShortFee.size() >= 2) {
                        auto& dt = order.totalShortFee.detail[order.totalShortFee.size];
                        strncpy(dt.asset, vShortFee[0].c_str(), stra::ASSET_LEN);
                        dt.amount = stod(vShortFee[1]);
                        order.totalShortFee.size++;
                    }
                }

                vector<string> vTotalLongFee;
                splitString(v[33], vTotalLongFee, "|");
                for (size_t i = 0; i < vTotalLongFee.size(); ++i) {
                    vector<string> vLongFee;
                    splitString(vTotalLongFee[i], vLongFee, "-");
                    if (vLongFee.size() >= 2) {
                        auto& dt = order.totalLongFee.detail[order.totalLongFee.size];
                        strncpy(dt.asset, vLongFee[0].c_str(), stra::ASSET_LEN);
                        dt.amount = stod(vLongFee[1]);
                        order.totalLongFee.size++;
                    }
                }

                vector<string> vTimeStatus;
                splitString(v[34], vTimeStatus, "|");
                for (size_t i = 0; i < vTimeStatus.size(); ++i) {
                    vector<string> vStatus;
                    splitString(vTimeStatus[i], vStatus, "-");
                    if (vStatus.size() >= 2) {
                        auto& dt = order.orderTimeStatus.detail[order.orderTimeStatus.size];
                        dt.updateTime = stoll(vStatus[0]);
                        dt.orderStatus = stra::OrderStatus(stoi(vStatus[1]));
                        order.orderTimeStatus.size++;
                    }
                }

                order.errorId = stoi(v[35]);
                strncpy(order.originErrorMsg, v[36].c_str(), stra::MSG_LEN);
                strncpy(order.strategyName, v[37].c_str(), stra::NAME_LEN);
                order.reduceOnly = bool(stoi(v[38]));
                order.isActiveOrder = bool(stoi(v[39]));
                order.rebalance = bool(stoi(v[40]));
                order.orderTime = stoll(v[41]);
                order.updateTime = stoll(v[42]);
                order.killTime = stoll(v[43]);
                order.queryCount = stoi(v[44]);

                mOrd[order.strategyOrderId] = order;
            }
        }
    }

    int64_t currentTime = GetCurrentTimeUs();
    int64_t oneDayUs = 24 * 60 * 60 * 1000 * 1000;
    for (auto iter = mOrd.begin(); iter != mOrd.end(); ++iter) {
        auto& quantOrder = iter->second;
        if ((quantOrder.orderStatus == OS_PENDING_NEW || quantOrder.orderStatus == OS_PARTFILLED || quantOrder.orderStatus == OS_CANCELLING) && currentTime - quantOrder.updateTime < oneDayUs) {
            InsertOrderByOrder(quantOrder);
        }
    }
}

void OrderManager::UpdateTransferOnTransfer(const stra::TdTransfer& transfer) {
    int64_t nowTime = crypot::getCurrentTime();
    auto it = mTransfer.find(transfer.clOrdId);
    if (it != mTransfer.end()) {
        it->second.transferStatus = transfer.orderStatus;
        it->second.updateTime = nowTime;

        auto& dt = it->second.transferTimeStatus.detail[it->second.transferTimeStatus.size];
        dt.orderStatus = transfer.orderStatus;
        dt.updateTime = nowTime;
        it->second.transferTimeStatus.size++;

        if (it->second.transferTimeStatus.size >= stra::TIME_STATUS_LEN) {
            it->second.transferTimeStatus.size = stra::TIME_STATUS_LEN - 1;
            LOG_INFO("transferTimeStatus size:%d > TIME_STATUS_LEN:%d", it->second.transferTimeStatus.size, stra::TIME_STATUS_LEN);
        }
    }
}

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
        ord.orderStatus = stra::OrderStatus_CANCEL;
        ord.updateTime = nowTime;

        auto& dt = ord.orderTimeStatus.detail[ord.orderTimeStatus.size];
        dt.orderStatus = stra::OrderStatus_CANCEL;
        dt.updateTime = nowTime;
        ord.orderTimeStatus.size++;

        if (ord.orderTimeStatus.size >= stra::TIME_STATUS_LEN) {
            ord.orderTimeStatus.size = stra::TIME_STATUS_LEN - 1;
            LOG_INFO("orderTimeStatus size:%d > TIME_STATUS_LEN:%d", ord.orderTimeStatus.size, stra::TIME_STATUS_LEN);
        }
    }
}

void OrderManager::CalculateTotalPriceVolume() {
    totalPrice = 0.0;
    totalVolume = 0.0;
    for (auto it = mOrder.begin(); it != mOrder.end(); ++it) {
        auto& order = it->second;
        string instrumentKey = order.instrumentKey;
        stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(instrumentKey);
        
        if (order.instType == USDT_SWAP || order.instType == USDT_FUTURES) {
            if (info.calculateType == 0) {
                totalPrice = (totalPrice * totalVolume + order.price * order.volume) / (totalVolume + order.volume);
                totalVolume += order.volume;
            } else if (info.calculateType == 1) {
                totalPrice = (totalVolume + order.volume) / (1 / totalPrice * totalVolume + 1 / order.price * order.volume);
                totalVolume += order.volume;
            }
        } else {
            totalPrice = (totalPrice * totalVolume + order.price * order.volume) / (totalVolume + order.volume);
            totalVolume += order.volume;
        }
    }
}
