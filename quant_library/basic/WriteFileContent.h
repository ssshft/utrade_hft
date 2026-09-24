#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/stat.h>

#include "Utility.h"

class WriteFileContent {
private:
    static constexpr int     kTypeCount        = 3;          // content.type 的取值范围
    static constexpr size_t  kBufSize          = 256 * 1024; // 256KB
    static constexpr int64_t kFlushIntervalUs  = 200 * 1000; // 200ms

    struct FileSink {
        std::ofstream f;
        std::string path;
        std::vector<char> buf;          // ★ 必须由我们自己持有，pubsetbuf 不接管所有权
        bool opened{false};
        bool headerWritten{false};
        int64_t lastFlushUs{0};
    };

    FileSink mSinks[kTypeCount];        // 下标 = content.type - 1
    std::string mCurDate;               // 当前文件日期，只有跨天才重算

    std::atomic<bool> running{true};
    thread* runningThread{nullptr};

    WriteFileContent() {
        runningThread = new thread(&WriteFileContent::Run, this);
    }

    ~WriteFileContent() {
        Stop();
    }

public:

    static WriteFileContent& GetInstance() {
        static WriteFileContent writeFileContent;
	    return writeFileContent;
    }

    std::string SuffixOf(int ty) {
        if (ty == 1) {
            return "_quantOrder.csv";
        }
        else if (ty == 2) {
            return "_pairOrder.csv";
        }
        else if (ty == 3) {
            return "_algoOrder.csv";
        }
        return "";
    }

    std::string HeaderOf(int ty) {
        if (ty == 1) {
            return "strategyName,strategyOrderId,systemOrderId,exchangeOrderId,instrumentKey,"
		    "orderType,direction,orderStatus,"
            "targetPrice,price,volume,totalPriceOnOrder,totalVolumeOnOrder,tradeVolume,"
            "activeBidPrice1,activeBidVolume1,activeAskPrice1,activeAskVolume1,"
            "passiveBidPrice1,passiveBidVolume1,passiveAskPrice1,passiveAskVolume1,"
		    "updateTime,errorId,originErrorMsg,reduceOnly," 
            "pairId,algoPairId,isActiveOrder,rebalance,generateTs,activeDepthTs,passiveDepthTs,activeDepthDelay,passiveDepthDelay";
        }
        else if (ty == 2) {
            return "pairId,algoPairId,strategyName,baseAsset,tradingTypeOrder,tradingTypeOffset,targetVolume,"
                "activeInstrumentKey,activeDirection,activeTargetPrice,activeBidPrice1,activeBidVolume1,activeAskPrice1,activeAskVolume1,"
                "passiveInstrumentKey,passiveDirection,passiveTargetPrice,passiveBidPrice1,passiveBidVolume1,passiveAskPrice1,passiveAskVolume1,"
                "spreadBidAsk,spreadBidBid,spreadAskBid,spreadAskAsk,generateTs,activeDepthTs,passiveDepthTs,activeDepthDelay,passiveDepthDelay,"
                "activeTotalPriceOnOrder,activeTotalVolumeOnOrder,passiveTotalPriceOnOrder,passiveTotalVolumeOnOrder,"
                "pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,"
                "activeFrozenPrice,activeFrozenVolume,passiveFrozenPrice,passiveFrozenVolume,"
                "activeAccountId,passiveAccountId,status,rebalanceFlag,updateTime,createTime,pairTargetSpread";
        }
        else if (ty == 3) {
            return "algoType,algoStrategyName,algoOrderId,pairInstrumentKey,baseAsset,algoOrderStatus,"
                "activeInstrumentKey,activePriceTakerPct,activePriceMakerPct,activeAccountId,activeDriveType,activeDepthMakerCheck,activeDepthTakerCheck,activeDepthMakerCheckType,activeDepthTakerCheckType,activeOrderType,"
                "passiveInstrumentKey,passivePriceTakerPct,passivePriceMakerPct,passiveAccountId,passiveDriveType,passiveDepthMakerCheck,passiveDepthTakerCheck,passiveDepthMakerCheckType,passiveDepthTakerCheckType,passiveOrderType,"
                "passiveVolumePct,activeMakerCancelOrderTime,activeTakerCancelOrderTime,passiveMakerCancelOrderTime,passiveTakerCancelOrderTime,activePassiveCancelOrderPct,activeMakerCancelOrderPct,activeTakerCancelOrderPct,passiveMakerCancelOrderPct,passiveTakerCancelOrderPct,"
                "activeMakerFeeRate,activeTakerFeeRate,passiveMakerFeeRate,passiveTakerFeeRate,activeTakerSlippage,activeMakerSlippage,passiveTakerSlippage,passiveMakerSlippage,"
                "pairActiveTotalPrice,pairTotalVolume,pairPassiveTotalPrice,"
                "makerTakerFs,takerTakerFs,maxMTOrderSize,maxTTOrderSize,"
                "targetSpreadType,activeVolumeCalcualteType,ttTargetVolume,mtTargetVolume,fishingSlippagePct,activeTrade";
        }
        return "";
    }

