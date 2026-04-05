#ifndef _ACCOUNT_MANAGER_H
#define _ACCOUNT_MANAGER_H

#include <string>
#include <unordered_map>
#include "DataStruct.h"

using namespace  std;


class AccountManager {
    public:
        static AccountManager& Instance();
        ~AccountManager();
        void Init();
        void OnBalance(const stra::TdBalance& balance);
        void OnPosition(const stra::TdPosition& position);
        void OnTotalAccount(const stra::TdTotalAccount& totalAccount);
        void UpdateAccountOnPosition(const stra::ExAccountInfo& accountInfo);
        void OnInsertTransfer(const stra::QuantTransfer& transfer);
        void OnInsertLending(const stra::QuantLending& lending);
        void UpdateTransferOnTransfer(const stra::TdTransfer& transfer, int64_t eventTime);
        void UpdateLendingOnLending(const stra::TdLending& lending, int64_t eventTime);
        void OnInsertOrder(const stra::QuantOrder& order);
        void OnDeleteOrder(const stra::QuantOrder& order);
        void OnOrder(const stra::QuantOrder& order);
        void UpdateAccountOnMarketDepth(const stra::QuantMarketDepth& depth);
        bool FundVerify(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info);
        bool FundVerifyUnified(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info);
        bool FundVerifyClassic(const stra::QuantOrder& order, double assetTick, stra::InstrumentInfo& info);
        stra::QuantTransfer GetTransfer(const stra::QuantOrder& order, string requireAsset, double requireAmount, int64_t strategyTransferId);
        void LoadFromFile(string filePath);
        void SaveToFile(string filePath);

    private:
        AccountManager();
        unordered_map<int64_t, stra::QuantTransfer> mTransfer;
        unordered_map<int, stra::QuantAccount> mAccount;
        unordered_map<int64_t, stra::QuantLending> mLending;
        bool check;
};

#endif