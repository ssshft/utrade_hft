// Stub for the external dbp (spread feed) headers (dbp/sig/dbp/include.h).
//
// ⚠️ DbpData 的字段**逐字照抄**真实头（含数组维度 [5]，旧桩写成 [8] 是错的），
//    只去掉 `: sp::RNB` / `: sp::CNB` 这两个基类 —— 它们来自 shmpool 的
//    `sp::` 命名空间，本机没有那套头文件，而策略侧和执行侧都没有用到基类里的成员。
//    这是本文件唯一一处**刻意的不一致**，写在这里以免下次误以为桩是全等的。
//
//    DbpTopic 同理：真实的是 `struct DbpTopic : sp::CNB`，名字由基类提供；
//    桩里用一个平铺的 `__name` 代替。
#pragma once

#include <cstdint>

namespace dbp {

const uint32_t DBP_COL_SIZE = 500;

// 价比1，价比2，价差1，价差2
enum SpreadCalcType {
    SPCT_PRICEDIV1,
    SPCT_PRICEDIV2,
    SPCT_PRICESUB1,
    SPCT_PRICESUB2
};

enum SpreadType {
    SPT_BID1ASK1,
    SPT_BID1BID1,
    SPT_ASK1ASK1,
    SPT_ASK1BID1
};

enum SpreadDrive {
    SPD_ALL,
    SPD_LEFT,
    SPD_RIGHT
};

struct DbpTopic {
    char __name[256]{""};

    char activeInstrumentKey[128]{""};
    char passiveInstrumentKey[128]{""};

    int activeDepth1DBWID{-1};
    int activeFundingRateDBWID{-1};
    int activeTradesDBWID{-1};

    int passiveDepth1DBWID{-1};
    int passiveFundingRateDBWID{-1};
    int passiveTradesDBWID{-1};

    SpreadDrive spreadDrive{SPD_ALL};
    SpreadType spreadType{SPT_BID1ASK1};
    SpreadCalcType spreadCalcType{SPCT_PRICEDIV1};

    double activeMultiply{1};
    double passiveMultiply{1};
    uint64_t activeCheckspan{30};
    uint64_t passivecCheckspan{30};

    bool skipdata{false};
    uint32_t stematime{0};
    uint32_t tematime{0};
    uint32_t maxmintime{0};
    uint32_t timsspan{0};
};

struct DbpData {
    bool spreadEffective{false};  // 价差是否有效
    bool statEffective{false};    // 统计量是否有效

    // 四个价差口径
    double spreadBidAsk{0.0};
    double spreadBidBid{0.0};
    double spreadAskBid{0.0};
    double spreadAskAsk{0.0};

    // TEMA 平滑
    double spreadBidAskTema{0.0};
    double spreadBidBidTema{0.0};
    double spreadAskBidTema{0.0};
    double spreadAskAskTema{0.0};

    // 极值
    double spreadBidAskMax{0.0};
    double spreadBidBidMax{0.0};
    double spreadAskBidMax{0.0};
    double spreadAskAskMax{0.0};

    double spreadBidAskMin{0.0};
    double spreadBidBidMin{0.0};
    double spreadAskBidMin{0.0};
    double spreadAskAskMin{0.0};

    // 盘口（真实头是 5 档）
    double activeAskPrice[5]{0.0};
    double activeBidPrice[5]{0.0};
    double activeAskVolume[5]{0.0};
    double activeBidVolume[5]{0.0};

    double passiveAskPrice[5]{0.0};
    double passiveBidPrice[5]{0.0};
    double passiveAskVolume[5]{0.0};
    double passiveBidVolume[5]{0.0};

    // 资金费
    double activeFundingRate{0.0};
    double passiveFundingRate{0.0};
    double activeNextFundingRate{0.0};
    double passiveNextFundingRate{0.0};

    // 腿价 TEMA
    double activePriceTema{0.0};
    double passivePriceTema{0.0};

    uint64_t activeFundingTs{0};
    uint64_t passiveFundingTs{0};
    int64_t activeDepthTs{0};
    int64_t passiveDepthTs{0};
    int64_t diffTs{0};
    int64_t generateTs{0};

    int64_t activeDepthDelay{0};
    int64_t passiveDepthDelay{0};

    int64_t exchActiveTradeDelay{0};
    int64_t exchPassiveTradeDelay{0};
};

struct DbpConfig {
    bool skipdata{false};
    uint32_t stematime{0};
    uint32_t tematime{0};
    uint32_t maxmintime{0};
    uint32_t timsspan{0};
    uint32_t checkspan{0};
    int marketype{0};
    SpreadType spreadType{SPT_BID1ASK1};
    SpreadDrive spreadDrive{SPD_ALL};
};

} // namespace dbp
