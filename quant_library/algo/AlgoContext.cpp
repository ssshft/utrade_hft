#include "AlgoContext.h"
#include "basic/AccountManager.h"
#include "basic/SpreadManager.h"
#include "basic/Convert.h"
#include "basic/StrategyConfig.h"
#include "basic/LarkRebot.h"
#include "basic/WriteFileContent.h"
#include "basic/LimitManager.h"
#include "basic/AlgoPairOrder.h"
#include "basic/AlgoFishingOrder.h"
#include "basic/AlgoRebalanceOrder.h"
#include "basic/AlgoRebalanceOrder.h"
#include "algo/PairTradingContext.h"


std::unordered_map<std::string, int> mAccountNameAccountId;

AlgoContext::AlgoContext() {
    isreal = true;
    rebalanceCount = 0; // rebalanceFlag计数 
    delayCount = 0;
    slippageCount = 0;
    spreadCount = 0;
    stuckOrderReportCount = 0;
    errorOrderReportCount = 0;
    spreadReportCount = 0;
    infoReportCount = 0;
    algoOrderReportCount = 0;
    queryAccountCount = 0;
    fundVerifyCount = 0;    
}

AlgoContext::~AlgoContext() {
}

void AlgoContext::PreStart() {
    QueryAccount();
}

void AlgoContext::Init(sm::SecurityManager* s) {
    smc = s;

    StrategyConfig::GetInstance().LoadConfig();
    
    LarkRebot::GetInstance();
    WriteFileContent::GetInstance();
    AccountManager::Instance().Init(smc);
    LimitManager::Instance().Init();

    curSpreadDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadDelay()) * 1000;
    curSpreadDepthDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadDepthDelay()) * 1000;
    curSpreadTradesDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadTradesDelay()) * 1000;
    lastAlgoUpdateTime = crypto::getCurrentTime();
    tradesDelayThreshold = static_cast<int64_t>(StrategyConfig::GetInstance().GetTradesThreshold()) * 1000;
    onTimerTrade = StrategyConfig::GetInstance().GetOnTimerTrade();
}

void AlgoContext::QueryAccount() {
    auto& mAccountInfo = StrategyConfig::GetInstance().GetAccountInfo();
    for (auto iter = mAccountInfo.begin(); iter != mAccountInfo.end(); ++iter) {
        stra::QuantOrder order;
        order.strategyAccountId = iter->second.accountId;
        order.exchangeType = iter->second.exchangeType;
        for (size_t i = 0; i < iter->second.vInstType.size(); ++i) {
            order.instType = iter->second.vInstType[i];
            QuantTrade::Instance().QueryAccount(order);
        }
    }
}

void AlgoContext::SetTradeClient(om::TradeClient* client) {
    QuantTrade::Instance().SetTradeClient(client);    
}

void AlgoContext::SetDbp(dbp::DbpReader* dbp) {
    QuantDbp::Instance().SetDbp(dbp);  
}


// 注册策略层直接创建好的算法单对象（不再拼 JSON 字符串）
void AlgoContext::SubmitAlgoOrder(BaseAlgoOrder* pAlgoOrder) {
    if (pAlgoOrder == nullptr) {
        LOG_ERROR("SubmitAlgoOrder: pAlgoOrder 为空 -> 算法单创建失败（上游构造返回了空指针）");
        return;
    }

    pAlgoOrder->commandType = stra::CommandType_TRADING;
    pAlgoOrder->algoOrderStatus = stra::ALGO_OS_NEW;
    // 策略层已经分配好 algoOrderId 时沿用（PairInfoManager 用同一个 id 追踪算法单）

    pAlgoOrder->algoOrderId = GenerateStrategyAlgoPairId();
    pAlgoOrder->Init(smc);

    alogOrderManager.InsertAlgoOrderByAlgoOrder(pAlgoOrder);
    LOG_INFO("算法单创建: algoOrderId:{} algoType:{} pairInstrumentKey:{} pairTotalVolume:{} 主动腿:{} 被动腿:{}",
             pAlgoOrder->algoOrderId, stra::AlgoTypeEnum2Str[pAlgoOrder->algoType], pAlgoOrder->pairInstrumentKey,
             pAlgoOrder->pairTotalVolume, pAlgoOrder->activeInstrumentKey, pAlgoOrder->passiveInstrumentKey);

    string pubMsg = pAlgoOrder->GeneratePubStr();
    LarkRebot::GetInstance().SendMsg(pubMsg);
    WriteAlgoOrder(pAlgoOrder);

    bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pAlgoOrder->pairInstrumentKey);
    if (!exist) {
        LOG_INFO("Subscribe pairInstrumentKey:{}", pAlgoOrder->pairInstrumentKey);
        SpreadManager::Instance().AddSpreadPara(pAlgoOrder->pairInstrumentKey);
        QuantDbp::Instance().Subscribe(pAlgoOrder->pairInstrumentKey);
    } else {
        LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", pAlgoOrder->pairInstrumentKey);
    }
}


