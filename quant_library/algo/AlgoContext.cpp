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


unordered_map<string, int> mAccountNameAccountId;

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

    StrategyConfig::GetInstance().LoadConfig();
    BasicInfoMgr::GetInstance();
    LarkRebot::GetInstance();
    WriteFileContent::GetInstance();
    AccountManager::Instance().Init();
    LimitManager::Instance().Init();


    curSpreadDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadDelay()) * 1000;
    curSpreadDepthDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadDepthDelay()) * 1000;
    curSpreadTradesDelay = static_cast<int64_t>(StrategyConfig::GetInstance().GetCurSpreadTradesDelay()) * 1000;
    lastAlgoUpdateTime = GetCurrentTimeUs();
    tradesDelayThreshold = static_cast<int64_t>(StrategyConfig::GetInstance().GetTradesThreshold()) * 1000;
    onTimerTrade = StrategyConfig::GetInstance().GetOnTimerTrade();


    string tradeConfigStr = crypto::read_file("/inc/trade_config.json");
    rapidjson::Document d1;
    rapidjson::Value &tradeConfig = d1.Parse<rapidjson::kParseNumbersAsStringsFlag>(tradeConfigStr.c_str());
    utrade2SccChannel = tradeConfig["utrade"]["Utrade2SCCChannel"].GetString();
    QuantPub::Instance().SetPubChannel(utrade2SccChannel);
}

AlgoContext::~AlgoContext() {
}

