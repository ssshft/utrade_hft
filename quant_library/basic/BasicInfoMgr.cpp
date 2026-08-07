#include "BasicInfoMgr.h"
#include "StrategyConfig.h"

BasicInfoMgr::BasicInfoMgr() {
    ConnectRedis();
    Init();
    maintainFlag = true;
    maintainRedisConnected = new thread(&BasicInfoMgr::MaintainRedisConnected, this);
}

BasicInfoMgr::~BasicInfoMgr() {
    client.disconnect();
    maintainFlag = false;
    if (maintainRedisConnected) {
        delete maintainRedisConnected;
        maintainRedisConnected = nullptr;
    }
}

BasicInfoMgr& BasicInfoMgr::GetInstance() {
	static BasicInfoMgr basicInfoMgr;
	return basicInfoMgr;
}

void BasicInfoMgr::ConnectRedis() {
    // 需要在配置文件中设置
    string dbAddr = StrategyConfig::GetInstance().GetMdAddr();
    int dbPort = StrategyConfig::GetInstance().GetMdPort();
    string dbPassword = StrategyConfig::GetInstance().GetMdPassword();


    client.disconnect();
    client.connect(dbAddr, dbPort, [&](const string &host, size_t port, cpp_redis::connect_state status) {
        if (status == cpp_redis::connect_state::ok) {
            isConnected = true;
            LOG_INFO("redis client connected with %s:%lu", host.c_str(), port);
            if (dbPassword.length() > 0) {
                client.auth(dbPassword, [this](const cpp_redis::reply& reply) {
                    LOG_INFO("auth info: %s", reply.as_string().c_str());
                });
            }
        }
        else if (status == cpp_redis::connect_state::dropped) {
            isConnected = false;
            LOG_ERROR("redis client disconnected with %s:%lu", host.c_str(), port);
        }
    });

    client.sync_commit();
}

void BasicInfoMgr::MaintainRedisConnected() {
    int cnt = 0;
    while (maintainFlag) {
        try {
            if(isConnected == false) {
                ConnectRedis();
            }

	        if (cnt >= 180) {
                UpdateInstrumentInfo();
		        cnt = 0;
            }
        } catch(exception& e) {
            //LOG_ERROR("%s", e.what());
        }
	cnt++;
        sleep(10);
    }
}

void BasicInfoMgr::Init() {
    UpdateInstrumentInfo();
}

stra::InstrumentInfo& BasicInfoMgr::GetBasicInfo(string key) { 
    return mInstrumentInfo[key];
}

string& BasicInfoMgr::GetSysIdByOriginId(string instrumentKey) { 
    return mOriginIdSysId[instrumentKey];
}

void BasicInfoMgr::UpdateInstrumentInfo() {
    string value = "";
    GetAllInstrumentInfo(value);
    if (value.length() > 0) {
        const web::json::value& content = web::json::value::parse(value.c_str());
        if (content.is_array()) {
            auto& arr = content.as_array();
            for (auto& a: arr) {
                if (a.is_object()) {
                    stra::InstrumentInfo info;
                    if (a.has_field("exchId")) {
                        info.exchangeType = a.at("exchId").as_string();
                    }
                    if (a.has_field("instType")) {
                        info.instrumentType = a.at("instType").as_string();
                    }
                    if (a.has_field("instId")) {
                        info.instrumentId = a.at("instId").as_string();
                    }
                    if (a.has_field("originInstId")) {
                        info.originInstrumentId = a.at("originInstId").as_string();
                    }

                    string base = "";
                    if (a.has_field("base")) {
                        base = a.at("base").as_string();
                    }

	                string quote = "";
                    if (a.has_field("quote")) {
                        quote = a.at("quote").as_string();
                    }

                    if (a.has_field("multipleVolume")) {
                        info.multipleVolume = a.at("multipleVolume").as_double();
                    }

                    if (a.has_field("lever")) {
                        info.lever = a.at("lever").as_integer();
                    }

                    if (a.has_field("tickSize")) {
                        info.tickSize = a.at("tickSize").as_double();
                    }

                    if (a.has_field("lotSize")) {
                        info.lotSize = a.at("lotSize").as_double();
                    }

                    if (a.has_field("minSize")) {
                        info.minSize = a.at("minSize").as_double();
                    }

                    if (a.has_field("minAmount")) {
                        info.minAmount = a.at("minAmount").as_double();
                    }

                    string key = info.exchangeType + "." + info.instrumentType + "." + info.instrumentId;
                    string instrumentKey = info.exchangeType + "." + info.instrumentType + "." + info.originInstrumentId;

                    if (info.instrumentType == "SPOT" || info.instrumentType == "InstType_SPOT") {
                        info.multiple = 1;
                        info.calculateType = 0;
                        info.instLeft = base;
                        info.instRight = quote;
                        info.left = base;
                        info.right = quote;
                    } else if (info.instrumentType == "FUTURES" || info.instrumentType == "SWAP" || info.instrumentType == "InstType_USDT_SWAP" || info.instrumentType == "InstType_BUSD_SWAP" || info.instrumentType == "InstType_C_SWAP" || info.instrumentType == "InstType_USDT_FUTURES" || info.instrumentType == "InstType_C_FUTURES") {
                        if (a.has_field("value")) {
                            info.multiple = a.at("value").as_double();
                        }

                        string margin = "";
                        if (a.has_field("margin")) {
                            margin = a.at("margin").as_string();
                        }
                        
                        if (margin == quote) {
                            info.calculateType = 0;   // u本位
                        } else if (margin == base) {
                            info.calculateType = 1;   // 币本位
                        }
                        info.instLeft = base;
                        info.instRight = quote;
                        // if (quote == "USD") {
                        //     info.instRight = "USDT";
                        // } else {
                        //     info.instRight = quote;
                        // }
                        info.margin = margin;
                        info.left = margin;
                        info.right = margin;

                        instrumentKey = info.exchangeType + ".FUTURES" + "." + info.originInstrumentId;
                    }
                    
                    mOriginIdSysId[instrumentKey] = key;
                    mInstrumentInfo[key] = info;
                }    
            }
        }
    }
}

bool BasicInfoMgr::Get(const char* key, string& value) {
    try {
        if (isConnected) {
            client.get(key, [&value](cpp_redis::reply& reply) {
                if (reply.is_string()) {
                    value = reply.as_string();
                    return true;
                }
                else {
                    return false;
                }
            });
            client.sync_commit();
            return true;
        }
        return false;
    }
    catch (exception &e){
        //LOG_ERROR("%s", e.what());
        return false;
    }
}

bool BasicInfoMgr::GetInstrumentInfo(string exchange, string instType, string instId, string& value) {
    string key = exchange + "." + instType + "." + instId + ".INSTRUMENT_INFO";
    return Get(key.c_str(), value);
}

bool BasicInfoMgr::GetAllInstrumentInfo(string& value) {
    string key = "SMC.ALL.INSTRUMENTINFO";
    return Get(key.c_str(), value);
}

unordered_map<string, stra::InstrumentInfo>& BasicInfoMgr::GetAllInstrumentInfo() {
    return mInstrumentInfo;
}

bool BasicInfoMgr::GetInstrumentInfo(string key, stra::InstrumentInfo& info) {
    bool find = false;
    if (mInstrumentInfo.find(key) != mInstrumentInfo.end()) {
        info = mInstrumentInfo[key];
        find = true;
    }
    return find;
}