void AlgoContext::SubmitAlgoOrder(int64_t algoOrderId, stra::CommandType cmd, const stra::AlgoOrderModify* modify) {
    // 变更路径：只操作"已经在册"的算法单。这里刻意不做上面那个重载里的任何一件事
    // （不 new、不 Init、不 InsertAlgoOrderByAlgoOrder、不订阅），否则会把在途子单、
    // 订单管理器、持仓管理器一起丢掉，还会把 mAlgoOrder 里的指针换成另一个对象。
    BaseAlgoOrder* pAlgoOrder = alogOrderManager.SeletAlgoOrderByAlgoOrderId(algoOrderId);
    if (pAlgoOrder == nullptr) {
        LOG_ERROR("SubmitAlgoOrder(change): algo order not found, algoOrderId:{} cmd:{}", algoOrderId, int(cmd));
        return;
    }

    // 已经终结的算法单不再接受任何变更
    if (pAlgoOrder->algoOrderStatus == stra::ALGO_OS_FILLED ||
        pAlgoOrder->algoOrderStatus == stra::ALGO_OS_CANCELED ||
        pAlgoOrder->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED) {
        LOG_INFO("SubmitAlgoOrder(change): skip terminal algo order, algoOrderId:{} status:{}",
                 algoOrderId, stra::AlgoOrderStatusEnum2Str[pAlgoOrder->algoOrderStatus]);
        return;
    }

    if (cmd == stra::CommandType_CANCEL) {
        // 已经在撤单流程里，重复请求幂等返回
        if (pAlgoOrder->algoOrderStatus == stra::ALGO_OS_CANCELLING) {
            return;
        }

        // 只撤"活着"的算法单。ERRORCANCELLING 是 OnTimer 里"行情断 30s"打上的滞留态
        // （那段只改 algoOrderStatus、不撤子单），允许它转入正常撤单流程，
        // 否则这类单子会一直占住对子。
        if (pAlgoOrder->algoOrderStatus != stra::ALGO_OS_NEW &&
            pAlgoOrder->algoOrderStatus != stra::ALGO_OS_PARTFILLED &&
            pAlgoOrder->algoOrderStatus != stra::ALGO_OS_ERRORCANCELLING) {
            LOG_INFO("SubmitAlgoOrder(CANCEL): skip, not cancellable, algoOrderId:{} status:{}",
                     algoOrderId, stra::AlgoOrderStatusEnum2Str[pAlgoOrder->algoOrderStatus]);
            return;
        }

        // 本地发起撤单（对齐祖先 update_order_at_cancel 就地改同一个算法单的状态）。
        // commandType 只用于报表，真正起作用的是 algoOrderStatus。
        pAlgoOrder->commandType = stra::CommandType_UCANCELLING;
        pAlgoOrder->algoOrderStatus = stra::ALGO_OS_CANCELLING;
        pAlgoOrder->cancelOrderTime = crypto::getCurrentTime();
        pAlgoOrder->updateTime = crypto::getCurrentTime();

        LarkRebot::GetInstance().SendMsg(pAlgoOrder->GeneratePubStr());
        WriteAlgoOrder(pAlgoOrder);
        LOG_INFO("SubmitAlgoOrder(CANCEL): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_CANCELLING",
                 algoOrderId, pAlgoOrder->pairInstrumentKey);
        return;
    }

    if (cmd == stra::CommandType_MODIFY) {
        if (modify == nullptr) {
            LOG_ERROR("SubmitAlgoOrder(MODIFY): modify is null, algoOrderId:{}", algoOrderId);
            return;
        }
        // 正在撤单的单子不再改参，避免和撤单流程互相覆盖
        if (pAlgoOrder->algoOrderStatus == stra::ALGO_OS_CANCELLING) {
            LOG_INFO("SubmitAlgoOrder(MODIFY): skip, algo order is cancelling, algoOrderId:{}", algoOrderId);
            return;
        }

        // 整份快照覆盖（祖先 create_modify_dict / create_close_modify_dict 的语义）
        pAlgoOrder->profitSwitch = modify->profitSwitch;
        pAlgoOrder->profitPct = modify->profitPct;

        pAlgoOrder->ttOLSwitch = modify->ttOLSwitch;
        pAlgoOrder->ttOSSwitch = modify->ttOSSwitch;
        pAlgoOrder->ttCLSwitch = modify->ttCLSwitch;
        pAlgoOrder->ttCSSwitch = modify->ttCSSwitch;
        pAlgoOrder->mtOLSwitch = modify->mtOLSwitch;
        pAlgoOrder->mtOSSwitch = modify->mtOSSwitch;
        pAlgoOrder->mtCLSwitch = modify->mtCLSwitch;
        pAlgoOrder->mtCSSwitch = modify->mtCSSwitch;

        pAlgoOrder->ttOLStartSpread = modify->ttOLStartSpread;
        pAlgoOrder->ttOLEndSpread = modify->ttOLEndSpread;
        pAlgoOrder->ttOLStartVolume = modify->ttOLStartVolume;
        pAlgoOrder->ttOLEndVolume = modify->ttOLEndVolume;
        pAlgoOrder->ttCLStartSpread = modify->ttCLStartSpread;
        pAlgoOrder->ttCLEndSpread = modify->ttCLEndSpread;
        pAlgoOrder->ttCLStartVolume = modify->ttCLStartVolume;
        pAlgoOrder->ttCLEndVolume = modify->ttCLEndVolume;
        pAlgoOrder->ttOSStartSpread = modify->ttOSStartSpread;
        pAlgoOrder->ttOSEndSpread = modify->ttOSEndSpread;
        pAlgoOrder->ttOSStartVolume = modify->ttOSStartVolume;
        pAlgoOrder->ttOSEndVolume = modify->ttOSEndVolume;
        pAlgoOrder->ttCSStartSpread = modify->ttCSStartSpread;
        pAlgoOrder->ttCSEndSpread = modify->ttCSEndSpread;
        pAlgoOrder->ttCSStartVolume = modify->ttCSStartVolume;
        pAlgoOrder->ttCSEndVolume = modify->ttCSEndVolume;

        pAlgoOrder->mtOLStartSpread = modify->mtOLStartSpread;
        pAlgoOrder->mtOLEndSpread = modify->mtOLEndSpread;
        pAlgoOrder->mtOLStartVolume = modify->mtOLStartVolume;
        pAlgoOrder->mtOLEndVolume = modify->mtOLEndVolume;
        pAlgoOrder->mtCLStartSpread = modify->mtCLStartSpread;
        pAlgoOrder->mtCLEndSpread = modify->mtCLEndSpread;
        pAlgoOrder->mtCLStartVolume = modify->mtCLStartVolume;
        pAlgoOrder->mtCLEndVolume = modify->mtCLEndVolume;
        pAlgoOrder->mtOSStartSpread = modify->mtOSStartSpread;
        pAlgoOrder->mtOSEndSpread = modify->mtOSEndSpread;
        pAlgoOrder->mtOSStartVolume = modify->mtOSStartVolume;
        pAlgoOrder->mtOSEndVolume = modify->mtOSEndVolume;
        pAlgoOrder->mtCSStartSpread = modify->mtCSStartSpread;
        pAlgoOrder->mtCSEndSpread = modify->mtCSEndSpread;
        pAlgoOrder->mtCSStartVolume = modify->mtCSStartVolume;
        pAlgoOrder->mtCSEndVolume = modify->mtCSEndVolume;

        pAlgoOrder->commandType = stra::CommandType_MODIFIED;
        pAlgoOrder->updateTime = crypto::getCurrentTime();

        LarkRebot::GetInstance().SendMsg(pAlgoOrder->GeneratePubStr());
        WriteAlgoOrder(pAlgoOrder);
        LOG_INFO("SubmitAlgoOrder(MODIFY): algoOrderId:{} pairInstrumentKey:{} profitSwitch:{} profitPct:{}",
                 algoOrderId, pAlgoOrder->pairInstrumentKey, pAlgoOrder->profitSwitch, pAlgoOrder->profitPct);
        return;
    }

    LOG_ERROR("SubmitAlgoOrder(change): unsupported cmd:{}, algoOrderId:{}", int(cmd), algoOrderId);
}


