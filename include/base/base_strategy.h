#pragma once
#include "crypto_exception.h"
#include "data_struct.h"
#include "key_util.h"
#include "pubsub_protocol.h"
#include "pubsub/pubsub.h"
#include "shm_global.h"
#include "program_util.h"
#include "command_helper.h"
#include "config.h"
#include "dbp/dbpreader.h"
#include "concurrent_queue.h"
#include "securitymanager.h"
#include <unordered_map>
#include "SpinBackoff.h"

using namespace pubsub;
using namespace md;

class BaseStrategy {
public:
    BaseStrategy() {
        std::string tradeConfigStr = crypto::read_file("/inc/trade_config.json");
        rapidjson::Document d;
        rapidjson::Value &tradeConfig = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(tradeConfigStr.c_str());

        utrade2TbTCommandSHM = tradeConfig["OMS"]["Utrade2TbTCommandSHM"].GetString();
        tb2TradeRcmd = new Tb2UtradeRcmd(tradeConfig["OMS"]["Tb2UtradeRCommandSHM"].GetString());

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

        std::string host = "localhost";
        int port = 9379;
        std::string password = "";
        if (tradeConfig.HasMember("SMC")) {
            host = tradeConfig["SMC"]["host"].GetString();
            port = std::stoi(tradeConfig["SMC"]["port"].GetString());
            if (tradeConfig["SMC"].HasMember("password")) {
                password = tradeConfig["SMC"]["password"].GetString();
            }
        }
        smc = new sm::SecurityManager(host.c_str(), port, password.c_str(), true);
    }

    virtual void pre_start(Config *config){

    }

    void start(){
        std::thread heavyThread(&BaseStrategy::heavy_work, this);
        heavyThread.detach();
    }

    virtual void pre_stop() {

    }

    ~BaseStrategy() {
        if (dbpreader) {
            delete dbpreader;
            dbpreader = nullptr;
        }

        if (smc) {
            delete smc;
            smc = nullptr;
        }

        if (tb2TradeRcmd) {
            delete tb2TradeRcmd;
            tb2TradeRcmd = nullptr;
        }

        if (tradeClient) {
            delete tradeClient;
            tradeClient = nullptr;
        }
    }

private:
    void heavy_work(){
        int64_t utcTime = crypto::getCurrentTime();
        pubsub::RCommand rcmd;
        pubsub::Position position;
        pubsub::Balance balance;
        pubsub::OrderResponse orderResponse;
        pubsub::TotalAccount totalAccount;
        unsigned step = 0;
        while (1) {
            bool idle = true;
            try {
                if (tb2TradeRcmd->tryPop(rcmd)) {
                    idle = false;
                    if (crypto::convert_rcmd_2_ordertrade(rcmd, orderResponse)) {
                        auto found = _strategyIds.find(orderResponse.strategyId);
                        if (found != _strategyIds.end()) {
                            on_ordertrade(orderResponse);
                        }
                    }
                    else if (crypto::convert_rcmd_2_balance(rcmd, balance)) {
                        auto found = _strategyIds.find(balance.strategyId);
                        if (found != _strategyIds.end()) {
                            on_balance(balance);
                        }
                    }
                    else if (crypto::convert_rcmd_2_position(rcmd, position)) {
                        auto found = _strategyIds.find(position.strategyId);
                        if (found != _strategyIds.end()) {
                            on_position(position);
                        }
                    } else if (crypto::convert_rcmd_2_total_account(rcmd, totalAccount)) {
                        auto found = _strategyIds.find(totalAccount.strategyId);
                        if (found != _strategyIds.end()) {
                            on_total_account(totalAccount);
                        }
                    }
                    else {
                        LOG_ERROR("it should not happen here, please contact your developer!");
                    }
                }

                if (dbpreader) {
                    if (dbpreader->FetchLast()) {
                        idle = false;
                    }
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

            if (idle) {
                spin::backoff(step);
            }
            else {
                step = 0;
            }
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

        std::vector<std::string> vSid;
        if (configValue["op"].HasMember("strategyIds")) {
            for (rapidjson::SizeType i = 0; i < configValue["op"]["strategyIds"].Size(); i++) {
                auto strategyId = configValue["op"]["strategyIds"][i].GetString();
                if (!crypto::str_cmp(strategyId, "")) {
                    _strategyIds[strategyId] = strategyId;
                    vSid.push_back(strategyId);
                }
            }
        }

        std::cout << "---------start new TradeClient--------- " << vSid[0] << " ------ " << vSid[1] << std::endl;
        tradeClient = new om::TradeClient(utrade2TbTCommandSHM, vSid);
    }

    //定时触发
    virtual void on_timer(const int64_t& utcTime) = 0;//{ }

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

protected:
    dbp::DbpReader* dbpreader{nullptr};
    Tb2UtradeRcmd* tb2TradeRcmd{nullptr};
    std::string utrade2TbTCommandSHM{""};
    om::TradeClient* tradeClient{nullptr};
    int timerInterval{1}; //ms
    std::unordered_map<std::string, std::string> _strategyIds;
    string tag{""};
    sm::SecurityManager* smc{nullptr};
};

