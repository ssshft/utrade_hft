// Stub for the external dbp (spread feed) headers.
// Only the fields PairTradingContext.cpp / PairInfoManager.cpp read.
#pragma once

#include <cstdint>

namespace dbp {

struct DbpTopic {
    char __name[256]{""};
};

struct DbpData {
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

    // 腿价 TEMA
    double activePriceTema{0.0};
    double passivePriceTema{0.0};

    // 盘口量（AccumulateSpreadSample 只取 [0]）
    double activeBidVolume[8]{0.0};
    double activeAskVolume[8]{0.0};

    // 资金费
    double activeFundingRate{0.0};
    double passiveFundingRate{0.0};
    int64_t activeFundingTs{0};
    int64_t passiveFundingTs{0};

    // 生成时间（us）
    int64_t generateTs{0};
};

} // namespace dbp
