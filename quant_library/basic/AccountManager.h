#ifndef _ACCOUNT_MANAGER_H
#define _ACCOUNT_MANAGER_H

#include <string>
#include <unordered_map>
#include "DataStruct.h"
#include "securitymanager.h"

using namespace  std;


class AccountManager {
    public:
        static AccountManager& Instance();
        ~AccountManager();
        void Init(sm::SecurityManager* s);
        void OnBalance(const pubsub::Balance& balance);
        void OnPosition(const pubsub::Position& position);
        void OnTotalAccount(const pubsub::TotalAccount& totalAccount);
    
        void OnInsertOrder(const stra::QuantOrder& order);
        void OnDeleteOrder(const stra::QuantOrder& order);
        void OnOrder(const stra::QuantOrder& order);
        //void UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth);
        bool FundVerify(const stra::QuantOrder& order, const md::InstrumentInfo& info);
        bool FundVerifyUnified(const stra::QuantOrder& order, const md::InstrumentInfo& info);
        bool FundVerifyClassic(const stra::QuantOrder& order, const md::InstrumentInfo& info);

    private:
        AccountManager();
        unordered_map<int, stra::QuantAccount> mAccount;
        sm::SecurityManager* smc{nullptr};
};

#endif