    int64_t RecordTimeUs(const content& c) {
        int64_t t = 0;
        if (c.type == 1) {
            t = c.quantOrder.updateTime;
        }
        else if (c.type == 2) {
            t = c.pairOrder.updateTime;
        }
        else if (c.type == 3) {
            t = c.algoOrder.updateTime;
        }
        // 兜底：时间戳为 0 时 CovertToUtcDate 会返回 "0"，文件名会变成 "0_xxx.csv"，
        // 而且下一条正常记录又会把文件轮转回来，来回 close/open
        return (t > 0) ? t : crypto::getCurrentTime();
    }

    void RotateAll(const std::string& newDate) {
        for (FileSink& s : mSinks) {
            if (s.opened) {
                s.f.flush();
                s.f.close();
                s.opened = false;
            }
            s.headerWritten = false;
            s.f.clear();          // ★ close() 之后必须 clear()，否则下次 open() 会静默失败
        }
        mCurDate = newDate;
    }

    std::string FormatContent(const content& c) {
        if (c.type == 1) { // quantOrder
            const stra::QuantOrderRecord& r = c.quantOrder;
            const std::string& s = fmt::format(
                "{},{},{},{},{},{},{},{},"      // strings + enums
                "{},{},{},{},{},{},"  // targetPrice ~ totalVolumeOnOrder
                "{},{},{},{},"                  // spread.activeBidPrice1 ~ spread.activeAskVolume1
                "{},{},{},{},"                  // spread.passiveBidPrice1 ~ spread.passiveAskVolume1
                "{},{},{},{},{},{},{},{},{},{},{},{},{}", // remaining fields
                r.strategyName,
                r.strategyOrderId,
                r.systemOrderId,
                r.exchangeOrderId,
                r.instrumentKey,
                OrderTypeEnum2StrMap[OrderType(r.orderType)],
                DirectionEnum2StrMap[Direction(r.direction)],
                OrderStatusEnum2StrMap[OrderStatus(r.orderStatus)],
            
                r.targetPrice,
                r.price,
                r.volume,
                r.totalPriceOnOrder,
                r.totalVolumeOnOrder,
                r.tradeVolume,
            
                r.dbp.activeBidPrice1,
                r.dbp.activeBidVolume1,
                r.dbp.activeAskPrice1,
                r.dbp.activeAskVolume1,

                r.dbp.passiveBidPrice1,
                r.dbp.passiveBidVolume1,
                r.dbp.passiveAskPrice1,
                r.dbp.passiveAskVolume1,
                
                r.updateTime,    
                r.errorId,
                r.originErrorMsg,
                r.reduceOnly,
            
                r.pairId,
                r.algoPairId,
                r.isActiveOrder,
                r.rebalance,
            
                r.dbp.generateTs,
                r.dbp.activeDepthTs,
                r.dbp.passiveDepthTs,
                r.dbp.activeDepthDelay,
                r.dbp.passiveDepthDelay
            );

            return s;
        }
        else if (c.type == 2) { // pairOrder
            const stra::PairOrderRecord& r = c.pairOrder;
            const std::string& s = fmt::format(
                // 1-7
                "{},{},{},{},{},{},{}," 
                // 8-14
                "{},{},{},{},{},{},{}," 
                // 15-21
                "{},{},{},{},{},{},{}," 
                // 22-25
                "{},{},{},{}," 
                // 26-30
                "{},{},{},{},{},"
                // 31-41 (11 doubles)
                "{},{},{},{},{},{},{},{},{},{},{},"
                // 42-48 (6 integers/long + 1 double)
                "{},{},{},{},{},{},{}",
                // ---- 参数 1 ~ 48 ----
                r.pairId,                         // 1  %ld
                r.algoPairId,                     // 2  %ld
                r.strategyName,                   // 3  %s
                r.baseAsset,                      // 4  %s
                stra::TradingTypeEnum2Str[stra::TradingType(r.tradingTypeOrder)],   // 5  %s
                stra::TradingTypeEnum2Str[stra::TradingType(r.tradingTypeOffset)],  // 6  %s
                r.targetVolume,                   // 7  %.13f
            
                r.activeInstrumentKey,            // 8  %s
                DirectionEnum2StrMap[Direction(r.activeDirection)],     // 9  %s
                r.activeTargetPrice,              //10  %.13f

                r.dbp.activeBidPrice1,               //11  %.13f
                r.dbp.activeBidVolume1,              //12  %.13f
                r.dbp.activeAskPrice1,               //13  %.13f
                r.dbp.activeAskVolume1,              //14  %.13f
            
                r.passiveInstrumentKey,           //15  %s
                DirectionEnum2StrMap[Direction(r.passiveDirection)],    //16  %s
                r.passiveTargetPrice,             //17  %.13f
                r.dbp.passiveBidPrice1,              //18  %.13f
                r.dbp.passiveBidVolume1,             //19  %.13f
                r.dbp.passiveAskPrice1,              //20  %.13f
                r.dbp.passiveAskVolume1,             //21  %.13f
            
                r.dbp.spreadBidAsk,                  //22  %.13f
                r.dbp.spreadBidBid,                  //23  %.13f
                r.dbp.spreadAskBid,                  //24  %.13f
                r.dbp.spreadAskAsk,                  //25  %.13f
            
                r.dbp.generateTs,                    //26  %ld (or suitable integer)
                r.dbp.activeDepthTs,                 //27  %ld
                r.dbp.passiveDepthTs,                //28  %ld
                r.dbp.activeDepthDelay,              //29  %ld
                r.dbp.passiveDepthDelay,             //30  %ld
            
                r.activeTotalPriceOnOrder,        //31  %.13f
                r.activeTotalVolumeOnOrder,       //32  %.13f
                r.passiveTotalPriceOnOrder,       //33  %.13f
                r.passiveTotalVolumeOnOrder,      //34  %.13f
                r.pairTotalVolume,                //35  %.13f
                r.pairActiveTotalPrice,           //36  %.13f
                r.pairPassiveTotalPrice,          //37  %.13f
                r.activeFrozenPrice,              //38  %.13f
                r.activeFrozenVolume,             //39  %.13f
                r.passiveFrozenPrice,             //40  %.13f
                r.passiveFrozenVolume,            //41  %.13f
            
                r.activeAccountId,                //42  %ld (or %d as appropriate)
                r.passiveAccountId,               //43  %ld
                r.status,                         //44  %d
                r.rebalanceFlag,                  //45  %d
                r.updateTime,                     //46  %ld
                r.createTime,                     //47  %ld
            
                r.pairTargetSpread                //48  %.13f
            );

            return s;
        }
        else if (c.type == 3) {
            const stra::AlgoOrderRecord& r = c.algoOrder;
            const std::string& s = fmt::format(
                // --- 第一段 ---
                "{},{},{},{},{},{},"                          // 6 字段（字符串/整数）
                // --- 第二段 ---
                "{},{},{},{},{},{},{},{},{},{},"   // 10 字段 
                // --- 第三段 ---
                "{},{},{},{},{},{},{},{},{},{},"   // 10 字段
                // --- 第四段 ---
                "{},{},{},{},{},{},{},{},{},{},"   // 10 字段
                // --- 第五段 ---
                "{},{},{},{},{},{},{},{}," // 8 字段
                // --- 第六段 ---
                "{},{},{},"                    // 3 字段
                // --- 第七段 ---
                "{},{},{},{},"            // 4 字段
                // --- 第八段 ---
                "{},{},{},{},{},{}",                      // 4 字段
            
                // 第一段
                stra::AlgoTypeEnum2Str[stra::AlgoType(r.algoType)],
                r.algoStrategyName,
                r.algoOrderId,
                r.pairInstrumentKey,
                r.baseAsset,
                stra::AlgoOrderStatusEnum2Str[stra::AlgoOrderStatus(r.algoOrderStatus)],
            
                // 第二段
                r.activeInstrumentKey,
                r.activePriceTakerPct,
                r.activePriceMakerPct,
                r.activeAccountId,
                stra::DriveTypeEnum2Str[stra::DriveType(r.activeDriveType)],
                r.activeDepthMakerCheck,
                r.activeDepthTakerCheck,
                stra::CheckTypeEnum2Str[stra::CheckType(r.activeDepthMakerCheckType)],
                stra::CheckTypeEnum2Str[stra::CheckType(r.activeDepthTakerCheckType)],
                OrderTypeEnum2StrMap[OrderType(r.activeOrderType)],
            
                // 第三段
                r.passiveInstrumentKey,
                r.passivePriceTakerPct,
                r.passivePriceMakerPct,
                r.passiveAccountId,
                stra::DriveTypeEnum2Str[stra::DriveType(r.passiveDriveType)],
                r.passiveDepthMakerCheck,
                r.passiveDepthTakerCheck,
                stra::CheckTypeEnum2Str[stra::CheckType(r.passiveDepthMakerCheckType)],
                stra::CheckTypeEnum2Str[stra::CheckType(r.passiveDepthTakerCheckType)],
                OrderTypeEnum2StrMap[OrderType(r.passiveOrderType)],
            
                // 第四段
                r.passiveVolumePct,
                r.activeMakerCancelOrderTime,
                r.activeTakerCancelOrderTime,
                r.passiveMakerCancelOrderTime,
                r.passiveTakerCancelOrderTime,
                r.activePassiveCancelOrderPct,
                r.activeMakerCancelOrderPct,
                r.activeTakerCancelOrderPct,
                r.passiveMakerCancelOrderPct,
                r.passiveTakerCancelOrderPct,
            
                // 第五段
                r.activeMakerFeeRate,
                r.activeTakerFeeRate,
                r.passiveMakerFeeRate,
                r.passiveTakerFeeRate,
                r.activeTakerSlippage,
                r.activeMakerSlippage,
                r.passiveTakerSlippage,
                r.passiveMakerSlippage,
            
                // 第六段
                r.pairActiveTotalPrice,
                r.pairTotalVolume,
                r.pairPassiveTotalPrice,
            
                // 第七段
                r.makerTakerFs,
                r.takerTakerFs,
                r.maxMTOrderSize,
                r.maxTTOrderSize,
            
                // 第八段
                stra::TargetSpredPriceEnum2Str[stra::TargetSpredPrice(r.targetSpreadType)],
                stra::ActiveVolumeCalcualteTypeEnum2Str[stra::ActiveVolumeCalcualteType(r.activeVolumeCalcualteType)],
                r.ttTargetVolume,
                r.mtTargetVolume,
                r.fishingSlippagePct,
                r.activeTrade
            );

            return s;
        }
        return "";
    }