void AlgoContext::OnCommand(string s) {
    // 原 JSON 建单路径已由 AlgoContext::SubmitAlgoOrder(BaseAlgoOrder*) 取代。
    // 下面这段硬编码的 DOGE 测试单先注释保留，确认新链路跑通后可直接删除。
    /*
    BaseAlgoOrder* pAlgoOrder = new AlgoPairOrder();
    pAlgoOrder->algoOrderId = GenerateStrategyAlgoPairId();
    pAlgoOrder->commandType = stra::CommandType_NEW;
    pAlgoOrder->insertTime = crypto::getCurrentTime();
    strncpy(pAlgoOrder->algoStrategyName, "cc_test1", stra::NAME_LEN);
    strncpy(pAlgoOrder->pairInstrumentKey, "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT", stra::INST_KEY_LEN);
    strncpy(pAlgoOrder->baseAsset, "USDT", stra::ASSET_LEN);

    strncpy(pAlgoOrder->activeInstrumentKey, "BINANCE.USDT_SWAP.DOGE-USDT", stra::INST_KEY_LEN);
    pAlgoOrder->activePriceTakerPct = 0.0;
    pAlgoOrder->activePriceMakerPct = 0.0;
    pAlgoOrder->activeAccountId = 10000;
    pAlgoOrder->activeDriveType = stra::DriveType_ACTIVE;
    pAlgoOrder->activeDepthMakerCheck = false;
    pAlgoOrder->activeDepthTakerCheck = false;
    pAlgoOrder->activeOrderType = OT_LIMIT;
    
    strncpy(pAlgoOrder->passiveInstrumentKey, "GATEIO.USDT_SWAP.DOGE-USDT", stra::INST_KEY_LEN);
    pAlgoOrder->passivePriceTakerPct = 0.0;
    pAlgoOrder->passivePriceMakerPct = 0.0;
    pAlgoOrder->passiveAccountId = 10001;
    pAlgoOrder->passiveDriveType = stra::DriveType_PASSIVE;
    pAlgoOrder->passiveDepthMakerCheck = false;
    pAlgoOrder->passiveDepthTakerCheck = false;
    pAlgoOrder->passiveOrderType = OT_LIMIT;
    
    pAlgoOrder->passiveVolumePct = 0.5;
    pAlgoOrder->activeMakerCancelOrderTime = 5000000LL;
    pAlgoOrder->activeTakerCancelOrderTime = 5000000LL;
    pAlgoOrder->passiveMakerCancelOrderTime = 5000000LL;
    pAlgoOrder->passiveTakerCancelOrderTime = 5000000LL;
    
    pAlgoOrder->activePassiveCancelOrderPct = 0.001;
    pAlgoOrder->activeMakerCancelOrderPct = 0.001;
    pAlgoOrder->activeTakerCancelOrderPct = 0.001;
    pAlgoOrder->passiveMakerCancelOrderPct = 0.001;
    pAlgoOrder->passiveTakerCancelOrderPct = 0.001;
    
    pAlgoOrder->activeMakerFeeRate = 0.0;
    pAlgoOrder->activeTakerFeeRate = 0.0;
    pAlgoOrder->passiveMakerFeeRate = 0.0;
    pAlgoOrder->passiveTakerFeeRate = 0.0;
    pAlgoOrder->activeTakerSlippage = 0.0;
    pAlgoOrder->activeMakerSlippage = 0.0;
    pAlgoOrder->passiveTakerSlippage = 0.0;
    pAlgoOrder->passiveMakerSlippage = 0.0;
    
    pAlgoOrder->pairActiveTotalPrice = 0.100335;
    pAlgoOrder->pairTotalVolume = 1000;
    pAlgoOrder->pairPassiveTotalVolume = -1000;
    pAlgoOrder->pairPassiveTotalPrice = 0.10027;
    
    pAlgoOrder->maxMTOrderSize = 5;
    pAlgoOrder->maxTTOrderSize = 5;
    pAlgoOrder->targetSpreadType = stra::TargetSpredPrice_NOW;
    pAlgoOrder->activeVolumeCalcualteType = stra::ActiveVolumeCalcualteType_PassiveVolumePct;
    
    pAlgoOrder->ttTargetVolume = 100;
    pAlgoOrder->mtTargetVolume = 100;
    pAlgoOrder->minVolume = 40;
    pAlgoOrder->profitSwitch = false;
    pAlgoOrder->profitPct = 0.1;

    pAlgoOrder->ttOLStartSpread = -0.0001;
    pAlgoOrder->ttOLEndSpread = -0.0005;
    pAlgoOrder->ttOLStartVolume = 0.0;
    pAlgoOrder->ttOLEndVolume = -1000;
    pAlgoOrder->ttOLSwitch = false;
      
    pAlgoOrder->ttCLStartSpread = 0;
    pAlgoOrder->ttCLEndSpread = 0.001;
    pAlgoOrder->ttCLStartVolume = -1000;
    pAlgoOrder->ttCLEndVolume = 0;
    pAlgoOrder->ttCLSwitch = false;
    
    pAlgoOrder->ttOSStartSpread = 0;
    pAlgoOrder->ttOSEndSpread = 0.000001;
    pAlgoOrder->ttOSStartVolume = 0;
    pAlgoOrder->ttOSEndVolume = 1000;
    pAlgoOrder->ttOSSwitch = false;
    
    pAlgoOrder->ttCSStartSpread = 0.0011;
    pAlgoOrder->ttCSEndSpread = 0.001;
    pAlgoOrder->ttCSStartVolume = 1000;
    pAlgoOrder->ttCSEndVolume = 0;
    pAlgoOrder->ttCSSwitch = true;
      
    pAlgoOrder->mtOLStartSpread = -0.0001;
    pAlgoOrder->mtOLEndSpread = -0.0005;
    pAlgoOrder->mtOLStartVolume = 0.0;
    pAlgoOrder->mtOLEndVolume = -1000;
    pAlgoOrder->mtOLSwitch = false;
     
    pAlgoOrder->mtCLStartSpread = 0;
    pAlgoOrder->mtCLEndSpread = 0.001;
    pAlgoOrder->mtCLStartVolume = -1000;
    pAlgoOrder->mtCLEndVolume = 0;
    pAlgoOrder->mtCLSwitch = false;
      
    pAlgoOrder->mtOSStartSpread = 0;
    pAlgoOrder->mtOSEndSpread = 0.001;
    pAlgoOrder->mtOSStartVolume = 0;
    pAlgoOrder->mtOSEndVolume = 1000;
    pAlgoOrder->mtOSSwitch = false;
     
    pAlgoOrder->mtCSStartSpread = -0.0001;
    pAlgoOrder->mtCSEndSpread = -0.0005;
    pAlgoOrder->mtCSStartVolume = 1000;
    pAlgoOrder->mtCSEndVolume = 0;
    pAlgoOrder->mtCSSwitch = false;

    pAlgoOrder->mtRebalanceSwitch = true;
    pAlgoOrder->ttRebalanceSwitch = true;
    pAlgoOrder->mtRebalanceFlag = true;
    pAlgoOrder->ttRebalanceFlag = true;
    pAlgoOrder->mtPriceTrendProtectFlag = false;
    pAlgoOrder->ttPriceTrendProtectFlag = false;
    pAlgoOrder->activePriceTickFlag = false;
    pAlgoOrder->activePriceTickNum = 0;
    pAlgoOrder->passivePriceTickFlag = false;
    pAlgoOrder->passivePriceTickNum = 0;
    pAlgoOrder->isManual = false;
    pAlgoOrder->takerTakerFs = pAlgoOrder->activeTakerFeeRate + pAlgoOrder->passiveTakerFeeRate + pAlgoOrder->activeTakerSlippage + pAlgoOrder->passiveTakerSlippage;
    pAlgoOrder->makerTakerFs = pAlgoOrder->activeMakerFeeRate + pAlgoOrder->passiveTakerFeeRate + pAlgoOrder->activeMakerSlippage + pAlgoOrder->passiveTakerSlippage;
    

    AlgoPairOrder* pPairOrder = (AlgoPairOrder*)pAlgoOrder;
    pPairOrder->commandType = stra::CommandType_TRADING;
    pPairOrder->algoOrderStatus = stra::ALGO_OS_NEW;
    pPairOrder->algoType = stra::AlgoType_PairTrading;
    pPairOrder->Init(smc);
    alogOrderManager.InsertAlgoOrderByAlgoOrder(pPairOrder);

    // string pubMsg = pPairOrder->GeneratePubStr();
    // LarkRebot::GetInstance().SendMsg(pubMsg);


    WriteAlgoOrder(pPairOrder);

    bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pPairOrder->pairInstrumentKey);
    if (!exist) {
        LOG_INFO("Subscribe pairInstrumentKey:{}", pPairOrder->pairInstrumentKey);
        SpreadManager::Instance().AddSpreadPara(pPairOrder->pairInstrumentKey);
        QuantDbp::Instance().Subscribe(pPairOrder->pairInstrumentKey);
    } else {
        LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", pPairOrder->pairInstrumentKey);
    } 
    */
    
}

void AlgoContext::OnMarketDepth() {

}

void AlgoContext::OnMarketTrade() {

}

