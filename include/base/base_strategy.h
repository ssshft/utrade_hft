#pragma once
#include "crypto_exception.h"
#include "data_struct.h"
#include "key_util.h"
#include "pubsub_protocol.h"
#include "pubsub/pubsub.h"
#include "shm_global.h"
#include "file_util.h"
#include "program_util.h"
#include "command_helper.h"
#include "config.h"
#include "databox.h"
#include "dbp/dbpreader.h"
#include "concurrent_queue.h"

using namespace pubsub;
using namespace md;

typedef pubsub::ConcurrentQueueWF<std::string, 1024*16> MPMCCMDQueue;

class BaseStrategy {
public:
    BaseStrategy() {
        std::string tradeConfigStr = crypto::read_file("/inc/trade_config.json");
        rapidjson::Document d;
        rapidjson::Value &tradeConfig = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(tradeConfigStr.c_str());
        int utrade2TbTCommandSHM = std::stoi(tradeConfig["OMS"]["Utrade2TbTCommandSHM"].GetString());
        std::string tb2UtradeRCommandSHM = tradeConfig["OMS"]["Tb2UtradeRCommandSHM"].GetString();

        tradeClient = new om::TradeClient(utrade2TbTCommandSHM);
        rcmdQueue = std::make_shared<pubsub::SPMCSubscriber<pubsub::RCommand>>(tb2UtradeRCommandSHM.c_str());
        cmd_queue = new MPMCCMDQueue();

        if (tradeConfig.HasMember("DBP")) {
            auto mpath = tradeConfig["DBP"]["mpath"].GetString();
            auto dpath = tradeConfig["DBP"]["dpath"].GetString();
            dbpreader = new dbp::DbpReader(mpath, dpath);
            auto f = std::bind(&BaseStrategy::on_dbpdata, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            dbpreader->SetCallback(f);
        }
        else {
            cryptothrow("trade_config.json not found DBP configuration!", -1);
        }
    }

    virtual void pre_start(Config *config){

    }

    void start(){
        std::thread heavyThread(&BaseStrategy::heavy_work, this);
        heavyThread.detach();
    }

    virtual void pre_stop() {
        if (dbpreader) {
            delete dbpreader;
            dbpreader = nullptr;
        }
    }

private:
    void heavy_work(){
        static long long utcTime = crypto::getCurrentTime();
        pubsub::RCommand rcmd;
        pubsub::Position position;
        pubsub::Balance balance;
        pubsub::OrderResponse orderResponse;
        pubsub::TotalAccount totalAccount;
        std::string cmdStr = "";

        while (1) {
            try {
                if (rcmdQueue->pop(rcmd)) {
                    if (crypto::convert_rcmd_2_ordertrade(rcmd, orderResponse)) {
                        auto found = _strategyIds.find(orderResponse.strategyId);
                        if (found) {
                            on_ordertrade(orderResponse);
                        }
                    }
                    else if (crypto::convert_rcmd_2_balance(rcmd, balance)) {
                        auto found = _strategyIds.find(balance.strategyId);
                        if (found) {
                            on_balance(balance);
                        }
                    }
                    else if (crypto::convert_rcmd_2_position(rcmd, position)) {
                        auto found = _strategyIds.find(balance.strategyId);
                        if (found) {
                            on_position(position);
                        }
                    } else if (crypto::convert_rcmd_2_total_account(rcmd, totalAccount)) {
                        auto found = _strategyIds.find(balance.strategyId);
                        if (found) {
                            on_total_account(totalAccount);
                        }
                    }
                    else {
                        LOG_ERROR("it should not happen here, please contact your developer!");
                    }
                }

                if (cmd_queue->pop(cmdStr)) {
                    _filter_on_command(cmdStr);
                }

                if (dbpreader) {
                    dbpreader->FetchLast();
                }
                    
                auto now = crypto::getCurrentTime();
                if (now - utcTime >= timerInterval * 1000) {
                    on_timer(now);
                    utcTime = now;
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("{}", e.what());
            }

        #ifdef NEED_SLEEP
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        #endif
        }
    }

protected:

    void _init(Config* config) {
        std::string configStr = config->get_document_str();
        rapidjson::Document d;
        rapidjson::Value &configValue = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(configStr.c_str());
        if(configValue.HasMember("tag")){
            tag = configValue["tag"].GetString();
        }
        else {
            cryptothrow("not found tag field in your config file", -1);
        }

        timerInterval = stoi(configValue["op"]["timerInterval"].GetString());
 
        if (configValue["op"].HasMember("strategyIds")) {
            for (rapidjson::SizeType i = 0; i < configValue["op"]["strategyIds"].Size(); i++) {
                auto strategyId = configValue["op"]["strategyIds"][i].GetString();
                if(crypto::str_cmp(strategyId, "") != false){
                    _strategyIds[strategyId] = strategyId;
                }
            }
        }

        string redisIp{"127.0.0.1"};
        int redisPort = 9379;
        string redisPasswd{""};
        if(configValue.HasMember("redis")){
            redisIp = configValue["redis"]["host"].GetString();
            redisPort = stoi(configValue["redis"]["port"].GetString());
            if(configValue["redis"].HasMember("passwd")){
                redisPasswd = configValue["redis"]["passwd"].GetString();
            }
            redisClient = new RedisClient(redisIp.c_str(), redisPort, redisPasswd.c_str());
        }
        else{
            cryptothrow("your config not found redis configuration!", -1);
        }
        string tradeConfigStr = crypto::read_file("/inc/trade_config.json");
        rapidjson::Document d1;
        rapidjson::Value &tradeConfig = d1.Parse<rapidjson::kParseNumbersAsStringsFlag>(tradeConfigStr.c_str());
        string AEC2UtradeChannel = tradeConfig["utrade"]["AEC2UtradeChannel"].GetString();
        
        auto _on_command = [&](const string &channel, const string &cmdStr) {
            cmd_queue->push(cmdStr);
        };
        redisClient->subscribe(AEC2UtradeChannel, _on_command);

    }

    //中控触发
    virtual void on_command(const string& cmdStr) = 0;//{ }

    //定时触发
    virtual void on_timer(const long &utcTime) = 0;//{ }

    //行情到达
    // virtual void on_marketdata(md::CryptoMarketData &cmd) = 0;//{ }

    //订单成交变化
    virtual void on_ordertrade(pubsub::OrderResponse& orderResponse) = 0;//{ }

    //资金推送
    virtual void on_balance(pubsub::Balance& balance) = 0;//{ }

    //仓位推送
    virtual void on_position(pubsub::Position& position) = 0;//{ }

    //账户总览推送
    virtual void on_total_account(pubsub::TotalAccount& totalAccount) = 0;//{ }

    //DBP推送
    virtual void on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* data, uint32_t jumpedNum) { }

    void _filter_on_command(const string& cmdStr){
        rapidjson::Document d;
        rapidjson::Value &cmdValue = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(cmdStr.c_str());
        if(crypto::str_cmp(tag.c_str(), cmdValue["toUtrade"].GetString()) == false){
            return;
        }

        on_command(cmdStr);
    }

    void _my_on_command(const string &cmdStr){}

protected:
    dbp::DbpReader* dbpreader{nullptr};
    std::shared_ptr<pubsub::SPMCSubscriber<pubsub::RCommand>> rcmdQueue{nullptr};
    Config* config{nullptr};
    om::TradeClient* tradeClient{nullptr};
    int timerInterval{1}; //ms
    MPMCCMDQueue* cmd_queue{nullptr};
    std::unordered_map<std::string, std::string> _strategyIds;
    RedisClient* redisClient{nullptr};
    string tag{""};
};