void AlgoContext::PreStart() {
    QueryAccount();
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

void AlgoContext::SetPub(RedisClient* redisClient) {
    QuantPub::Instance().SetPub(redisClient);
}

void AlgoContext::OnCommand(string s) {
    // rLarkMsg.Push(s);
    LOG_INFO("OnCommand: {}", s);
    try {
        rapidjson::Document d;
        rapidjson::Value &cmdValue = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(s.c_str());

        std::string sccId = "";
        std::string toAec = "";
        std::string clientOrderId = "";
        std::string pairInstrumentKey = "";
        std::string activeInstrumentKey = "";
        std::string passiveInstrumentKey = "";
        std::string operateTag = "";
        double pairTotalVolume = 0.0;
        double pairPassiveTotalVolume = 0.0;
        stra::CommandType commandType = stra::CommandType_MIN;
        int64_t insertTime;
        stra::AlgoType algoType = stra::AlgoType_MIN;
        BaseAlgoOrder* pAlgoOrder = nullptr;
        if (cmdValue.IsObject()) {
            if (cmdValue.HasMember("sccId")) {
                sccId = cmdValue["sccId"].GetString();
            }

            if (cmdValue.HasMember("toAec")) {
                toAec = cmdValue["toAec"].GetString();
            }

            if (cmdValue.HasMember("clientOrderId")) {
                clientOrderId = cmdValue["clientOrderId"].GetString();
            }
            
            if (cmdValue.HasMember("commandType")) {
                commandType = stra::CommandType(stoi(cmdValue["commandType"].GetString()));
            }

            if (cmdValue.HasMember("insertTime")) {
                insertTime = int64_t(stod(cmdValue["insertTime"].GetString()));
            }


            if (cmdValue.HasMember("jsonBody")) {
                rapidjson::Value& body = cmdValue["jsonBody"];

                if (body.HasMember("algoType")) {
                    algoType = stra::AlgoType(stoi(body["algoType"].GetString()));
                }

                if (body.HasMember("pairInstrumentKey")) {
                    pairInstrumentKey = body["pairInstrumentKey"].GetString();
                }

                if (body.HasMember("activeInstrumentKey")) {
                    activeInstrumentKey = body["activeInstrumentKey"].GetString();
                }

                if (body.HasMember("passiveInstrumentKey")) {
                    passiveInstrumentKey = body["passiveInstrumentKey"].GetString();
                }

                if (body.HasMember("pairTotalVolume")) {
                    pairTotalVolume = std::stod(body["pairTotalVolume"].GetString());
                }

                if (body.HasMember("pairPassiveTotalVolume")) {
                    pairPassiveTotalVolume = std::stod(body["pairPassiveTotalVolume"].GetString());
                }

                if (body.HasMember("operate_tag")) {
                    operateTag = body["operate_tag"].GetString();
                }

                if (commandType == stra::CommandType_NEW) { // 如果
                    auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
                    for (auto iter = allAlgoOrders.begin(); iter != allAlgoOrders.end(); ++iter) {
                        if (strcmp(iter->second->clientOrderId, clientOrderId.c_str()) == 0) {
                            json pub;
                            pub["sccId"] = std::string(sccId);
                            pub["toAec"] = std::string(toAec);
                            pub["clientOrderId"] = std::string(clientOrderId);
                            pub["commandType"] = int(stra::CommandType_ERROR);
                            pub["insertTime"] = insertTime;
                            pub["errMsg"] = "utrade already has same clientOrderId algoOrder!";
                            std::string pubMsg = pub.dump();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            return;
                        }

                        if (strcmp(iter->second->pairInstrumentKey, pairInstrumentKey.c_str()) == 0) {
                            if (algoType != stra::AlgoType_FishingTrading) {
                                json pub;
                                pub["sccId"] = std::string(sccId);
                                pub["toAec"] = std::string(toAec);
                                pub["clientOrderId"] = std::string(clientOrderId);
                                pub["commandType"] = int(stra::CommandType_ERROR);
                                pub["insertTime"] = insertTime;
                                pub["errMsg"] = "utrade already has same pairInstrumentKey algoOrder!";
                                std::string pubMsg = pub.dump();
                                QuantPub::Instance().Publish(pubMsg);
                                rLarkMsg.Push(pubMsg);
                                return;
                            }
                        }
                    }

                    if (algoType == stra::AlgoType_Rebalance && operateTag != "TinyCloseOnly") {
                        stra::InstrumentInfo& activeInfo = BasicInfoMgr::GetInstance().GetBasicInfo(activeInstrumentKey);
                        stra::InstrumentInfo& passiveInfo = BasicInfoMgr::GetInstance().GetBasicInfo(passiveInstrumentKey);
                        double activeVolume = fabs(pairTotalVolume) * activeInfo.multiple;
                        double passiveVolume = fabs(pairPassiveTotalVolume) * passiveInfo.multiple;
                        double exposureVolume = fabs(activeVolume - passiveVolume);
                        double minVolume = max(activeInfo.minSize * activeInfo.multiple, passiveInfo.minSize * passiveInfo.multiple); // usdt本位

                        if (fabs(activeVolume - passiveVolume) < minVolume && (fabs(pairTotalVolume) > activeInfo.minSize || fabs(pairPassiveTotalVolume) > passiveInfo.minSize)) {
                            json pub;
                            pub["sccId"] = std::string(sccId);
                            pub["toAec"] = std::string(toAec);
                            pub["clientOrderId"] = std::string(clientOrderId);
                            pub["commandType"] = int(stra::CommandType_ERROR);
                            pub["insertTime"] = insertTime;
                            pub["errMsg"] = "rebalance algoOrder has no exposure!";
                            std::string pubMsg = pub.dump();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            return;       
                        }
                    }

                    if (algoType == stra::AlgoType_PairTrading) {
                        pAlgoOrder = new AlgoPairOrder();
                    } else if (algoType == stra::AlgoType_FishingTrading) {
                        pAlgoOrder = new AlgoFishingOrder();
                    } else if (algoType == stra::AlgoType_Rebalance) {
                        pAlgoOrder = new AlgoRebalanceOrder();
                    }
                } else if (commandType == stra::CommandType_CANCEL || commandType == stra::CommandType_MODIFY || commandType == stra::CommandType_QUERY) {
                    int64_t algoOrderId = stoll(body["systemOrderId"].GetString());
                    pAlgoOrder = alogOrderManager.SeletAlgoOrderByAlgoOrderId(algoOrderId);
                } 

                if (pAlgoOrder == nullptr) {
                    // publish error
                    json pub;
                    pub["sccId"] = string(sccId);
                    pub["toAec"] = string(toAec);
                    pub["clientOrderId"] = string(clientOrderId);
                    pub["commandType"] = int(stra::CommandType_ERROR);
                    pub["insertTime"] = insertTime;
                    pub["errMsg"] = "can not find algoOrder!";
                    string pubMsg = pub.dump();
                    QuantPub::Instance().Publish(pubMsg);
                    rLarkMsg.Push(pubMsg);
                    return;
                }

                if (commandType == stra::CommandType_QUERY) {
                    pAlgoOrder->commandType = stra::CommandType_QUERIED;
                    string pubMsg = pAlgoOrder->GeneratePubStr();
                    QuantPub::Instance().Publish(pubMsg);
                    rLarkMsg.Push(pubMsg);
                    return;
                }

                if (commandType == stra::CommandType_CANCEL) {
                    pAlgoOrder->commandType = stra::CommandType_UCANCELLING;
                    pAlgoOrder->algoOrderStatus = stra::OrderStatus_CANCELLING;
                    pAlgoOrder->cancelOrderTime = GetCurrentTimeUs();
                    string pubMsg = pAlgoOrder->GeneratePubStr();
                    QuantPub::Instance().Publish(pubMsg);
                    rLarkMsg.Push(pubMsg);
                    return;
                }


                strncpy(pAlgoOrder->sccId, sccId.c_str(), stra::INST_ID_LEN);
                strncpy(pAlgoOrder->toAec, toAec.c_str(), stra::NAME_LEN);
                strncpy(pAlgoOrder->clientOrderId, clientOrderId.c_str(), stra::ID_LEN);
                pAlgoOrder->commandType = commandType;
                pAlgoOrder->insertTime = insertTime;

                if (body.HasMember("algoStrategyName")) {
                    string algoStrategyName = body["algoStrategyName"].GetString();
                    strncpy(pAlgoOrder->algoStrategyName, algoStrategyName.c_str(), stra::NAME_LEN);
                }

                if (body.HasMember("pairInstrumentKey")) {
                    strncpy(pAlgoOrder->pairInstrumentKey, pairInstrumentKey.c_str(), stra::INST_KEY_LEN);
                }

                if (body.HasMember("baseAsset")) {
                    string baseAsset = body["baseAsset"].GetString();
                    strncpy(pAlgoOrder->baseAsset, baseAsset.c_str(), stra::ASSET_LEN);
                }

                if (body.HasMember("activeInstrumentKey")) {
                    strncpy(pAlgoOrder->activeInstrumentKey, activeInstrumentKey.c_str(), stra::INST_KEY_LEN);
                }

                if (body.HasMember("activePriceTakerPct")) {
                    pAlgoOrder->activePriceTakerPct = stod(body["activePriceTakerPct"].GetString());
                }

                if (body.HasMember("activePriceMakerPct")) {
                    pAlgoOrder->activePriceMakerPct = stod(body["activePriceMakerPct"].GetString());
                }

                if (body.HasMember("activeAccountId")) {
                    pAlgoOrder->activeAccountId = mAccountNameAccountId[body["activeAccountId"].GetString()];
                }

                if (body.HasMember("activeDriveType")) {
                    pAlgoOrder->activeDriveType = stra::DriveType(stoi(body["activeDriveType"].GetString()));
                }

                if (body.HasMember("activeDepthMakerCheck")) {
                    pAlgoOrder->activeDepthMakerCheck = body["activeDepthMakerCheck"].GetBool();
                }

                if (body.HasMember("activeDepthTakerCheck")) {
                    pAlgoOrder->activeDepthTakerCheck = body["activeDepthTakerCheck"].GetBool();
                }

                if (body.HasMember("activeOrderType")) {
                    pAlgoOrder->activeOrderType = stra::OrderType(stoi(body["activeOrderType"].GetString()));
                }

                if (body.HasMember("passiveInstrumentKey")) {
                    strncpy(pAlgoOrder->passiveInstrumentKey, passiveInstrumentKey.c_str(), stra::INST_KEY_LEN);
                }

                if (body.HasMember("passivePriceTakerPct")) {
                    pAlgoOrder->passivePriceTakerPct = stod(body["passivePriceTakerPct"].GetString());
                }

                if (body.HasMember("passivePriceMakerPct")) {
                    pAlgoOrder->passivePriceMakerPct = stod(body["passivePriceMakerPct"].GetString());
                }

                if (body.HasMember("passiveAccountId")) {
                    pAlgoOrder->passiveAccountId = mAccountNameAccountId[body["passiveAccountId"].GetString()];
                }

                if (body.HasMember("passiveDriveType")) {
                    pAlgoOrder->passiveDriveType = stra::DriveType(stoi(body["passiveDriveType"].GetString()));
                }

                if (body.HasMember("passiveDepthMakerCheck")) {
                    pAlgoOrder->passiveDepthMakerCheck = body["passiveDepthMakerCheck"].GetBool();
                }

                if (body.HasMember("passiveDepthTakerCheck")) {
                    pAlgoOrder->passiveDepthTakerCheck = body["passiveDepthTakerCheck"].GetBool();
                }

                if (body.HasMember("passiveOrderType")) {
                    pAlgoOrder->passiveOrderType = stra::OrderType(stoi(body["passiveOrderType"].GetString()));
                }


                if (body.HasMember("passiveVolumePct")) {
                    pAlgoOrder->passiveVolumePct = stod(body["passiveVolumePct"].GetString());
                }

                if (body.HasMember("activeMakerCancelOrderTime")) {
                    pAlgoOrder->activeMakerCancelOrderTime = stoll(body["activeMakerCancelOrderTime"].GetString());
                }

                if (body.HasMember("activeTakerCancelOrderTime")) {
                    pAlgoOrder->activeTakerCancelOrderTime = stoll(body["activeTakerCancelOrderTime"].GetString());
                }

                if (body.HasMember("passiveMakerCancelOrderTime")) {
                    pAlgoOrder->passiveMakerCancelOrderTime = stoll(body["passiveMakerCancelOrderTime"].GetString());
                }

                if (body.HasMember("passiveTakerCancelOrderTime")) {
                    pAlgoOrder->passiveTakerCancelOrderTime = stoll(body["passiveTakerCancelOrderTime"].GetString());
                }

                if (body.HasMember("activePassiveCancelOrderPct")) {
                    pAlgoOrder->activePassiveCancelOrderPct = stod(body["activePassiveCancelOrderPct"].GetString());
                }

                if (body.HasMember("activeMakerCancelOrderPct")) {
                    pAlgoOrder->activeMakerCancelOrderPct = stod(body["activeMakerCancelOrderPct"].GetString());
                }

                if (body.HasMember("activeTakerCancelOrderPct")) {
                    pAlgoOrder->activeTakerCancelOrderPct = stod(body["activeTakerCancelOrderPct"].GetString());
                }

                if (body.HasMember("passiveMakerCancelOrderPct")) {
                    pAlgoOrder->passiveMakerCancelOrderPct = stod(body["passiveMakerCancelOrderPct"].GetString());
                }

                if (body.HasMember("passiveTakerCancelOrderPct")) {
                    pAlgoOrder->passiveTakerCancelOrderPct = stod(body["passiveTakerCancelOrderPct"].GetString());
                }

                if (body.HasMember("activeMakerFeeRate")) {
                    pAlgoOrder->activeMakerFeeRate = stod(body["activeMakerFeeRate"].GetString());
                }

                if (body.HasMember("activeTakerFeeRate")) {
                    pAlgoOrder->activeTakerFeeRate = stod(body["activeTakerFeeRate"].GetString());
                }

                if (body.HasMember("passiveMakerFeeRate")) {
                    pAlgoOrder->passiveMakerFeeRate = stod(body["passiveMakerFeeRate"].GetString());
                }

                if (body.HasMember("passiveTakerFeeRate")) {
                    pAlgoOrder->passiveTakerFeeRate = stod(body["passiveTakerFeeRate"].GetString());
                }

                if (body.HasMember("activeTakerSlippage")) {
                    pAlgoOrder->activeTakerSlippage = stod(body["activeTakerSlippage"].GetString());
                }

                if (body.HasMember("activeMakerSlippage")) {
                    pAlgoOrder->activeMakerSlippage = stod(body["activeMakerSlippage"].GetString());
                }

                if (body.HasMember("passiveTakerSlippage")) {
                    pAlgoOrder->passiveTakerSlippage = stod(body["passiveTakerSlippage"].GetString());
                }

                if (body.HasMember("passiveMakerSlippage")) {
                    pAlgoOrder->passiveMakerSlippage = stod(body["passiveMakerSlippage"].GetString());
                }

                if (body.HasMember("pairActiveTotalPrice")) {
                    pAlgoOrder->pairActiveTotalPrice = stod(body["pairActiveTotalPrice"].GetString());
                }

                if (body.HasMember("pairTotalVolume")) {
                    pAlgoOrder->pairTotalVolume = stod(body["pairTotalVolume"].GetString());
                }

                if (body.HasMember("pairPassiveTotalVolume")) {
                    pAlgoOrder->pairPassiveTotalVolume = stod(body["pairPassiveTotalVolume"].GetString());
                }

                if (body.HasMember("pairPassiveTotalPrice")) {
                    pAlgoOrder->pairPassiveTotalPrice = stod(body["pairPassiveTotalPrice"].GetString());
                }
                
                if (body.HasMember("maxMTOrderSize")) {
                    pAlgoOrder->maxMTOrderSize = stod(body["maxMTOrderSize"].GetString());
                }

                if (body.HasMember("maxTTOrderSize")) {
                    pAlgoOrder->maxTTOrderSize = stod(body["maxTTOrderSize"].GetString());
                }   

                if (body.HasMember("targetSpreadType")) {
                    pAlgoOrder->targetSpreadType = stra::TargetSpredPrice(stoi(body["targetSpreadType"].GetString()));
                }   

                if (body.HasMember("activeVolumeCalcualteType")) {
                    pAlgoOrder->activeVolumeCalcualteType = stra::ActiveVolumeCalcualteType(stoi(body["activeVolumeCalcualteType"].GetString()));
                }   

                if (body.HasMember("ttTargetVolume")) {
                    pAlgoOrder->ttTargetVolume = stod(body["ttTargetVolume"].GetString());
                }   

                if (body.HasMember("mtTargetVolume")) {
                    pAlgoOrder->mtTargetVolume = stod(body["mtTargetVolume"].GetString());
                }   

                if (body.HasMember("minVolume")) {
                    pAlgoOrder->minVolume = stod(body["minVolume"].GetString());
                }   

                if (body.HasMember("profitSwitch")) {
                    pAlgoOrder->profitSwitch = body["profitSwitch"].GetBool();
                }   

                if (body.HasMember("profitPct")) {
                    pAlgoOrder->profitPct = stod(body["profitPct"].GetString());
                }   

                if (body.HasMember("ttOLStartSpread")) {
                    pAlgoOrder->ttOLStartSpread = stod(body["ttOLStartSpread"].GetString());
                }   

                if (body.HasMember("ttOLEndSpread")) {
                    pAlgoOrder->ttOLEndSpread = stod(body["ttOLEndSpread"].GetString());
                } 

                if (body.HasMember("ttOLStartVolume")) {
                    pAlgoOrder->ttOLStartVolume = stod(body["ttOLStartVolume"].GetString());
                } 

                if (body.HasMember("ttOLEndVolume")) {
                    pAlgoOrder->ttOLEndVolume = stod(body["ttOLEndVolume"].GetString());
                } 

                if (body.HasMember("ttOLSwitch")) {
                    pAlgoOrder->ttOLSwitch = body["ttOLSwitch"].GetBool();
                }   

                if (body.HasMember("ttCLStartSpread")) {
                    pAlgoOrder->ttCLStartSpread = stod(body["ttCLStartSpread"].GetString());
                }   

                if (body.HasMember("ttCLEndSpread")) {
                    pAlgoOrder->ttCLEndSpread = stod(body["ttCLEndSpread"].GetString());
                } 

                if (body.HasMember("ttCLStartVolume")) {
                    pAlgoOrder->ttCLStartVolume = stod(body["ttCLStartVolume"].GetString());
                } 

                if (body.HasMember("ttCLEndVolume")) {
                    pAlgoOrder->ttCLEndVolume = stod(body["ttCLEndVolume"].GetString());
                } 

                if (body.HasMember("ttCLSwitch")) {
                    pAlgoOrder->ttCLSwitch = body["ttCLSwitch"].GetBool();
                }   


                if (body.HasMember("ttOSStartSpread")) {
                    pAlgoOrder->ttOSStartSpread = stod(body["ttOSStartSpread"].GetString());
                }   

                if (body.HasMember("ttOSEndSpread")) {
                    pAlgoOrder->ttOSEndSpread = stod(body["ttOSEndSpread"].GetString());
                } 

                if (body.HasMember("ttOSStartVolume")) {
                    pAlgoOrder->ttOSStartVolume = stod(body["ttOSStartVolume"].GetString());
                } 

                if (body.HasMember("ttOSEndVolume")) {
                    pAlgoOrder->ttOSEndVolume = stod(body["ttOSEndVolume"].GetString());
                } 

                if (body.HasMember("ttOSSwitch")) {
                    pAlgoOrder->ttOSSwitch = body["ttOSSwitch"].GetBool();
                }   


                if (body.HasMember("ttCSStartSpread")) {
                    pAlgoOrder->ttCSStartSpread = stod(body["ttCSStartSpread"].GetString());
                }   

                if (body.HasMember("ttCSEndSpread")) {
                    pAlgoOrder->ttCSEndSpread = stod(body["ttCSEndSpread"].GetString());
                } 

                if (body.HasMember("ttCSStartVolume")) {
                    pAlgoOrder->ttCSStartVolume = stod(body["ttCSStartVolume"].GetString());
                } 

                if (body.HasMember("ttCSEndVolume")) {
                    pAlgoOrder->ttCSEndVolume = stod(body["ttCSEndVolume"].GetString());
                } 

                if (body.HasMember("ttCSSwitch")) {
                    pAlgoOrder->ttCSSwitch = body["ttCSSwitch"].GetBool();
                }   


                if (body.HasMember("mtOLStartSpread")) {
                    pAlgoOrder->mtOLStartSpread = stod(body["mtOLStartSpread"].GetString());
                }   

                if (body.HasMember("mtOLEndSpread")) {
                    pAlgoOrder->mtOLEndSpread = stod(body["mtOLEndSpread"].GetString());
                } 

                if (body.HasMember("mtOLStartVolume")) {
                    pAlgoOrder->mtOLStartVolume = stod(body["mtOLStartVolume"].GetString());
                } 

                if (body.HasMember("mtOLEndVolume")) {
                    pAlgoOrder->mtOLEndVolume = stod(body["mtOLEndVolume"].GetString());
                } 

                if (body.HasMember("mtOLSwitch")) {
                    pAlgoOrder->mtOLSwitch = body["mtOLSwitch"].GetBool();
                }   


                if (body.HasMember("mtCLStartSpread")) {
                    pAlgoOrder->mtCLStartSpread = stod(body["mtCLStartSpread"].GetString());
                }   

                if (body.HasMember("mtCLEndSpread")) {
                    pAlgoOrder->mtCLEndSpread = stod(body["mtCLEndSpread"].GetString());
                } 

                if (body.HasMember("mtCLStartVolume")) {
                    pAlgoOrder->mtCLStartVolume = stod(body["mtCLStartVolume"].GetString());
                } 

                if (body.HasMember("mtCLEndVolume")) {
                    pAlgoOrder->mtCLEndVolume = stod(body["mtCLEndVolume"].GetString());
                } 

                if (body.HasMember("mtCLSwitch")) {
                    pAlgoOrder->mtCLSwitch = body["mtCLSwitch"].GetBool();
                }   


                if (body.HasMember("mtOSStartSpread")) {
                    pAlgoOrder->mtOSStartSpread = stod(body["mtOSStartSpread"].GetString());
                }   

                if (body.HasMember("mtOSEndSpread")) {
                    pAlgoOrder->mtOSEndSpread = stod(body["mtOSEndSpread"].GetString());
                } 

                if (body.HasMember("mtOSStartVolume")) {
                    pAlgoOrder->mtOSStartVolume = stod(body["mtOSStartVolume"].GetString());
                } 

                if (body.HasMember("mtOSEndVolume")) {
                    pAlgoOrder->mtOSEndVolume = stod(body["mtOSEndVolume"].GetString());
                } 

                if (body.HasMember("mtOSSwitch")) {
                    pAlgoOrder->mtOSSwitch = body["mtOSSwitch"].GetBool();
                }  


                if (body.HasMember("mtCSStartSpread")) {
                    pAlgoOrder->mtCSStartSpread = stod(body["mtCSStartSpread"].GetString());
                }   

                if (body.HasMember("mtCSEndSpread")) {
                    pAlgoOrder->mtCSEndSpread = stod(body["mtCSEndSpread"].GetString());
                } 

                if (body.HasMember("mtCSStartVolume")) {
                    pAlgoOrder->mtCSStartVolume = stod(body["mtCSStartVolume"].GetString());
                } 

                if (body.HasMember("mtCSEndVolume")) {
                    pAlgoOrder->mtCSEndVolume = stod(body["mtCSEndVolume"].GetString());
                } 

                if (body.HasMember("mtCSSwitch")) {
                    pAlgoOrder->mtCSSwitch = body["mtCSSwitch"].GetBool();
                }


                if (body.HasMember("mtRebalanceSwitch")) {
                    pAlgoOrder->mtRebalanceSwitch = body["mtRebalanceSwitch"].GetBool();
                }

                if (body.HasMember("ttRebalanceSwitch")) {
                    pAlgoOrder->ttRebalanceSwitch = body["ttRebalanceSwitch"].GetBool();
                }

                if (body.HasMember("mtRebalanceFlag")) {
                    pAlgoOrder->mtRebalanceFlag = body["mtRebalanceFlag"].GetBool();
                }

                if (body.HasMember("ttRebalanceFlag")) {
                    pAlgoOrder->ttRebalanceFlag = body["ttRebalanceFlag"].GetBool();
                }

                if (body.HasMember("mtPriceTrendProtectFlag")) {
                    pAlgoOrder->mtPriceTrendProtectFlag = body["mtPriceTrendProtectFlag"].GetBool();
                }
    
                if (body.HasMember("ttPriceTrendProtectFlag")) {
                    pAlgoOrder->ttPriceTrendProtectFlag = body["ttPriceTrendProtectFlag"].GetBool();
                }
    

                if (body.HasMember("activePriceTickFlag")) {
                    pAlgoOrder->activePriceTickFlag = body["activePriceTickFlag"].GetBool();
                }
        
                if (body.HasMember("activePriceTickNum")) {
                    pAlgoOrder->activePriceTickNum = stod(body["activePriceTickNum"].GetString());
                }

                if (body.HasMember("passivePriceTickFlag")) {
                    pAlgoOrder->passivePriceTickFlag = body["passivePriceTickFlag"].GetBool();
                }
        
                if (body.HasMember("passivePriceTickNum")) {
                    pAlgoOrder->passivePriceTickNum = stoi(body["passivePriceTickNum"].GetString());
                }

                if (operateTag == "Manual") {
                    pAlgoOrder->isManual = true;
                }
                else {
                    pAlgoOrder->isManual = false;
                }

                pAlgoOrder->takerTakerFs = pAlgoOrder->activeTakerFeeRate + pAlgoOrder->passiveTakerFeeRate + pAlgoOrder->activeTakerSlippage + pAlgoOrder->passiveTakerSlippage;
                pAlgoOrder->makerTakerFs = pAlgoOrder->activeMakerFeeRate + pAlgoOrder->passiveTakerFeeRate + pAlgoOrder->activeMakerSlippage + pAlgoOrder->passiveTakerSlippage;
                

                AlgoPairOrder* pPairOrder = nullptr;
                AlgoFishingOrder* pFishingOrder = nullptr;
                AlgoRebalanceOrder* pRebalanceOrder = nullptr;
                if (algoType == stra::AlgoType_PairTrading) {
                    pPairOrder = (AlgoPairOrder*)pAlgoOrder;
                } else if (algoType == stra::AlgoType_FishingTrading) {
                    pFishingOrder = (AlgoFishingOrder*)pAlgoOrder;
                    if (body.HasMember("fishingSlippagePct")) {
                        pFishingOrder->fishingSlippagePct = stod(body["fishingSlippagePct"].GetString());
                    }
                } else if (algoType == stra::AlgoType_Rebalance) {
                    pRebalanceOrder = (AlgoRebalanceOrder*)pAlgoOrder;
                    if (body.HasMember("activeTrade")) {
                        pRebalanceOrder->activeTrade = stoi(body["activeTrade"].GetString());
                    }
                }

                if (commandType == stra::CommandType_NEW) {
                    if (pPairOrder) {
                        pPairOrder->commandType = stra::CommandType_TRADING;
                        pPairOrder->algoOrderStatus = stra::OrderStatus_NEW;
                        pPairOrder->algoType = algoType;
                        pAlgoOrder->algoOrderId = GenerateStrategyAlgoPairId();
                        pPairOrder->Init();
                        alogOrderManager.InsertAlgoOrderByAlgoOrder(pPairOrder);
                        string pubMsg = pPairOrder->GeneratePubStr();
                        QuantPub::Instance().Publish(pubMsg);
                        rLarkMsg.Push(pubMsg);
                        WriteAlgoPairOrder(pPairOrder);

                        bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pPairOrder->pairInstrumentKey);
                        if (!exist) {
                            LOG_INFO("Subscribe pairInstrumentKey:{}", pPairOrder->pairInstrumentKey);
                            SpreadManager::Instance().AddSpreadPara(pPairOrder->pairInstrumentKey, 10);
                            QuantDbp::Instance().Subscribe(pPairOrder->pairInstrumentKey);
                        } else {
                            LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", pPairOrder->pairInstrumentKey);
                        }
                    }

                    if (pFishingOrder) {
                        pFishingOrder->commandType = stra::CommandType_TRADING;
                        pFishingOrder->algoOrderStatus = stra::OrderStatus_NEW;
                        pFishingOrder->algoType = algoType;
                        pFishingOrder->algoOrderId = GenerateStrategyAlgoPairId();
                        pFishingOrder->Init();
                        alogOrderManager.InsertAlgoOrderByAlgoOrder(pFishingOrder);
                        string pubMsg = pFishingOrder->GeneratePubStr();
                        QuantPub::Instance().Publish(pubMsg);
                        rLarkMsg.Push(pubMsg);
                        WriteAlgoFishingOrder(pFishingOrder);

                        bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pFishingOrder->pairInstrumentKey);
                        if (!exist) {
                            LOG_INFO("Subscribe pairInstrumentKey:{}", pFishingOrder->pairInstrumentKey);
                            SpreadManager::Instance().AddSpreadPara(pFishingOrder->pairInstrumentKey, 10);
                            QuantDbp::Instance().Subscribe(pFishingOrder->pairInstrumentKey);
                        } else {
                            LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", pPairOrder->pairInstrumentKey);
                        }
                    }

                    if (pRebalanceOrder) {
                        pRebalanceOrder->commandType = stra::CommandType_TRADING;
                        pRebalanceOrder->algoOrderStatus = stra::OrderStatus_NEW;
                        pRebalanceOrder->algoType = algoType;
                        pRebalanceOrder->algoOrderId = GenerateStrategyAlgoPairId();
                        pRebalanceOrder->Init();
                        alogOrderManager.InsertAlgoOrderByAlgoOrder(pRebalanceOrder);
                        string pubMsg = pRebalanceOrder->GeneratePubStr();
                        QuantPub::Instance().Publish(pubMsg);
                        rLarkMsg.Push(pubMsg);
                        WriteAlgoRebalanceOrder(pRebalanceOrder);

                        bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pRebalanceOrder->pairInstrumentKey);
                        if (!exist) {
                            LOG_INFO("Subscribe pairInstrumentKey:{}", pRebalanceOrder->pairInstrumentKey);
                            SpreadManager::Instance().AddSpreadPara(pRebalanceOrder->pairInstrumentKey, 10);
                            QuantDbp::Instance().Subscribe(pRebalanceOrder->pairInstrumentKey);
                        } else {
                            LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", pPairOrder->pairInstrumentKey);
                        }
                    }

                } else if (commandType == stra::CommandType_MODIFY) {
                    if (pAlgoOrder) {
                        pAlgoOrder->commandType = stra::CommandType_MODIFIED;
                        if (pAlgoOrder->algoType == stra::AlgoType_PairTrading) {
                            AlgoPairOrder* pPairOrder = (AlgoPairOrder*)pAlgoOrder;
                            string pubMsg = pPairOrder->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoPairOrder(pPairOrder);
                        } else if (pAlgoOrder->algoType == stra::AlgoType_FishingTrading) {
                            AlgoFishingOrder* pFishingOrder = (AlgoFishingOrder*)pAlgoOrder;
                            if (body.HasMember("fishingSlippagePct")) {
                                pFishingOrder->fishingSlippagePct = stod(body["fishingSlippagePct"].GetString());
                            }
                            string pubMsg = pFishingOrder->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoFishingOrder(pFishingOrder);
                        } else if (pAlgoOrder->algoType == stra::AlgoType_Rebalance) {
                            AlgoRebalanceOrder* pRebalanceOrder = (AlgoRebalanceOrder*)pAlgoOrder;
                            if (body.HasMember("activeTrade")) {
                                pRebalanceOrder->activeTrade = stoi(body["activeTrade"].GetString());
                            }

                            string pubMsg = pRebalanceOrder->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoRebalanceOrder(pRebalanceOrder);
                        }
                    }
                    else {
                        LOG_ERROR("modify algo order error, no algo order id!");
                    }
                }
            }
        } else {
            LOG_INFO("cmdValue is not object: {}", s);
        }
    } catch(StraException& e) {
        LOG_INFO("StraException in AlgoContext::OnCommand, error msg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException in AlgoContext::OnCommand, error msg:{}", e.what());
        rLarkMsg.Push(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnCommand, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnCommand, errormsg:{}", e.what());
        rLarkMsg.Push(msg);
    }
    
}