void AlgoContext::OnSpread(const dbp::DbpTopic* topic, const dbp::DbpData* pdata) {
    try {
        SpreadManager::Instance().OnMarketSpread(topic, pdata);
        const Bbo& activeBbo = SpreadManager::Instance().GetBbo(topic->activeInstrumentKey);
        const Bbo& passiveBbo = SpreadManager::Instance().GetBbo(topic->passiveInstrumentKey);

        int64_t nowTime = crypto::getCurrentTime();
        bool openOrderFlag = false;
        bool curDelay = false;
        bool curDepthDelay = false;
        bool curTradeDelay = false;

        int64_t timeVal = nowTime - pdata->generateTs;
        if (timeVal < curSpreadDelay) {
            curDelay = true;
        } else {
            LOG_INFO("OnSpread delay > {} ---  nowTime:{}  generateTs:{}  now - genetateTs: {} pairInstrumentKey:{}", curSpreadDelay, nowTime, pdata->generateTs, timeVal, topic->__name);
        }

        int64_t timeDepthVal = nowTime - min(pdata->activeDepthTs, pdata->passiveDepthTs);
        if (timeDepthVal < curSpreadDepthDelay) {
            curDepthDelay = true;
        } else {
            LOG_INFO("OnSpread depth delay > {} ---  nowTime:{}  ativeDepthTs:{}  passiveDepthTs:{} timeDepthVal:{} pairInstrumentKey:{}", curSpreadDepthDelay, nowTime, pdata->activeDepthTs, pdata->passiveDepthTs, timeDepthVal, topic->__name);
        }

        if (pdata->exchActiveTradeDelay < tradesDelayThreshold && pdata->exchPassiveTradeDelay < tradesDelayThreshold) {
            curTradeDelay = true;
        }
        else if (timeDepthVal > curSpreadDepthDelay) {
            curTradeDelay = true;
        } else {
            LOG_INFO("OnSpread depth tradesdelay exchActiveTradeDelay:{} exchPassiveTradeDelay:{} tradesDelayThreshold:{} pairInstrumentKey:{}", pdata->exchActiveTradeDelay, pdata->exchPassiveTradeDelay, tradesDelayThreshold, topic->__name);
        }

        openOrderFlag = curDelay && curDepthDelay && curTradeDelay;


        auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
        std::cout << "AlgoContext allAlgoOrders size: " << allAlgoOrders.size() << std::endl;


        for (auto it = allAlgoOrders.begin(); it != allAlgoOrders.end(); ++it) {
            BaseAlgoOrder* pAlgoOrder = it->second;

            if (strcmp(pAlgoOrder->pairInstrumentKey, topic->__name) == 0) {
                // 已经终结的算法单不再处理任何行情逻辑（OnTimer 会留一个宽限期给策略层读取终结状态，
                // 宽限期内这些单子还在容器里，不能再去撤它已经成交/已撤掉的子单）
                if (pAlgoOrder->algoOrderStatus == stra::ALGO_OS_FILLED ||
                    pAlgoOrder->algoOrderStatus == stra::ALGO_OS_CANCELED ||
                    pAlgoOrder->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED) {
                    continue;
                }

                pAlgoOrder->CancelOrderOnSpread(pdata); // 执行撤单逻辑

                // 暂时注释
                // pAlgoOrder->posMgrMakerTaker.UpdateAccountOnMarketDepth(activeBbo);  // 更新floatAmount
                // pAlgoOrder->posMgrTakerTaker.UpdateAccountOnMarketDepth(activeBbo);  // 更新floatAmount
                // pAlgoOrder->posMgrMakerTaker.UpdateAccountOnMarketDepth(passiveBbo);
                // pAlgoOrder->posMgrTakerTaker.UpdateAccountOnMarketDepth(passiveBbo);

                if (pAlgoOrder->algoOrderStatus != stra::ALGO_OS_NEW && pAlgoOrder->algoOrderStatus != stra::ALGO_OS_PARTFILLED) { // algoOrderStatus;  // New Cancelling Canceled Filled Fault
                    continue;
                }

                if (pAlgoOrder->algoType == stra::AlgoType_PairTrading && openOrderFlag) {  // 执行拆单报单逻辑
                    // 配对单Onspread逻辑
                    // 如果订单与spread相关
                    // 检查行情有效性
                    if (pdata->spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
                        // 行情有效支持开仓
                        if (!pAlgoOrder->mtSlipageFlag && !pAlgoOrder->mtSpreadFlag) {
                            // MakerTaker延迟与滑点
                            if (pAlgoOrder->mtRebalanceFlag) {
                                // 再平衡模式一种类型的订单只能有一个pairOrder
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER) == 0) {
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {  
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    LarkRebot::GetInstance().SendMsg(msg);
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // 非再平衡模式一种类型的订单可以有多个订单
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER) < pAlgoOrder->maxMTOrderSize){
                                    auto& allPairOrders = pAlgoOrder->pairOrderMgr.GetAllPairOrders();
                                    bool createFlag = true;
                                    for (auto iterPair = allPairOrders.begin(); iterPair != allPairOrders.end(); ++iterPair) {
                                        for (auto iterActive = iterPair->second.sActiveOrder.begin(); iterActive != iterPair->second.sActiveOrder.end(); ++iterActive) {
                                            int64_t strategyOrderId = *iterActive;
                                            stra::QuantOrder quantOrder = pAlgoOrder->orderMgr.SelectOrderByStrategyOrderId(strategyOrderId);
                                            if (quantOrder.orderStatus == OS_PEND || quantOrder.orderStatus == OS_PENDING_NEW || quantOrder.orderStatus == OS_NEW || quantOrder.orderStatus == OS_PARTFILLED) {
                                                createFlag = false;
                                                break;
                                            }
                                        }
                                        if (!createFlag) {
                                            break;
                                        }
                                    }
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    if (createFlag) {
                                        bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                        if (pass) {
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!pAlgoOrder->ttSlipageFlag && !pAlgoOrder->ttSpreadFlag) {
                            if (pAlgoOrder->ttRebalanceFlag) {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER) == 0) {
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    LarkRebot::GetInstance().SendMsg(msg);
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER) < pAlgoOrder->maxTTOrderSize){
                                    auto& allPairOrders = pAlgoOrder->pairOrderMgr.GetAllPairOrders();
                                    bool createFlag = true;
                                    for (auto iterPair = allPairOrders.begin(); iterPair != allPairOrders.end(); ++iterPair) {
                                        for (auto iterActive = iterPair->second.sActiveOrder.begin(); iterActive != iterPair->second.sActiveOrder.end(); ++iterActive) {
                                            int64_t strategyOrderId = *iterActive;
                                            stra::QuantOrder quantOrder = pAlgoOrder->orderMgr.SelectOrderByStrategyOrderId(strategyOrderId);
                                            if (quantOrder.orderStatus == OS_PEND || quantOrder.orderStatus == OS_PENDING_NEW || quantOrder.orderStatus == OS_NEW || quantOrder.orderStatus == OS_PARTFILLED) {
                                                createFlag = false;
                                                break;
                                            }
                                        }

                                        if (!createFlag) {
                                            break;
                                        }
                                    }
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    if (createFlag) {
                                        bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                        if (pass) {
                                             LOG_INFO("ttRebalanceFlag false create new pair order");
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } 
                } else if (pAlgoOrder->algoType == stra::AlgoType_FishingTrading && openOrderFlag) {  // 执行拆单报单逻辑
                // 钓鱼单逻辑
                    if (pdata->spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
                        // 行情有效支持开仓
                        if (!pAlgoOrder->mtSlipageFlag && !pAlgoOrder->mtSpreadFlag) {
                            // MakerTaker延迟与滑点
                            if (pAlgoOrder->mtRebalanceFlag) {
                                // 再平衡模式一种类型的订单只能有一个pairOrder
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER) == 0) {
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        if (pAlgoOrder->pairTotalVolume < 0){
                                            // 再平衡模式根据当前持仓选择方向
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, DT_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {  
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        } else{
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, DT_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {  
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // 非再平衡模式一种类型的订单可以有多个订单, 后续可以通过挂单数调整挂价距离
                                bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                if (pass) {
                                    if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, DT_LONG) < pAlgoOrder->maxMTOrderSize){
                                        // 主动腿多头报单
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, DT_LONG);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);                                               
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    LarkRebot::GetInstance().SendMsg(msg);
                                                }
                                            }
                                        }
                                    }
                                    if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, DT_SHORT) < pAlgoOrder->maxMTOrderSize){
                                        // 主动腿空头报单
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, DT_SHORT);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    LarkRebot::GetInstance().SendMsg(msg);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!pAlgoOrder->ttSlipageFlag && !pAlgoOrder->ttSpreadFlag) {
                            if (pAlgoOrder->ttRebalanceFlag) {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER) == 0) {
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        // 再平衡模式根据当前持仓选择方向
                                        if (pAlgoOrder->pairTotalVolume < 0){
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, DT_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        } else {
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, DT_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER) < pAlgoOrder->maxTTOrderSize){
                                    // 如果进入再平衡模式，需要无配对单才可以报单
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, DT_LONG) < pAlgoOrder->maxMTOrderSize){
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, DT_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                        if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::TAKER_TAKER, DT_SHORT) < pAlgoOrder->maxMTOrderSize){
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, DT_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, pdata);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    md::InstrumentInfo info;
                                                    smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, pdata);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        LarkRebot::GetInstance().SendMsg(msg);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } 
                }
                else if (pAlgoOrder->algoType == stra::AlgoType_Rebalance && openOrderFlag) {
                    if (pdata->spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
                        if (!pAlgoOrder->mtSlipageFlag && !pAlgoOrder->mtSpreadFlag) {
                            if (pAlgoOrder->mtRebalanceFlag) {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER) == 0) {
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();

                                            stra::QuantOrder quant_order;
                                            AlgoRebalanceOrder* pRebalance = (AlgoRebalanceOrder*)pAlgoOrder;
                                            if (pRebalance->activeTrade == 1) {
                                                LOG_INFO("AlgoType_Rebalance MAKER_TAKER start create active order, pairId: {}", pairOrder.pairId);
                                                quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            }
                                            else {
                                                LOG_INFO("AlgoType_Rebalance MAKER_TAKER start create passive order, pairId: {}", pairOrder.pairId);
                                                quant_order = pairOrder.CreateVolumePassiveOrder(strategyOrderId);    
                                            }

                                            if (quant_order.strategyOrderId > 0) {
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);   
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                }
                                                else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);      
                                                    LarkRebot::GetInstance().SendMsg(msg);  
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }


                        if (!pAlgoOrder->ttSlipageFlag && !pAlgoOrder->ttSpreadFlag) {
                            if (pAlgoOrder->ttRebalanceFlag) {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::TAKER_TAKER) == 0) {
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, pdata);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();

                                            stra::QuantOrder quant_order;
                                            AlgoRebalanceOrder* pRebalance = (AlgoRebalanceOrder*)pAlgoOrder;
                                            if (pRebalance->activeTrade == 1) {
                                                LOG_INFO("AlgoType_Rebalance TAKER_TAKER start create active order, pairId: {}", pairOrder.pairId);
                                                quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            }
                                            else {
                                                LOG_INFO("AlgoType_Rebalance TAKER_TAKER start create passive order, pairId: {}", pairOrder.pairId);
                                                quant_order = pairOrder.CreateVolumePassiveOrder(strategyOrderId);    
                                            }

                                            if (quant_order.strategyOrderId > 0) {
                                                md::InstrumentInfo info;
                                                smc->get_instrument_info(quant_order.exchangeType, quant_order.instType, quant_order.instrument, info);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, info);
                                                if (verify) {
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, pdata);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                }
                                                else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);  
                                                    LarkRebot::GetInstance().SendMsg(msg);  
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                    }
                }
            }
        }
    } catch(StraException& e) {
        LOG_INFO("StraException, in AlgoContext::OnSpread error msg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException, in AlgoContext::OnSpread error msg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnSpread, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnSpread, errormsg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    }
}

void AlgoContext::OnOrder(const pubsub::OrderResponse& orderResponse) {
    try {
        int64_t nowTime = crypto::getCurrentTime();
  
        int64_t algoId = 0;
        int64_t pairId = 0;
        std::vector<std::string> v;
        splitString(orderResponse.strategyRef, v, "_");
        if (v.size() >= 2) {
            algoId = stoll(v[0]);
            pairId = stoll(v[1]);
        }
        else {
            LOG_WARN("OnOrder: strategyRef 解析失败，定位不到算法单/对子 -> 丢弃该回报 strategyRef:{}", orderResponse.strategyRef);
            return;
        }

        BaseAlgoOrder* pAlgoOrder = alogOrderManager.SeletAlgoOrderByAlgoOrderId(algoId);
        if (pAlgoOrder != nullptr) {
            if (pAlgoOrder->algoType == stra::AlgoType_PairTrading || pAlgoOrder->algoType == stra::AlgoType_FishingTrading || pAlgoOrder->algoType == stra::AlgoType_Rebalance) {
                // 是配对单进行配对单的处理
                PairOrder& pairOrder = pAlgoOrder->pairOrderMgr.SelectPairOrderByPairId(pairId);
                if (pairOrder.pairId <= 0) {
                    LOG_WARN("OnOrder: 回报找不到 pairOrder -> 丢弃 algoOrderId:{} pairId:{} clientOrderId:{}",
                             pAlgoOrder->algoOrderId, pairId, orderResponse.clientOrderId);
                    return;
                }
                // 更新od_mgr与quant_order

                dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(pAlgoOrder->pairInstrumentKey);
                stra::QuantOrder quantOrder = pAlgoOrder->orderMgr.SelectOrderByStrategyOrderId(orderResponse.clientOrderId);

                if (quantOrder.totalVolumeOnOrder - orderResponse.volumeTraded > stra::MIN_FLOAT) {  // 过滤乱序的报单
                    return;
                }

                // 订单状态适配 待确定
                // if (order.apiSource == AS_CANCEL_ORDER && order.orderStatus == OS_REJECTED {
                //     if (order.exchangType == BYBIT || order.exchangType == BITGET) {
                //         order.orderStatus = OS_REJECTED;
                //     }
                //     else {
                //         order.orderStatus = OS_FAILED;
                //     }
                // } else if (order.apiSource == AS_QUERY_ORDER && order.orderStatus == OS_REJECTED && strlen(quantOrder.exchangeOrderId) > 0){
                //     if (order.exchangType == BYBIT || order.exchangType == BITGET) {
                //         order.orderStatus = OS_REJECTED;
                //     }
                //     else {
                //         order.orderStatus = OS_UNKNOWN;
                //     }
                // }


                // 订单手续费适配 是否可以去掉？
                // stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(quantOrder.instrumentKey);
                // double temp_fee_rate = 0.0;
                // if (quantOrder.isActiveOrder){
                //     // if (order.isMaker)
                //     if (quantOrder.orderType == OT_POST_ONLY) {
                //         temp_fee_rate = pAlgoOrder->activeMakerFeeRate;
                //     } else {
                //         temp_fee_rate = pAlgoOrder->activeTakerFeeRate;
                //     }
                // } else {
                //     // if (order.isMaker){
                //     if (quantOrder.orderType == OT_POST_ONLY) {
                //         temp_fee_rate = pAlgoOrder->passiveMakerFeeRate;
                //     } else {
                //         temp_fee_rate = pAlgoOrder->passiveTakerFeeRate;
                //     }
                // }


                // 是否需要计算本次成交价
                // if (orderResponse.volumeTraded - quantOrder.totalVolumeOnOrder > stra::MIN_FLOAT){
                //     if (quantOrder.instType == SPOT || quantOrder.instType == MARGIN){
                //         if (quantOrder.direction == DT_LONG){
                //             order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder * order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder * quantOrder.totalPriceOnOrder) / (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder);
                //             strncpy(order.lastExecutedTradeFeeCurrency, info.instLeft.c_str(), stra::ASSET_LEN);
                //             order.lastExecutedTradeFee = order.lastExecutedVolumeOnOrder * temp_fee_rate;
                //         }else{
                //             order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder) / (order.totalVolumeOnOrder / order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder / quantOrder.totalPriceOnOrder);
                //             strncpy(order.lastExecutedTradeFeeCurrency, info.instRight.c_str(), stra::ASSET_LEN);
                //             order.lastExecutedTradeFee = order.lastExecutedVolumeOnOrder * order.lastExecutedPriceOnOrder * temp_fee_rate;
                //         }
                //     }else{
                //         if (info.calculateType == 0){
                //             order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder * order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder * quantOrder.totalPriceOnOrder) / (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder);
                //             strncpy(order.lastExecutedTradeFeeCurrency, info.margin.c_str(), stra::ASSET_LEN);
                //             order.lastExecutedTradeFee = info.multiple * order.lastExecutedVolumeOnOrder * order.lastExecutedPriceOnOrder * temp_fee_rate;
                //         }else{
                //             order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder) / (order.totalVolumeOnOrder / order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder / quantOrder.totalPriceOnOrder);
                //             strncpy(order.lastExecutedTradeFeeCurrency, info.margin.c_str(), stra::ASSET_LEN);
                //             order.lastExecutedTradeFee = info.multiple * order.lastExecutedVolumeOnOrder / order.lastExecutedPriceOnOrder * temp_fee_rate;
                //         }
                //     }
                // }


                // 进行延迟计算与检查
                //LOG_INFO("OnOrder check orderstatus PEND_NEW CANCEL!");
                if (quantOrder.orderStatus == OS_PEND || quantOrder.orderStatus == OS_CANCEL) {
                    pAlgoOrder->systemDelayTimeSpan = 0.8 * pAlgoOrder->systemDelayTimeSpan + 0.2 * (nowTime - quantOrder.updateTime);
                    // if (pAlgoOrder->systemDelayTimeSpan > 20000) {
                    if (pAlgoOrder->systemDelayTimeSpan > 50000) {
                        pAlgoOrder->systemDelayFlag = true;
                        // 进行异常播报
                        char msg[stra::MSG_LEN];
                        sprintf(msg, "strategyName:%s algoOrderId:%ld quantOrder:%ld  systemDelayFlag:%d  systemDelayTimeSpan:%ld", pAlgoOrder->algoStrategyName, pAlgoOrder->algoOrderId, quantOrder.strategyOrderId, pAlgoOrder->systemDelayFlag, pAlgoOrder->systemDelayTimeSpan);
                        LarkRebot::GetInstance().SendMsg(string(msg));
                    }
                }

                //LOG_INFO("OnOrder check orderstatus PENDING_NEW CANCELLING!");
                if (quantOrder.orderStatus == OS_PENDING_NEW || quantOrder.orderStatus == OS_CANCELLING) {
                    pAlgoOrder->exchangeDelayTimeSpan = 0.8 * pAlgoOrder->exchangeDelayTimeSpan + 0.2 * (nowTime - quantOrder.updateTime);
                    int64_t scd10 = 10000000LL; // 10s
                    if (pAlgoOrder->exchangeDelayTimeSpan > scd10){
                        pAlgoOrder->exchangeDelayFlag = true;
                        // 进行异常播报
                        char msg[stra::MSG_LEN];
                        sprintf(msg, "strategyName:%s algoOrderId:%ld quantOrder:%ld exchangeDelayFlag:%d  exchangeDelayTimeSpan:%ld", pAlgoOrder->algoStrategyName, pAlgoOrder->algoOrderId, quantOrder.strategyOrderId, pAlgoOrder->exchangeDelayFlag, pAlgoOrder->exchangeDelayTimeSpan);
                        LarkRebot::GetInstance().SendMsg(string(msg));
                    }
                }

                // 开始更新订单
                if (orderResponse.apiSourceEnum == AS_QUERY_ORDER) {
                    quantOrder = pAlgoOrder->orderMgr.UpdateOrderOnQueryOrder(orderResponse);
                } else {
                    quantOrder = pAlgoOrder->orderMgr.UpdateOrderOnOrder(orderResponse);
                }

                //LOG_INFO("OnOrder write quant order!");
                if (quantOrder.strategyOrderId > 0) {
                    WriteQuantOrder(quantOrder, pdata); // 行情数据写入
                }
                
                //LOG_INFO("OnOrder start update algoPairOrderByQuantOrder!");
                // 更新algo_order的ps_mgr与pair_order
                pAlgoOrder->UpdateAlgoPairOrderByQuantOrder(quantOrder);
                if (quantOrder.orderStatus == OS_FILLED|| quantOrder.orderStatus == OS_REJECTED || quantOrder.orderStatus == OS_CANCELED) {
                    // 订单终结。报单被拒是"为什么没成交"的直接原因，单独提级。
                    if (quantOrder.orderStatus == OS_REJECTED) {
                        LOG_ERROR("报单被拒: algoOrderId:{} pairId:{} instrumentKey:{} strategyOrderId:{} 主动腿:{}",
                                  pAlgoOrder->algoOrderId, pairOrder.pairId, quantOrder.instrumentKey,
                                  quantOrder.strategyOrderId, quantOrder.isActiveOrder);
                    } else {
                        LOG_INFO("订单终结: algoOrderId:{} pairId:{} instrumentKey:{} status:{} strategyOrderId:{} 主动腿:{}",
                                 pAlgoOrder->algoOrderId, pairOrder.pairId, quantOrder.instrumentKey,
                                 OrderStatusEnum2StrMap[quantOrder.orderStatus], quantOrder.strategyOrderId,
                                 quantOrder.isActiveOrder);
                    }
                    // 订单完结解冻
                    //LOG_INFO("OnOrder start update algoPairOrderByDeleteQuantOrder!");
                    pAlgoOrder->UpdateAlgoPairOrderByDeleteQuantOrder(quantOrder);
                    //LOG_INFO("OnOrder start orderStatus filled rejected canceled PairOrderTrade!");
                    pAlgoOrder->PairOrderTrade(pairOrder);

                    // algoOrder保存
                    // pAlgoOrder->SaveToFile();
                    // WriteAlgoOrder(algoOrder);
                } else {
                    // 主动腿未完结有成交则需要进行被动腿报单
                    //LOG_INFO("OnOrder start orderStatus not in filled rejected canceled PairOrderTrade!");
                    if (quantOrder.isActiveOrder && quantOrder.tradeVolume > stra::MIN_FLOAT){
                        pAlgoOrder->PairOrderTrade(pairOrder);
                    }
                }
                //LOG_INFO("OnOrder Update end!");
                if (quantOrder.isActiveOrder) {
                    if (quantOrder.orderStatus == OS_NEW || quantOrder.orderStatus == OS_PARTFILLED) {
                        pAlgoOrder->CancelOrderOnSpread(pdata); // 执行撤单逻辑
                    }
                }
            }
        } else {
            LOG_WARN("OnOrder: 回报找不到算法单（可能已终结并删除）-> 丢弃 algoOrderId:{} strategyRef:{}",
                     algoId, orderResponse.strategyRef);
        }
    } catch(StraException& e) {
        LOG_INFO("StraException in AlgoContext::OnOrder, error msg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException in AlgoContext::OnOrder, error msg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnOrder, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnOrder, errormsg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    }
}

void AlgoContext::OnKline() {

}

void AlgoContext::OnFundingRate() {

}

void AlgoContext::OnTimer(int64_t eventTime) {
    // 延迟检查，订单从发出到回报的延迟时间作为一个变量存起来，超过标准需要报警
    // 杠杆检查，accountMgr杠杆过高检查，超过标准需要报警。未来在极端情况下强制进行自动减仓
    // 订单异常检查
    try {
        double orderAmount = 0.0;
        int64_t second1 = 1000 * 1000;
        rebalanceCount++;
        delayCount++;
        slippageCount++;
        spreadCount++;
        stuckOrderReportCount++;
        errorOrderReportCount++;
        spreadReportCount++;
        infoReportCount++;
        algoOrderReportCount++;
        queryAccountCount++;
        fundVerifyCount++;

        for (auto iter = mSpreadReportCount.begin(); iter != mSpreadReportCount.end(); ++iter) {
            iter->second += 1;
        }
  
        bool stuckOrderFlag = false;
        bool errorOrderFlag = false;
        vector<string> vUnSubscribePairInstId;
        string algoOrderStr = "";
        auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
        for (auto it = allAlgoOrders.begin(); it != allAlgoOrders.end();) {
            bool deleteAlgoOrderFlag = false;

            // 已经终结的算法单先留在容器里一个宽限期再回收。
            // 终结回传已由本函数各终态分支同线程直接调用（NotifyAlgoOrderUpdate），
            // 不再依赖策略层轮询，所以宽限期现在只用于兜底与状态核对。
            bool algoOrderTerminal = (it->second->algoOrderStatus == stra::ALGO_OS_FILLED ||
                                      it->second->algoOrderStatus == stra::ALGO_OS_CANCELED ||
                                      it->second->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED);
            if (algoOrderTerminal) {
                if (eventTime - it->second->updateTime > 5 * second1) {
                    vUnSubscribePairInstId.push_back(it->second->pairInstrumentKey);
                    delete it->second;
                    it->second = nullptr;
                    allAlgoOrders.erase(it++);
                } else {
                    it++;
                }
                continue;
            }

            auto& orderMgr = it->second->orderMgr;
            auto& allOrders = orderMgr.GetAllOrders();

            if (eventTime - lastAlgoUpdateTime > 10 * 60 * second1) {
                it->second->Update();
            }

            // 定时播报algoOrder信息
            char algoOrderMsg[stra::MSG_LEN];
            sprintf(algoOrderMsg, "algoOrder --- strategyName:%s algoOrderId:%ld pairInstrumentKey:%s", it->second->algoStrategyName, it->second->algoOrderId, it->second->pairInstrumentKey);
            algoOrderStr += string(algoOrderMsg) + "\n";
            
            
            // 触发被动腿报单
            auto& pairOrderMgr = it->second->pairOrderMgr;
            unordered_map<int64_t, PairOrder> allPairOrders = pairOrderMgr.GetAllPairOrders();
            for (auto ia = allPairOrders.begin(); ia != allPairOrders.end(); ++ia) {
                it->second->PairOrderTrade(ia->second);
            }
            
            // 行情检查
            dbp::DbpData* pdata = SpreadManager::Instance().GetSpread(it->second->pairInstrumentKey);
            if (eventTime - pdata->generateTs > 30 * second1 && mSpreadReportCount[it->second->pairInstrumentKey] > 60) {
                char msg[stra::MSG_LEN];
                sprintf(msg, "check spread data !!! strategyName:%s algoOrderId:%ld pairInstrumentKey:%s  eventTime:%ld  pdata->generateTs:%ld", it->second->algoStrategyName, it->second->algoOrderId, it->second->pairInstrumentKey, eventTime, pdata->generateTs);
                LarkRebot::GetInstance().SendMsg(msg);
                LOG_INFO("Spread {}", msg);
                it->second->commandType = stra::CommandType_ERROR;
                it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELLING;
                it->second->updateTime = eventTime;
            }

            // 异常订单检查
            for (auto iu = allOrders.begin(); iu != allOrders.end(); ++iu) {
                auto& order = iu->second;
                if (order.orderStatus == OS_CANCEL || order.orderStatus == OS_CANCELLING || order.orderStatus == OS_PEND || order.orderStatus == OS_PENDING_NEW) {
                    if (eventTime - order.updateTime > second1 * 5) {
                        // LOG_INFO("lark alarm! quantOrder: %s", order.GetStr().c_str());
                        // 异步lark报警
                        if (stuckOrderReportCount > 60){
                            char msg[stra::MSG_LEN];
                            sprintf(msg, "check stuck order !!! strategyName:%s algoOrderId:%ld strategyOrderId:%ld  instrumentKey:%s orderStatus:%s posDirection:%s direction:%s", it->second->algoStrategyName, it->second->algoOrderId, order.strategyOrderId, order.instrumentKey, OrderStatusEnum2StrMap[order.orderStatus].c_str(), OffsetFlagEnum2StrMap[order.offsetFlag].c_str(), DirectionEnum2StrMap[order.direction].c_str());
                            LarkRebot::GetInstance().SendMsg(msg);
                            stuckOrderFlag = true;
                        }

                        // 主动发起订单查询
                        bool pass = LimitManager::Instance().PassLimit(order.strategyAccountId);
                        if (pass) {
                            QuantTrade::Instance().QueryOrder(order);
                            // 更新updatetime
                            order.updateTime = eventTime;
                        }
                        if (order.queryCount > 5){
                            it->second->commandType = stra::CommandType_ERROR;
                            it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELED; 
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            //QuantPub::Instance().Publish(pubMsg);
                            LarkRebot::GetInstance().SendMsg(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                            pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                            LOG_ERROR("算法单终结(卡单): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_ERRORCANCELED，子单查询超 5 次仍无确认 orderStatus:{}",
                                      it->second->algoOrderId, it->second->pairInstrumentKey,
                                      OrderStatusEnum2StrMap[order.orderStatus]);
                            std::cout << "---stuck order----query error-----" << std::endl;
                        }

                        if (pass) {
                            order.queryCount += 1;
                        }
                    }
                } else if(order.orderStatus == OS_UNKNOWN) {
                    if (eventTime - order.updateTime > second1 * 15) {
                        // LOG_INFO("lark alarm! quantOrder: %s", order.GetStr().c_str());
                        // 异步lark报警
                        if (errorOrderReportCount > 60){
                            char msg[stra::MSG_LEN];
                            sprintf(msg, "check unknown order !!! strategyName:%s algoOrderId:%ld strategyOrderId:%ld  orderStatus:%s", it->second->algoStrategyName, it->second->algoOrderId, order.strategyOrderId, OrderStatusEnum2StrMap[order.orderStatus].c_str());
                            LarkRebot::GetInstance().SendMsg(msg);
                            errorOrderFlag = true;
                        }
                        // 主动发起订单查询
                        bool pass = LimitManager::Instance().PassLimit(order.strategyAccountId);
                        if (pass) {
                            QuantTrade::Instance().QueryOrder(order);
                            // 更新updatetime
                            order.updateTime = eventTime;
                        }
                        if (order.queryCount > 5) {
                            it->second->commandType = stra::CommandType_ERROR;
                            it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELED; 
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            //QuantPub::Instance().Publish(pubMsg);
                            LarkRebot::GetInstance().SendMsg(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                            pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                            LOG_ERROR("算法单终结(状态未知): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_ERRORCANCELED，子单 orderStatus 长期 OS_UNKNOWN 且查询超 5 次",
                                      it->second->algoOrderId, it->second->pairInstrumentKey);
                            std::cout << "---unknown order----query error-----" << std::endl;
                        }
                    }
                }
            }
            // 结束订单检查
            if (it->second->ttOLSwitch == false && it->second->ttOSSwitch == false && it->second->mtOLSwitch == false && it->second->mtOSSwitch == false) {
                if (it->second->algoType == stra::AlgoType_Rebalance) {
                    AlgoRebalanceOrder* pRebalanceOrder = (AlgoRebalanceOrder*)(it->second);
                    if (pRebalanceOrder->activeTrade == 1) {
                        orderAmount = fabs(it->second->pairTotalVolume);
                    }
                    else {
                        orderAmount = fabs(it->second->pairPassiveTotalVolume);
                    }

                    double minSize = pRebalanceOrder->activeTrade == 1 ? it->second->activeInfo.minSize : it->second->passiveInfo.minSize;


                    if (orderAmount < minSize && allPairOrders.size() == 0) {
                        //
                        //LOG_INFO("activeInstrumentKey:%s orderAmount:%f  activeInfo.minSize:%f  multiple:%f", it->second->activeInstrumentKey, orderAmount, it->second->activeInfo.minSize, it->second->activeInfo.multiple);
                        it->second->algoOrderStatus = stra::ALGO_OS_FILLED;
                        it->second->commandType = stra::CommandType_FINISHED;
                        it->second->updateTime = eventTime;
                        // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                        string pubMsg = it->second->GeneratePubStr();
                        //QuantPub::Instance().Publish(pubMsg);
                        LarkRebot::GetInstance().SendMsg(pubMsg);
                        WriteAlgoOrder(it->second);
                        deleteAlgoOrderFlag = true;
                        pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                        LOG_INFO("算法单终结(剩余量不足不再报单): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_FILLED，剩余量 {} < 最小报单量 {} 且无在途 pairOrder",
                                 it->second->algoOrderId, it->second->pairInstrumentKey, orderAmount, minSize);
                        std::cout << "orderAmount < minSize && allPairOrders.size() == 0" << std::endl;
                    }

                }
                else {
                    orderAmount = fabs(it->second->pairTotalVolume);
                    if (orderAmount < it->second->activeInfo.minSize && allPairOrders.size() == 0) {
                        //
                        //LOG_INFO("activeInstrumentKey:%s orderAmount:%f  activeInfo.minSize:%f  multiple:%f", it->second->activeInstrumentKey, orderAmount, it->second->activeInfo.minSize, it->second->activeInfo.multiple);
                        it->second->algoOrderStatus = stra::ALGO_OS_FILLED;
                        it->second->commandType = stra::CommandType_FINISHED;
                        it->second->updateTime = eventTime;
                        // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                        string pubMsg = it->second->GeneratePubStr();
                        //QuantPub::Instance().Publish(pubMsg);
                        LarkRebot::GetInstance().SendMsg(pubMsg);
                        WriteAlgoOrder(it->second);
                        deleteAlgoOrderFlag = true;
                        pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                        LOG_INFO("算法单终结(剩余量不足不再报单): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_FILLED，剩余量 {} < 最小报单量 {} 且无在途 pairOrder",
                                 it->second->algoOrderId, it->second->pairInstrumentKey, orderAmount, it->second->activeInfo.minSize);
                        std::cout << "orderAmount < it->second->activeInfo.minSize && allPairOrders.size() == 0" << std::endl;
                    }    
                }        
            }

            // rebalanceFlag设置
            if (rebalanceCount >= 5) {
                if (it->second->mtRebalanceSwitch) {
                    it->second->mtRebalanceFlag = true;
                }

                if (it->second->ttRebalanceSwitch) {
                    it->second->ttRebalanceFlag = true;
                }
            }
            // 延迟与滑点定期重置
            if (delayCount >= 60 * 5) {
                it->second->systemDelayFlag = false;
                it->second->exchangeDelayFlag = false;
            }
            if (slippageCount >= 60 * 5) {
                it->second->mtSlipageFlag = false;
                it->second->ttSlipageFlag = false;
            }

            if (spreadCount >= 60 * 5) {  // spreadCount应该是每个AlgoOrder有的吧；累计的价差也需要被重置吧？
                // it->second->mtSpread = 0;
                // it->second->ttSpread = 0;
                it->second->mtSpreadFlag = false;
                it->second->ttSpreadFlag = false;
            }

            if (fundVerifyCount >= 60) {
                it->second->fundVerifyFailedFlag = false;
            }


            if (infoReportCount >= 60) {
                string info = it->second->GetLastestStatusInfo();
                LarkRebot::GetInstance().SendMsg(info);
                // string pubMsg = it->second->GeneratePubStr();
                // LarkRebot::GetInstance().SendMsg(pubMsg);
            }
            
            // delete cancled status algoorder
            //LOG_INFO("algoId:%ld pairInstrumentKey:%s allPairOrders.size: %d  algoOrderStatus:%d", it->second->algoOrderId, it->second->pairInstrumentKey, allPairOrders.size(), int(it->second->algoOrderStatus));
            /*
	    if (allPairOrders.size() > 0 && it->second->algoOrderStatus == stra::ALGO_OS_CANCELLING) {
                stringstream ss;
                for (auto iter = allPairOrders.begin(); iter != allPairOrders.end(); ++iter) {
                    ss << "pairId:" << iter->second.pairId << " activeInstrumentKey:" << iter->second.activeInstrumentKey << " tradingTypeOrder:" << iter->second.tradingTypeOrder << " tradingTypeOffset:" << iter->second.tradingTypeOffset;
                }
                LOG_INFO("OrderStatus_CANCELLING --- %s", ss.str().c_str());
            }
	    */

            if (allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::ALGO_OS_CANCELLING) {
                it->second->commandType = stra::CommandType_CANCELED;
                it->second->algoOrderStatus = stra::ALGO_OS_CANCELED;
                string pubMsg = it->second->GeneratePubStr();
                //QuantPub::Instance().Publish(pubMsg);
                LarkRebot::GetInstance().SendMsg(pubMsg);
                deleteAlgoOrderFlag = true;
                pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                LOG_INFO("算法单终结(撤单完成): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_CANCELED，子单已全部清零",
                         it->second->algoOrderId, it->second->pairInstrumentKey);
                std::cout << "allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::ALGO_OS_CANCELLING" << std::endl;
            } else if (allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::ALGO_OS_ERRORCANCELLING) {  //需要考虑下这个状态的定义
                it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELED; 
                string pubMsg = it->second->GeneratePubStr();
                //QuantPub::Instance().Publish(pubMsg);
                LarkRebot::GetInstance().SendMsg(pubMsg);
                deleteAlgoOrderFlag = true;
                pt::PairTradingContext::NotifyAlgoOrderUpdate(it->second);
                LOG_WARN("算法单终结(异常撤单完成): algoOrderId:{} pairInstrumentKey:{} -> ALGO_OS_ERRORCANCELED，子单已全部清零",
                         it->second->algoOrderId, it->second->pairInstrumentKey);
                std::cout << "allPairOrders.size() == 0" << std::endl;
            }

            if (deleteAlgoOrderFlag) {
                // 刚被标记为终结的算法单不在这里立刻删除，交给循环开头的宽限期分支处理
                bool terminal = (it->second->algoOrderStatus == stra::ALGO_OS_FILLED ||
                                 it->second->algoOrderStatus == stra::ALGO_OS_CANCELED ||
                                 it->second->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED);
                if (terminal) {
                    it++;
                } else {
                    vUnSubscribePairInstId.push_back(it->second->pairInstrumentKey);

                    if (it->second) {
                        delete it->second;
                        it->second = nullptr;
                    }
                    allAlgoOrders.erase(it++);
                }
                deleteAlgoOrderFlag = false;
            } else {
                it++;
            }
        }

        if (eventTime - lastAlgoUpdateTime > 10 * 60 * second1) {
            lastAlgoUpdateTime = eventTime;
        }

        if (algoOrderReportCount >= 60) {
            if (algoOrderStr.length() > 0) {
                LarkRebot::GetInstance().SendMsg(algoOrderStr);
            } else {
                LarkRebot::GetInstance().SendMsg("There are no algo orders!");
            }
        }

        // 遍历，删除不需要的pairInstrumentKey
        for (size_t i = 0; i < vUnSubscribePairInstId.size(); ++i) {
            bool exist = false;
            for (auto iter = allAlgoOrders.begin(); iter != allAlgoOrders.end(); ++iter) {
                if (strcmp(vUnSubscribePairInstId[i].c_str(), iter->second->pairInstrumentKey) == 0) {
                    exist = true;
                    break;
                }
            }

            if (!exist) {
                SpreadManager::Instance().DeleteSpread(vUnSubscribePairInstId[i]);
                QuantDbp::Instance().UnSubscribe(vUnSubscribePairInstId[i]);   
            }
        }

        // 定时查询持仓
        if (queryAccountCount >= 10) {
	        QueryAccount();
        }

        // 重置计数器
        if (rebalanceCount >= 5) {
            rebalanceCount = 0;
        }
        if (delayCount >= 60 * 5) {
            delayCount = 0;
        }
        if (slippageCount >= 60 * 5) {
            slippageCount = 0;
        }

        if (spreadCount >= 60 * 5) {
            spreadCount = 0;
        }     

        if (fundVerifyCount >= 60) {
            fundVerifyCount = 0;
        }

        if (stuckOrderFlag) {
            stuckOrderReportCount = 0;
        }
        if (errorOrderFlag) {
            errorOrderReportCount = 0;
        }

        for (auto iter = mSpreadReportCount.begin(); iter != mSpreadReportCount.end(); ++iter) {
            if (iter->second >= 60) {
                iter->second = 0;
            }
        }

        if (infoReportCount >= 60) {
            infoReportCount = 0;
        }

        if (algoOrderReportCount >= 60) {
            algoOrderReportCount = 0;
        }

        if (queryAccountCount >= 10) {
            queryAccountCount = 0;
        }


    } catch(StraException& e) {
        LOG_INFO("StraException in AlgoContext::OnTimer, error msg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException in AlgoContext::OnTimer, error msg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnTimer, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnTimer, errormsg:%s", e.what());
        LarkRebot::GetInstance().SendMsg(msg);
    }

    // 如果多次查询仍无法同步订单状态，有订单丢失的情况，则需要停止algoOrder,返回回报并将algoOrder从algoOrderMgr中删除，回报时订单状态为Fault

    // 查询资金持仓, 按照algo_pair_order的account_id一个一个查一个一个更新, 查询有回报后这个帐号才可以交易(才可以进行验资)

    // 另外,algoOrder增加一个暂停报单的属性, 每次停止交易若干秒，这个锁在on_timer里面进行解锁


    // rebalanceFlag设置
}

void AlgoContext::OnBalance(const pubsub::Balance& balance) {
    AccountManager::Instance().OnBalance(balance);
}

void AlgoContext::OnPosition(const pubsub::Position& position) {
    AccountManager::Instance().OnPosition(position);
}

void AlgoContext::OnTotalAccount(const pubsub::TotalAccount& totalAccount) {
    AccountManager::Instance().OnTotalAccount(totalAccount);
}

// 持久化
// algoOrder在每次创建和终结一个pairOrder时需要持久化
// pairOrder在被创建和终结时需要持久化
// quantOrder在被创建和终结时需要持久化

BaseAlgoOrder* AlgoContext::GetAlgoOrder(int64_t algoOrderId) {
    return alogOrderManager.SeletAlgoOrderByAlgoOrderId(algoOrderId);
}
