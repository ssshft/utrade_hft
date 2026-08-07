#ifndef _BASIC_INFO_MGR_H
#define _BASIC_INFO_MGR_H

#include "Utility.h"
#include <unistd.h>
#include <cpp_redis/cpp_redis>
#include <cpprest/json.h>


using namespace std;

namespace stra {
    struct InstrumentInfo {
	    string exchangeType{""};
	    string instrumentType{""};
	    string instrumentId{""};
	    string originInstrumentId{""};
	    double multiple{0.0};
	    double multipleVolume{0};
	    int calculateType{0};
	    string deliveryDate{""};
    	int lever{0};
	    double tickSize{0.0};
	    double lotSize{0.0};
	    double minSize{0.0};
        double minAmount{0.0};
	    string instLeft{""};
	    string instRight{""};
	    string margin{""};
        string left{""};
        string right{""};
    };
}


inline double GetAmountByVolumePrice(const stra::InstrumentInfo& info, string baseAsset, double volume, double price) {
    double amount = 0.0;
    if (info.instRight == baseAsset || ((info.instRight == "USDT" || info.instRight == "USD" || info.instRight == "USDC" || info.instRight == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calculateType == 0) {
            amount = volume * info.multiple;
        } else if (info.calculateType == 1) {
            amount = volume * info.multiple / price;
        }
    } else if (info.instLeft == baseAsset || ((info.instLeft == "USDT" || info.instLeft == "USD" || info.instLeft == "USDC" || info.instLeft == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calculateType == 0) {
            amount = volume * info.multiple * price;
        } else if (info.calculateType == 1) {
            amount = volume * info.multiple;
        }
    }
    return amount;
}

inline double GetVolumeByAmountPrice(const stra::InstrumentInfo& info, string baseAsset, double amount, double price) {
    double volume = 0.0;
    if (info.instRight == baseAsset || ((info.instRight == "USDT" || info.instRight == "USD" || info.instRight == "USDC" || info.instRight == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calculateType == 0) {
            volume = amount / info.multiple;
        } else if (info.calculateType == 1) {
            volume = amount * price / info.multiple;
        }
    } else if (info.instLeft == baseAsset || ((info.instLeft == "USDT" || info.instLeft == "USD" || info.instLeft == "USDC" || info.instLeft == "BUSD") && (baseAsset == "USDT" || baseAsset == "USD" || baseAsset == "USDC" || baseAsset == "BUSD"))) {
        if (info.calculateType == 0) {
            volume = amount / price / info.multiple;
        } else if (info.calculateType == 1) {
            volume = amount / info.multiple;
        }
    }
    return volume;
}

class BasicInfoMgr {
public:
    static BasicInfoMgr& GetInstance();
	~BasicInfoMgr();
    void ConnectRedis();
    void MaintainRedisConnected();
    void Init();
    stra::InstrumentInfo& GetBasicInfo(string key);
    string& GetSysIdByOriginId(string instrumentKey);
    bool GetInstrumentInfo(string exchId, string instType, string instId, string& value);
    bool GetAllInstrumentInfo(string& value);
    void UpdateInstrumentInfo();
    unordered_map<string, stra::InstrumentInfo>& GetAllInstrumentInfo();
    bool GetInstrumentInfo(string key, stra::InstrumentInfo& info)

private:
	BasicInfoMgr();
    bool Get(const char* key, string& value);
    unordered_map<string, stra::InstrumentInfo> mInstrumentInfo;
    unordered_map<string, string> mOriginIdSysId;
    cpp_redis::client client;
    bool isConnected;
    bool maintainFlag;
    thread* maintainRedisConnected;
};

#endif