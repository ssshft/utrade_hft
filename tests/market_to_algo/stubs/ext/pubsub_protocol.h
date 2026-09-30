// Stub for the external pubsub protocol headers.
// Only the fields the strategy side reads are modelled.
#pragma once

#include <cstdint>
#include <string>
#include "data_struct.h"

namespace pubsub {

struct Position {
    bool isLast{false};          // 批次尾标记
    int accountId{0};
    ExchangeType exchangeTypeEnum{ET_MIN};
    InstType instTypeEnum{IT_MIN};
    char instId[64]{""};
    Direction direction{DT_MIN}; // DT_LONG / DT_SHORT
    double volume{0.0};
    double avgPrice{0.0};
    double unrealizedPnl{0.0};
    double liquidPrice{0.0};
    double markPrice{0.0};
    double adlQuantile{0.0};
};

struct Balance {
    char currency[32]{""};
    ExchangeType exchangeTypeEnum{ET_MIN};
    InstType instTypeEnum{IT_MIN};
    double total{0.0};
    double unrealizedPnl{0.0};
};

struct TotalAccount {
    double totalEquity{0.0};
};

struct OrderResponse {
    int64_t orderId{0};
};

} // namespace pubsub