    void WriteFile(const content& c) {
        // type 越界保护：mSinks 只有 kTypeCount 个，越界会写坏相邻内存
        if (c.type < 1 || c.type > kTypeCount) {
            LOG_ERROR("WriteFileContent invalid content type: {}", c.type);
            return;
        }

        // ① 用「记录自身的时间」选文件（不是写线程的当前时刻）
        std::string date = CovertToUtcDate(RecordTimeUs(c));
        if (date != mCurDate) {
            RotateAll(date);                       // 关掉全部 sink、清 headerWritten
        }

        // ② 取/建对应的 sink
        FileSink& sink = mSinks[c.type - 1];
        if (!sink.opened) {
            OpenSink(sink, c.type, date);
        }

        // ③ 表头只写一次（★ 别忘了换行，否则表头会和第一条数据粘在同一行）
        if (!sink.headerWritten) {
            sink.f << HeaderOf(c.type) << "\n";
            sink.headerWritten = true;
        }

        // ④ 写内容（只进内存缓冲区）
        sink.f << FormatContent(c) << "\n";

        // ⑤ 定期 flush，而不是每条 close
        int64_t now = crypto::getCurrentTime();
        if (now - sink.lastFlushUs > kFlushIntervalUs) {
            sink.f.flush();
            sink.lastFlushUs = now;
        }
    }

