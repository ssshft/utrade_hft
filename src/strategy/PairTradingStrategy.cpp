#include "strategy/PairTradingStrategy.h"
#include "basic/DataStruct.h"
#include "basic/PairInfoManager.h"
#include "basic/WriteFileContent.h"

#include "log_engine.h"
#include <chrono>
#include <cstring>


using namespace std::chrono;

static int64_t NowUs() {
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

PairTradingStrategy::PairTradingStrategy() {
    algoContext.SetTradeClient(tradeClient);
};

PairTradingStrategy::~PairTradingStrategy() {

};

void PairTradingStrategy::pre_start(Config* config) {
    lastOnCommand = crypto::getCurrentTime();

    _init(config);

    std::string cfgStr = config->get_document_str();
    rapidjson::Document d;
    rapidjson::Value& v = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(cfgStr.c_str());

    auto& op = v["op"];

    m_ptCfg.activeAccountId = std::stoi(op["activeAccountId"].GetString());
    m_ptCfg.passiveAccountId = std::stoi(op["passiveAccountId"].GetString());

    if (op.HasMember("pairKeys")) {
        for (auto& pk: op["pairKeys"].GetArray()) {
            std::string pairKey = pk.GetString();
            m_ptCfg.pairKeys.emplace_back(pairKey);
            //dbpreader->Subscribe(pairKey);
        }
    }

    if (op.HasMember("maxPositionValue")) {
        m_ptCfg.maxPositionValue = std::stod(op["maxPositionValue"].GetString());
    }
    if (op.HasMember("maxAmount")) {
        m_ptCfg.maxAmount = std::stod(op["maxAmount"].GetString());
    }
    if (op.HasMember("targetAmount")) {
        m_ptCfg.targetAmount = std::stod(op["targetAmount"].GetString());
    }

    if (op.HasMember("csvStatePath")) {
        m_ptCfg.csvStatePath = op["csvStatePath"].GetString();
    }

    algoContext.Init(smc);
    algoContext.SetDbp(dbpreader);
    algoContext.PreStart();

    ptContext.SetAlgoCommandCallback([this](BaseAlgoOrder* pAlgoOrder) { 
        SubmitAlgoCommand(pAlgoOrder); 
    });

    //ptContext.Init(m_ptCfg, smc);
}

void PairTradingStrategy::pre_stop() {
    if (!m_ptCfg.csvStatePath.empty()) {
        pt::PairInfoManager::Instance().SaveToCSV(m_ptCfg.csvStatePath);
    }

    // 显式停掉落库线程并 flush 缓冲区。
    // 只靠 ~WriteFileContent() 不够：exit() 里的静态析构顺序跨 TU 未定义，
    // 万一 contentQueue（BaseAlgoOrder.cpp 里的全局对象）先被析构，
    // 写线程还在跑就会访问已析构对象。
    WriteFileContent::GetInstance().Stop();

    BaseStrategy::pre_stop();
}

void PairTradingStrategy::SubmitAlgoCommand(BaseAlgoOrder* pAlgoOrder) {
    algoContext.SubmitAlgoOrder(pAlgoOrder);
}

void PairTradingStrategy::on_command(const std::string& json) {
    algoContext.OnCommand(json);
}

void PairTradingStrategy::on_timer(const int64_t& utcTime) {
    algoContext.OnTimer(utcTime);
    //ptContext.OnTimer(utcTime);

    if (utcTime - m_lastScanUs >= SCAN_INTERVAL_US) {
        //ScanFinishedAlgoOrders(utcTime);
        m_lastScanUs = utcTime;
    }


    if (utcTime - lastOnCommand > 10000000LL) {
        if (!createAlgo) {
            algoContext.OnCommand("");
            lastOnCommand = utcTime;
            createAlgo = true;
        }
    }
}

void PairTradingStrategy::on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum) {
    std::cout << topic->__name << " " << pdata->activeAskPrice[0] << " " << pdata->activeBidPrice[0] << " " << pdata->passiveAskPrice[0] << " " << pdata->passiveBidPrice[0] << std::endl;
    algoContext.OnSpread(topic, pdata);
    //ptContext.OnSpread(topic, pdata);
}


void PairTradingStrategy::on_balance(pubsub::Balance& balance) {
    algoContext.OnBalance(balance);
    //ptContext.OnBalance(balance);
}


void PairTradingStrategy::on_position(pubsub::Position& position) {
    algoContext.OnPosition(position);
    //ptContext.OnPosition(position);
}


void PairTradingStrategy::on_total_account(pubsub::TotalAccount& totalAccount) {
    algoContext.OnTotalAccount(totalAccount);
    //ptContext.OnTotalAccount(totalAccount);
}


void PairTradingStrategy::on_ordertrade(pubsub::OrderResponse& orderResponse) {
    algoContext.OnOrder(orderResponse);
}

void PairTradingStrategy::ScanFinishedAlgoOrders(int64_t nowUs) {
    auto& pim = pt::PairInfoManager::Instance();

    for (pt::PairInfo* pi : pim.GetAllPairInfos()) {
        if (!pi->hasActiveAlgoOrder) {
            continue;
        }

        int64_t algoOrderIdInt = std::stoll(pi->currentAlgoOrderId);
        BaseAlgoOrder* order = algoContext.GetAlgoOrder(algoOrderIdInt);

        if (!order) {
            double volFilled = order ? order->pairTotalVolume - pi->pairTotalVolume : 0.0;
            bool fullyFlat = !pi->HasPosition();

            ptContext.OnAlgoOrderUpdate(pi->pairInstrumentKey, pi->currentAlgoOrderId, volFilled, 0.0, 0.0, true, fullyFlat);
        }
        else {
            if (order->algoOrderStatus == stra::ALGO_OS_FILLED || order->algoOrderStatus == stra::ALGO_OS_CANCELED || order->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED) {
                double volFilled = order->pairTotalVolume - pi->pairTotalVolume;
                bool fullyFlat = !pi->HasPosition();

                ptContext.OnAlgoOrderUpdate(pi->pairInstrumentKey, pi->currentAlgoOrderId, volFilled, order->pairActiveTotalPrice, order->pairPassiveTotalPrice, true, fullyFlat);
            }
        }
    }
}