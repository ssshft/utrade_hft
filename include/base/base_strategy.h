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
#include "hft_metrics.h"

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
        // 绑核放在线程体第一行：线程一旦被创建就已经在跑了，从外面钉有窗口期。
        // 这是 utrade 唯一的自旋线程，钉错了会直接抬高整条价差链路的 p99，
        // 所以失败必须报 ERROR 而不是静默继续。
        if (cpuHeavyWork >= 0 && crypto::set_cpu_self(cpuHeavyWork) != 0) {
            LOG_ERROR("utrade heavy_work: pin to core {} failed, running unpinned", cpuHeavyWork);
        }

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
                // ---- 这一轮的三件事，各量各的 ----
                // 全部在**同一条串行线程**（heavy_work）上，彼此是"相加"关系：
                //   一轮耗时 ≈ ut.rpt + ut.sweep + ut.timer（空转时还有 spin::backoff 的等待）
                // 所以不要把它们合成一个指标 —— 合成就只能看到总数，看不出是谁在涨。
                if (tb2TradeRcmd->tryPop(rcmd)) {
                    // 一条回报从出队到处理完：convert 判类型 + 按 strategyId 过滤 + 策略侧状态机。
                    // 速率是**每秒几次**（tb 只推成交/余额/仓位），所以全量计时。
                    //
                    // ⚠️ 这一条**不拆**创建/执行：成交回报只进执行侧（algoContext.OnOrder），
                    //    余额/仓位/总览两侧都进。而它每秒只有几条、单条 µs 级，
                    //    拆开得到的两个数只会互相淹没。真正贵的是下面两条（价差/定时）。
                    HFT_SCOPE("ut.rpt");
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
                    // 一次"全扫"：FetchLast 会把 submap 里**每个订阅的键**都看一遍，
                    // 有新数据就当场回调 on_dbpdata（→ ut.spread）。所以这里量的是
                    // "扫一遍全部对子的总成本"，p50 就是**空扫**成本（N 个对子 × 一次
                    // 共享内存 FetchLast），p99 才是"有行情那一轮"。
                    // ⚠️ 500 对子时空扫是 µs 级、每轮都做 —— 它要是涨了，说明对子数
                    //    或 submap 结构出了问题，不是行情变慢。
                    HFT_SCOPE("ut.sweep");
                    if (dbpreader->FetchLast()) {
                        idle = false;
                    }
                }
                    
                auto now = crypto::getCurrentTime();
                if (now - utcTime >= timerInterval * 1000) {
                    // 定时任务：AlgoContext 的 OnTimer（重算报单量/阈值、算法单超时）
                    // + PairTradingContext 的 OnTimer + 终结扫描。1 Hz（timerInterval=1000ms）。
                    HFT_SCOPE("ut.timer");
                    on_timer(now);
                    utcTime = now;
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("{}", e.what());
            }

            if (idle) {
                // 空转轮数：除以时间就是**轮询频率**，倒数 = 一条消息从进队到被 pop 的上界。
                // 这个数明显低于预期（几万/秒）时，说明 backoff 退得太狠，消息在队列里等。
                // 名字用 loop 而不是 exec：`ut.exec.*` 已经被"算法单执行侧"占用了。
                HFT_CNT("ut.loop.idle");
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

        // 线程绑核：只有 heavy_work 是自旋线程，需要独占一颗核；-1 = 不绑。
        if (configValue["op"].HasMember("cpuHeavyWork")) {
            cpuHeavyWork = stoi(configValue["op"]["cpuHeavyWork"].GetString());
        }
        LOG_INFO("cpu pin: heavyWork={} (-1 = not pinned)", cpuHeavyWork);

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
    int cpuHeavyWork{-1}; // heavy_work 的绑核号（-1 = 不绑），_init 里从 op.cpuheavywork 读
    std::unordered_map<std::string, std::string> _strategyIds;
    string tag{""};
    sm::SecurityManager* smc{nullptr};
};