void AlgoContext::OnMarketDepth() {

}

void AlgoContext::OnMarketTrade() {

}

void AlgoContext::OnSpread(const stra::MdSpread& spread, int64_t eventTime) {
    const stra::QuantSpread& quantSpread = ConvertTdSpreadToStraSpread(spread);
    SpreadManager::Instance().OnMarketSpread(quantSpread, eventTime);

    OnSpreadTrade(quantSpread, eventTime);
}
    
void AlgoContext::OnSpreadTrade(const stra::QuantSpread& quantSpread, int64_t eventTime) {
    try {
        // if (quantSpread.spreadDrive == stra::SpreadDrive_Active) {
        stra::QuantMarketDepth activeDepth = DataManager::Instance().GetLastDepth(quantSpread.activeInstumentKey);
        // } else if (quantSpread.spreadDrive == stra::SpreadDrive_Passive) {
        stra::QuantMarketDepth passiveDepth = DataManager::Instance().GetLastDepth(quantSpread.passiveInstrumentKey);
        // }

        int64_t nowTime = 0;
        if (isreal){
            nowTime = GetCurrentTimeUs();
        } else {
            nowTime = quantSpread.generateTs;
        }

        bool openOrderFlag = false;
        bool curDelay = false;
        bool curDepthDelay = false;
        bool curTradeDelay = false;

        int64_t timeVal = nowTime - quantSpread.generateTs;
        if (timeVal < curSpreadDelay) {
            curDelay = true;
        } else {
            LOG_INFO("OnSpread delay > {} ---  nowTime:{}  generateTs:{}  now - genetateTs: {} pairInstrumentKey:{}", curSpreadDelay, nowTime, quantSpread.generateTs, timeVal, quantSpread.pairInstrumentKey);
        }

        int64_t timeDepthVal = nowTime - min(quantSpread.activeDepthTs, quantSpread.passiveDepthTs);
        if (timeDepthVal < curSpreadDepthDelay) {
            curDepthDelay = true;
        } else {
            LOG_INFO("OnSpread depth delay > {} ---  nowTime:{}  ativeDepthTs:{}  passiveDepthTs:{} timeDepthVal:{} pairInstrumentKey:{}", curSpreadDepthDelay, nowTime, quantSpread.activeDepthTs, quantSpread.passiveDepthTs, timeDepthVal, quantSpread.pairInstrumentKey);
        }

        if (quantSpread.exchActiveTradeDelay < tradesDelayThreshold && quantSpread.exchPassiveTradeDelay < tradesDelayThreshold) {
            curTradeDelay = true;
        }
        else if (timeDepthVal > curSpreadDepthDelay) {
            curTradeDelay = true;
        } else {
            LOG_INFO("OnSpread depth tradesdelay exchActiveTradeDelay:{} exchPassiveTradeDelay:{} tradesDelayThreshold:{} pairInstrumentKey:{}", quantSpread.exchActiveTradeDelay, quantSpread.exchPassiveTradeDelay, tradesDelayThreshold, quantSpread.pairInstrumentKey);
        }

        openOrderFlag = curDelay && curDepthDelay && curTradeDelay;


        auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
        for (auto it = allAlgoOrders.begin(); it != allAlgoOrders.end(); ++it) {
            BaseAlgoOrder* pAlgoOrder = it->second;

            if (strcmp(pAlgoOrder->pairInstrumentKey, quantSpread.pairInstrumentKey) == 0) {
                pAlgoOrder->CancelOrderOnSpread(quantSpread, eventTime); // 执行撤单逻辑

                pAlgoOrder->posMgrMakerTaker.UpdateAccountOnMarketDepth(activeDepth);  // 更新floatAmount
                pAlgoOrder->posMgrTakerTaker.UpdateAccountOnMarketDepth(activeDepth);  // 更新floatAmount
                pAlgoOrder->posMgrMakerTaker.UpdateAccountOnMarketDepth(passiveDepth);
                pAlgoOrder->posMgrTakerTaker.UpdateAccountOnMarketDepth(passiveDepth);

                if (pAlgoOrder->algoOrderStatus != stra::OrderStatus_NEW && pAlgoOrder->algoOrderStatus != stra::OrderStatus_PARTFILLED) { // algoOrderStatus;  // New Cancelling Canceled Filled Fault
                    continue;
                }

                if (pAlgoOrder->algoType == stra::AlgoType_PairTrading && openOrderFlag) {  // 执行拆单报单逻辑
                    // 配对单Onspread逻辑
                    // 如果订单与spread相关
                    // 检查行情有效性
                    if (quantSpread.spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
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
                                            WritePairOrder(pairOrder, quantSpread);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {  
                                                double assetTick;
                                                stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);
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
                                            //if (quantOrder.orderStatus == stra::OrderStatus_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED) {
                                            if (quantOrder.orderStatus == stra::OrderStatus_PEND_NEW || quantOrder.orderStatus == stra::OrderStatus_PENDING_NEW || quantOrder.orderStatus == stra::OrderStatus_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED) {
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
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
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
                                            WritePairOrder(pairOrder, quantSpread);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                double assetTick;
                                                stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);
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
                                            //if (quantOrder.orderStatus == stra::OrderStatus_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED) {
                                            if (quantOrder.orderStatus == stra::OrderStatus_PEND_NEW || quantOrder.orderStatus == stra::OrderStatus_PENDING_NEW || quantOrder.orderStatus == stra::OrderStatus_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED) {
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
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
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
                    if (quantSpread.spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
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
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, stra::Direction_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {  
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
                                                    }
                                                }
                                            }
                                        } else{
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, stra::Direction_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {  
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
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
                                    if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, stra::Direction_LONG) < pAlgoOrder->maxMTOrderSize){
                                        // 主动腿多头报单
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, stra::Direction_LONG);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, quantSpread);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                double assetTick;
                                                stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);
                                                }
                                            }
                                        }
                                    }
                                    if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, stra::Direction_SHORT) < pAlgoOrder->maxMTOrderSize){
                                        // 主动腿空头报单
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER, stra::Direction_SHORT);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, quantSpread);
                                            pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                            int64_t strategyOrderId = GenerateStrategyOrderId();
                                            stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                            if (quant_order.strategyOrderId > 0) {
                                                double assetTick;
                                                stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    // 通过验资正常报单
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                } else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);
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
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, stra::Direction_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
                                                    }
                                                }
                                            }
                                        } else {
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, stra::Direction_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
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
                                        if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::MAKER_TAKER, stra::Direction_LONG) < pAlgoOrder->maxMTOrderSize){
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, stra::Direction_LONG);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
                                                    }
                                                }
                                            }
                                        }
                                        if (pAlgoOrder->pairOrderMgr.GetSizeByOrderTypeAndActiveDirection(stra::TAKER_TAKER, stra::Direction_SHORT) < pAlgoOrder->maxMTOrderSize){
                                            PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::TAKER_TAKER, stra::Direction_SHORT);
                                            if (pairOrder.pairId > 0) {
                                                WritePairOrder(pairOrder, quantSpread);
                                                pAlgoOrder->pairOrderMgr.InsertPairOrderByPairOrder(pairOrder);
                                                int64_t strategyOrderId = GenerateStrategyOrderId();
                                                stra::QuantOrder quant_order = pairOrder.CreateOrginActiveOrder(strategyOrderId);
                                                if (quant_order.strategyOrderId > 0) {
                                                    double assetTick;
                                                    stra::InstrumentInfo info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                    bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                    if (verify) {
                                                        // 通过验资正常报单
                                                        bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                        WriteQuantOrder(quant_order, quantSpread);
                                                        if (orderFlag) {
                                                            pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                        }
                                                    } else {
                                                        pAlgoOrder->fundVerifyFailedFlag = true;
                                                        char msg[stra::MSG_LEN];
                                                        sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                        rLarkMsg.Push(msg);
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
                    if (quantSpread.spreadEffective && !pAlgoOrder->systemDelayFlag && !pAlgoOrder->exchangeDelayFlag) {
                        if (!pAlgoOrder->mtSlipageFlag && !pAlgoOrder->mtSpreadFlag) {
                            if (pAlgoOrder->mtRebalanceFlag) {
                                if (pAlgoOrder->pairOrderMgr.GetSizeByOrderType(stra::MAKER_TAKER) == 0) {
                                    bool pass = LimitManager::Instance().PassLimit(pAlgoOrder->activeAccountId);
                                    if (pass) {
                                        PairOrder pairOrder = pAlgoOrder->CreatePairOrder(stra::MAKER_TAKER);
                                        if (pairOrder.pairId > 0) {
                                            WritePairOrder(pairOrder, quantSpread);
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
                                                double assetTick;
                                                stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                }
                                                else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);                                                  sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);  
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
                                            WritePairOrder(pairOrder, quantSpread);
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
                                                double assetTick;
                                                stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(quant_order.instrumentKey);
                                                bool verify = AccountManager::Instance().FundVerify(quant_order, assetTick, info);
                                                if (verify) {
                                                    bool orderFlag = QuantTrade::Instance().CreateOrder(quant_order);
                                                    WriteQuantOrder(quant_order, quantSpread);
                                                    if (orderFlag) {
                                                        pAlgoOrder->UpdateAlgoPairOrderByInsertQuantOrder(quant_order);
                                                    }
                                                }
                                                else {
                                                    pAlgoOrder->fundVerifyFailedFlag = true;
                                                    char msg[stra::MSG_LEN];
                                                    sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);                                                  sprintf(msg, "AccountManager FundVerify failed! quant_order  algoPairId:%ld, pairId:%ld instrumentKey:%s direction:%d", quant_order.algoPairId, quant_order.pairId, quant_order.instrumentKey, quant_order.direction);
                                                    rLarkMsg.Push(msg);  
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
        sprintf(msg, "StraException, in AlgoContext::OnSpread error msg:{}", e.what());
        rLarkMsg.Push(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnSpread, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnSpread, errormsg:{}", e.what());
        rLarkMsg.Push(msg);
    }
}

void AlgoContext::OnOrder(stra::TdOrder& order, int64_t eventTime) {
    try {
        int64_t nowTime = 0;
        if (isreal){
            nowTime = GetCurrentTimeUs();
        } else {
            nowTime = eventTime;
        }

        BaseAlgoOrder* pAlgoOrder = alogOrderManager.SeletAlgoOrderByAlgoOrderId(order.algoId);
        if (pAlgoOrder != nullptr) {
            if (pAlgoOrder->algoType == stra::AlgoType_PairTrading || pAlgoOrder->algoType == stra::AlgoType_FishingTrading || pAlgoOrder->algoType == stra::AlgoType_Rebalance) {
                // 是配对单进行配对单的处理
                PairOrder& pairOrder = pAlgoOrder->pairOrderMgr.SelectPairOrderByPairId(order.pairId);
                if (pairOrder.pairId <= 0) {
                    return;
                }
                // 更新od_mgr与quant_order

                stra::QuantSpread quantSpread = SpreadManager::Instance().GetLastSpread(pAlgoOrder->pairInstrumentKey);
                stra::QuantOrder quantOrder = pAlgoOrder->orderMgr.SelectOrderByStrategyOrderId(order.clOrdId);

                if (quantOrder.totalVolumeOnOrder - order.totalVolumeOnOrder > stra::MIN_FLOAT) {  // 过滤乱序的报单
                    return;
                }

                // 订单状态适配
                if (order.apiSource == stra::ApiSource_CANCEL_ORDER && order.orderStatus == stra::OrderStatus_REJECTED) {
                    if (order.exchangType == stra::ET_BYBIT || order.exchangType == stra::ET_BITGET) {
                        order.orderStatus = stra::OrderStatus_REJECTED;
                    }
                    else {
                        order.orderStatus = stra::OrderStatus_FAILED;
                    }
                } else if (order.apiSource == stra::ApiSource_QUERY_ORDER && order.orderStatus == stra::OrderStatus_REJECTED && strlen(quantOrder.exchangeOrderId) > 0){
                    if (order.exchangType == stra::ET_BYBIT || order.exchangType == stra::ET_BITGET) {
                        order.orderStatus = stra::OrderStatus_REJECTED;
                    }
                    else {
                        order.orderStatus = stra::OrderStatus_UNKNOWN;
                    }
                }
                // 订单手续费适配
                stra::InstrumentInfo& info = BasicInfoMgr::GetInstance().GetBasicInfo(quantOrder.instrumentKey);
                double temp_fee_rate = 0.0;
                if (quantOrder.isActiveOrder){
                    // if (order.isMaker)
                    if (quantOrder.orderType == stra::OrderType_POST_ONLY) {
                        temp_fee_rate = pAlgoOrder->activeMakerFeeRate;
                    } else {
                        temp_fee_rate = pAlgoOrder->activeTakerFeeRate;
                    }
                } else {
                    // if (order.isMaker){
                    if (quantOrder.orderType == stra::OrderType_POST_ONLY) {
                        temp_fee_rate = pAlgoOrder->passiveMakerFeeRate;
                    } else {
                        temp_fee_rate = pAlgoOrder->passiveTakerFeeRate;
                    }
                }
                if (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder > stra::MIN_FLOAT){
                    if (quantOrder.instType == stra::InstType_SPOT || quantOrder.instType == stra::InstType_MARGIN){
                        if (quantOrder.direction==stra::Direction_LONG){
                            order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder * order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder * quantOrder.totalPriceOnOrder) / (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder);
                            strncpy(order.lastExecutedTradeFeeCurrency, info.instLeft.c_str(), stra::ASSET_LEN);
                            order.lastExecutedTradeFee = order.lastExecutedVolumeOnOrder * temp_fee_rate;
                        }else{
                            order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder) / (order.totalVolumeOnOrder / order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder / quantOrder.totalPriceOnOrder);
                            strncpy(order.lastExecutedTradeFeeCurrency, info.instRight.c_str(), stra::ASSET_LEN);
                            order.lastExecutedTradeFee = order.lastExecutedVolumeOnOrder * order.lastExecutedPriceOnOrder * temp_fee_rate;
                        }
                    }else{
                        if (info.calculateType == 0){
                            order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder * order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder * quantOrder.totalPriceOnOrder) / (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder);
                            strncpy(order.lastExecutedTradeFeeCurrency, info.margin.c_str(), stra::ASSET_LEN);
                            order.lastExecutedTradeFee = info.multiple * order.lastExecutedVolumeOnOrder * order.lastExecutedPriceOnOrder * temp_fee_rate;
                        }else{
                            order.lastExecutedPriceOnOrder = (order.totalVolumeOnOrder - quantOrder.totalVolumeOnOrder) / (order.totalVolumeOnOrder / order.totalPriceOnOrder - quantOrder.totalVolumeOnOrder / quantOrder.totalPriceOnOrder);
                            strncpy(order.lastExecutedTradeFeeCurrency, info.margin.c_str(), stra::ASSET_LEN);
                            order.lastExecutedTradeFee = info.multiple * order.lastExecutedVolumeOnOrder / order.lastExecutedPriceOnOrder * temp_fee_rate;
                        }
                    }
                }
                // 进行延迟计算与检查
                //LOG_INFO("OnOrder check orderstatus PEND_NEW CANCEL!");
                if (quantOrder.orderStatus == stra::OrderStatus_PEND_NEW || quantOrder.orderStatus == stra::OrderStatus_CANCEL) {
                    pAlgoOrder->systemDelayTimeSpan = 0.8 * pAlgoOrder->systemDelayTimeSpan + 0.2 * (nowTime - quantOrder.updateTime);
                    // if (pAlgoOrder->systemDelayTimeSpan > 20000) {
                    if (pAlgoOrder->systemDelayTimeSpan > 50000) {
                        pAlgoOrder->systemDelayFlag = true;
                        // 进行异常播报
                        char msg[stra::MSG_LEN];
                        sprintf(msg, "strategyName:%s algoOrderId:%ld quantOrder:%ld  systemDelayFlag:%d  systemDelayTimeSpan:%ld", pAlgoOrder->algoStrategyName, pAlgoOrder->algoOrderId, quantOrder.strategyOrderId, pAlgoOrder->systemDelayFlag, pAlgoOrder->systemDelayTimeSpan);
                        rLarkMsg.Push(string(msg));
                    }
                }

                //LOG_INFO("OnOrder check orderstatus PENDING_NEW CANCELLING!");
                if (quantOrder.orderStatus == stra::OrderStatus_PENDING_NEW || quantOrder.orderStatus == stra::OrderStatus_CANCELLING) {
                    pAlgoOrder->exchangeDelayTimeSpan = 0.8 * pAlgoOrder->exchangeDelayTimeSpan + 0.2 * (nowTime - quantOrder.updateTime);
                    int64_t oneSecond = 1000 * 1000;
                    int64_t scd10 = 10 * oneSecond;
                    if (pAlgoOrder->exchangeDelayTimeSpan > scd10){
                        pAlgoOrder->exchangeDelayFlag = true;
                        // 进行异常播报
                        char msg[stra::MSG_LEN];
                        sprintf(msg, "strategyName:%s algoOrderId:%ld quantOrder:%ld exchangeDelayFlag:%d  exchangeDelayTimeSpan:%ld", pAlgoOrder->algoStrategyName, pAlgoOrder->algoOrderId, quantOrder.strategyOrderId, pAlgoOrder->exchangeDelayFlag, pAlgoOrder->exchangeDelayTimeSpan);
                        rLarkMsg.Push(string(msg));
                    }
                }

                //LOG_INFO("OnOrder start update order!");
                // 开始更新订单
                if (order.apiSource == stra::ApiSource_QUERY_ORDER) {
                    quantOrder = pAlgoOrder->orderMgr.UpdateOrderOnQueryOrder(order, eventTime);
                } else {
                    quantOrder = pAlgoOrder->orderMgr.UpdateOrderOnOrder(order, eventTime);
                }


                //LOG_INFO("OnOrder write quant order!");
                if (quantOrder.strategyOrderId > 0) {
                    WriteQuantOrder(quantOrder, quantSpread); // 行情数据写入
                }
                
                //LOG_INFO("OnOrder start update algoPairOrderByQuantOrder!");
                // 更新algo_order的ps_mgr与pair_order
                pAlgoOrder->UpdateAlgoPairOrderByQuantOrder(quantOrder, eventTime);
                if (quantOrder.orderStatus == stra::OrderStatus_FILLED || quantOrder.orderStatus == stra::OrderStatus_REJECTED || quantOrder.orderStatus == stra::OrderStatus_CANCELED) {
                    // 订单完结解冻
                    //LOG_INFO("OnOrder start update algoPairOrderByDeleteQuantOrder!");
                    pAlgoOrder->UpdateAlgoPairOrderByDeleteQuantOrder(quantOrder, eventTime);
                    //LOG_INFO("OnOrder start orderStatus filled rejected canceled PairOrderTrade!");
                    pAlgoOrder->PairOrderTrade(pairOrder, eventTime);

                    // algoOrder保存
                    // pAlgoOrder->SaveToFile();
                    // WriteAlgoPairOrder(algoOrder);
                } else {
                    // 主动腿未完结有成交则需要进行被动腿报单
                    //LOG_INFO("OnOrder start orderStatus not in filled rejected canceled PairOrderTrade!");
                    if (quantOrder.isActiveOrder && quantOrder.tradeVolume > stra::MIN_FLOAT){
                        pAlgoOrder->PairOrderTrade(pairOrder, eventTime);
                    }
                }
                //LOG_INFO("OnOrder Update end!");
                if (quantOrder.isActiveOrder) {
                    if (quantOrder.orderStatus == stra::OrderStatus_NEW || quantOrder.orderStatus == stra::OrderStatus_PARTFILLED) {
                        pAlgoOrder->CancelOrderOnSpread(quantSpread, eventTime); // 执行撤单逻辑
                    }
                }
            }
        }
    } catch(StraException& e) {
        LOG_INFO("StraException in AlgoContext::OnOrder, error msg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException in AlgoContext::OnOrder, error msg:{}", e.what());
        rLarkMsg.Push(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnOrder, errormsg:{}", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnOrder, errormsg:{}", e.what());
        rLarkMsg.Push(msg);
    }
}

void AlgoContext::OnPosition(const stra::TdPosition& position, int64_t eventTime) {

}

void AlgoContext::OnKline() {

}

void AlgoContext::OnFundingRate() {

}

void AlgoContext::OnTimerTrade(int64_t eventTime) {
    auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
    for (auto it = allAlgoOrders.begin(); it != allAlgoOrders.end(); ++it) {
        BaseAlgoOrder* pAlgoOrder = it->second;
        const stra::QuantSpread& quantSpread = SpreadManager::Instance().GetLastSpread(pAlgoOrder->pairInstrumentKey);
        if (quantSpread.generateTs > 0) {
            OnSpreadTrade(quantSpread, eventTime);
        }
    }
}

void AlgoContext::OnTimer(int64_t eventTime) {
    // 延迟检查，订单从发出到回报的延迟时间作为一个变量存起来，超过标准需要报警
    // 杠杆检查，accountMgr杠杆过高检查，超过标准需要报警。未来在极端情况下强制进行自动减仓
    // 订单异常检查
    try {
        if (onTimerTrade) {
            OnTimerTrade(eventTime);
        }

        bool deleteAlgoOrderFlag = false;
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
            auto& orderMgr = it->second->orderMgr;
            auto& allOrders = orderMgr.GetAllOrders();
            auto& allTransfers = orderMgr.GetAllTransfers();

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
                it->second->PairOrderTrade(ia->second, eventTime);
            }
            
            // 行情检查
            stra::QuantSpread quantSpread = SpreadManager::Instance().GetLastSpread(it->second->pairInstrumentKey);
            if (eventTime - quantSpread.generateTs > 30 * second1 && mSpreadReportCount[it->second->pairInstrumentKey] > 60) {
                char msg[stra::MSG_LEN];
                sprintf(msg, "check spread data !!! strategyName:%s algoOrderId:%ld pairInstrumentKey:%s  eventTime:%ld  quantSpread.generateTs:%ld", it->second->algoStrategyName, it->second->algoOrderId, it->second->pairInstrumentKey, eventTime, quantSpread.generateTs);
                rLarkMsg.Push(msg);
                LOG_INFO("Spread {}", msg);
                it->second->commandType = stra::CommandType_ERROR;
                it->second->algoOrderStatus = stra::OrderStatus_ERRORCANCELLING;
                it->second->updateTime = eventTime;
            }

            // 异常订单检查
            for (auto iu = allOrders.begin(); iu != allOrders.end(); ++iu) {
                auto& order = iu->second;
                if (order.orderStatus == stra::OrderStatus_CANCEL || order.orderStatus == stra::OrderStatus_CANCELLING || order.orderStatus == stra::OrderStatus_PENDING_NEW || order.orderStatus == stra::OrderStatus_PEND_NEW) {
                    if (eventTime - order.updateTime > second1 * 5) {
                        // LOG_INFO("lark alarm! quantOrder: %s", order.GetStr().c_str());
                        // 异步lark报警
                        if (stuckOrderReportCount > 60){
                            char msg[stra::MSG_LEN];
                            sprintf(msg, "check stuck order !!! strategyName:%s algoOrderId:%ld strategyOrderId:%ld  instrumentKey:%s orderStatus:%s posDirection:%s direction:%s", it->second->algoStrategyName, it->second->algoOrderId, order.strategyOrderId, order.instrumentKey, stra::OrderStatusEnum2Str[order.orderStatus].c_str(), stra::PosDirectionEnum2Str[order.posDirection].c_str(), stra::DirectionEnum2Str[order.direction].c_str());
                            rLarkMsg.Push(msg);
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
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                        }

                        if (pass) {
                            order.queryCount += 1;
                        }
                    }
                } else if(order.orderStatus == stra::OrderStatus_UNKNOWN) {
                    if (eventTime - order.updateTime > second1 * 15) {
                        // LOG_INFO("lark alarm! quantOrder: %s", order.GetStr().c_str());
                        // 异步lark报警
                        if (errorOrderReportCount > 60){
                            char msg[stra::MSG_LEN];
                            sprintf(msg, "check unknown order !!! strategyName:%s algoOrderId:%ld strategyOrderId:%ld  orderStatus:%s", it->second->algoStrategyName, it->second->algoOrderId, order.strategyOrderId, stra::OrderStatusEnum2Str[order.orderStatus].c_str());
                            rLarkMsg.Push(msg);
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
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                        }
                    }
                }
            }
            // 结束订单检查
            if (it->second->ttOLSwitch == false && it->second->ttOSSwitch == false && it->second->mtOLSwitch == false && it->second->mtOSSwitch == false) {
                double price = DataManager::Instance().GetMidPrice(it->second->activeInstrumentKey);
                if (price > 0) {
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
                            it->second->algoOrderStatus = stra::OrderStatus_FILLED;
                            it->second->commandType = stra::CommandType_FINISHED;
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                        }

                    }
                    else {
                        orderAmount = fabs(it->second->pairTotalVolume);
                        if (orderAmount < it->second->activeInfo.minSize && allPairOrders.size() == 0) {
                            //
                            //LOG_INFO("activeInstrumentKey:%s orderAmount:%f  activeInfo.minSize:%f  multiple:%f", it->second->activeInstrumentKey, orderAmount, it->second->activeInfo.minSize, it->second->activeInfo.multiple);
                            it->second->algoOrderStatus = stra::OrderStatus_FILLED;
                            it->second->commandType = stra::CommandType_FINISHED;
                            it->second->updateTime = eventTime;
                            // 不满足最小报单量,不会报pairOrder了,这时候订单终止,返回交易结果
                            string pubMsg = it->second->GeneratePubStr();
                            QuantPub::Instance().Publish(pubMsg);
                            rLarkMsg.Push(pubMsg);
                            WriteAlgoOrder(it->second);
                            deleteAlgoOrderFlag = true;
                        }    
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
                rLarkMsg.Push(info);
                // string pubMsg = it->second->GeneratePubStr();
                // rLarkMsg.Push(pubMsg);
            }
            
            // delete cancled status algoorder
            //LOG_INFO("algoId:%ld pairInstrumentKey:%s allPairOrders.size: %d  algoOrderStatus:%d", it->second->algoOrderId, it->second->pairInstrumentKey, allPairOrders.size(), int(it->second->algoOrderStatus));
            /*
	    if (allPairOrders.size() > 0 && it->second->algoOrderStatus == stra::OrderStatus_CANCELLING) {
                stringstream ss;
                for (auto iter = allPairOrders.begin(); iter != allPairOrders.end(); ++iter) {
                    ss << "pairId:" << iter->second.pairId << " activeInstrumentKey:" << iter->second.activeInstrumentKey << " tradingTypeOrder:" << iter->second.tradingTypeOrder << " tradingTypeOffset:" << iter->second.tradingTypeOffset;
                }
                LOG_INFO("OrderStatus_CANCELLING --- %s", ss.str().c_str());
            }
	    */

            if (allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::OrderStatus_CANCELLING) {
                it->second->commandType = stra::CommandType_CANCELED;
                it->second->algoOrderStatus = stra::OrderStatus_CANCELED;
                string pubMsg = it->second->GeneratePubStr();
                QuantPub::Instance().Publish(pubMsg);
                rLarkMsg.Push(pubMsg);
                deleteAlgoOrderFlag = true;
            } else if (allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::OrderStatus_ERRORCANCELLING) {
                it->second->algoOrderStatus = stra::OrderStatus_ERRORCANCELED;
                string pubMsg = it->second->GeneratePubStr();
                QuantPub::Instance().Publish(pubMsg);
                rLarkMsg.Push(pubMsg);
                deleteAlgoOrderFlag = true;
            }

            if (deleteAlgoOrderFlag) {
                vUnSubscribePairInstId.push_back(it->second->pairInstrumentKey);

                if (it->second) {
                    delete it->second;
                    it->second = nullptr;
                }
                allAlgoOrders.erase(it++);
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
                rLarkMsg.Push(algoOrderStr);
            } else {
                rLarkMsg.Push("There are no algo orders!");
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
                SpreadManager::Instance().DeleteQuantSpread(vUnSubscribePairInstId[i]);
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
        LOG_INFO("StraException in AlgoContext::OnTimer, error msg:%s", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "StraException in AlgoContext::OnTimer, error msg:%s", e.what());
        rLarkMsg.Push(msg);
    } catch (exception& e) {
        LOG_INFO("some errors has happened in AlgoContext::OnTimer, errormsg:%s", e.what());
        char msg[stra::MSG_LEN];
        sprintf(msg, "some errors has happened in AlgoContext::OnTimer, errormsg:%s", e.what());
        rLarkMsg.Push(msg);
    }

    // 如果多次查询仍无法同步订单状态，有订单丢失的情况，则需要停止algoOrder,返回回报并将algoOrder从algoOrderMgr中删除，回报时订单状态为Fault

    // 查询资金持仓, 按照algo_pair_order的account_id一个一个查一个一个更新, 查询有回报后这个帐号才可以交易(才可以进行验资)

    // 另外,algoOrder增加一个暂停报单的属性, 每次停止交易若干秒，这个锁在on_timer里面进行解锁


    // rebalanceFlag设置
}

void AlgoContext::OnBalance(const stra::TdBalance& balance) {
    AccountManager::Instance().OnBalance(balance);
}

void AlgoContext::OnPosition(const stra::TdPosition& position) {
    AccountManager::Instance().OnPosition(position);
}

void AlgoContext::OnTotalAccount(const stra::TdTotalAccount& totalAccount) {
    AccountManager::Instance().OnTotalAccount(totalAccount);
}

// 持久化
// algoOrder在每次创建和终结一个pairOrder时需要持久化
// pairOrder在被创建和终结时需要持久化
// quantOrder在被创建和终结时需要持久化

BaseAlgoOrder* AlgoContext::GetAlgoOrder(int64_t algoOrderId) {
    return alogOrderManager.SeletAlgoOrderByAlgoOrderId(algoOrderId);
}