    void OpenSink(FileSink& sink, int type, const std::string& date) {
        sink.path = date + SuffixOf(type);                 // "_quantOrder.csv" 等

        // ★ pubsetbuf 必须在 open() 之前调用，之后调用无效（常见坑）
        sink.buf.resize(kBufSize);
        sink.f.rdbuf()->pubsetbuf(sink.buf.data(), sink.buf.size());

        sink.f.open(sink.path.c_str(), std::ios::app | std::ios::binary);
        sink.opened = true;

        // ★ 用文件大小判断要不要写表头，替掉每条的 access()
        struct stat st;
        sink.headerWritten = (::stat(sink.path.c_str(), &st) == 0 && st.st_size > 0);
    }

    void Stop() {
        running = false;
        if (runningThread) {
            if (runningThread->joinable()) {
                runningThread->join();
            }
            delete runningThread;
            runningThread = nullptr;
        }
        for (FileSink& s : mSinks) {
            if (s.opened) {
                s.f.flush();
                s.f.close();
                s.opened = false;
            }
        }
    }

    void Run() {
        while (running) {
            bool idle = true;
            content c;
            // 一次把队列排空，而不是每条睡 1ms（否则上限只有约 1000 条/秒，积压后永远追不上）
            while (contentQueue.Pop(c)) {
                idle = false;
                try {
                    WriteFile(c);
                } catch (const std::exception& e) {
                    // 不要空 catch：格式化/写文件失败必须能看到
                    LOG_ERROR("WriteFileContent failed, type:{} err:{}", c.type, e.what());
                }
            }
            if (idle) {
                usleep(1000);
            }
        }
    }
};
