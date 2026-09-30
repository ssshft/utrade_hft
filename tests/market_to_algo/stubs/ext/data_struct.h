// Stub for the external (deploy-machine) <data_struct.h>.
// Only the enums + enum<->string maps that the strategy-side .cpp files touch.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// ---- OrderType ----
enum OrderType {
    OT_MIN = 0,
    OT_LIMIT,
    OT_MARKET,
    OT_MAX
};

// ---- OffsetFlag ----
enum OffsetFlag {
    OF_MIN = 0,
    OF_OPEN,
    OF_CLOSE,
    OF_MAX
};

// ---- Direction (order side) ----
enum Direction {
    DT_MIN = 0,
    DT_LONG,
    DT_SHORT,
    DT_MAX
};

// ---- OrderStatus ----
enum OrderStatus {
    OS_MIN = 0,
    OS_PEND,
    OS_PENDING_NEW,
    OS_NEW,
    OS_PARTFILLED,
    OS_FILLED,
    OS_CANCEL,
    OS_CANCELLING,
    OS_CANCELED,
    OS_REJECTED,
    OS_UNKNOWN,
    OS_MAX
};

// ---- ExchangeType / InstType (venue taxonomy) ----
enum ExchangeType {
    ET_MIN = 0,
    BINANCE,
    GATEIO,
    OKX,
    ET_MAX
};

enum InstType {
    IT_MIN = 0,
    USDT_SWAP,
    COIN_SWAP,
    SPOT,
    IT_MAX
};

// Enum -> string (used by PairInfoManager::UpdateOnPosition / UpdateOnBalance)
static std::unordered_map<ExchangeType, std::string> ExchangeTypeEnum2StrMap{
    {BINANCE, "BINANCE"}, {GATEIO, "GATEIO"}, {OKX, "OKX"}};

static std::unordered_map<InstType, std::string> InstTypeEnum2StrMap{
    {USDT_SWAP, "USDT_SWAP"}, {COIN_SWAP, "COIN_SWAP"}, {SPOT, "SPOT"}};

// String -> enum (used by PairInfoManager::Init to parse the pairKey)
static std::unordered_map<std::string, ExchangeType> ExchangeTypeStr2EnumMap{
    {"BINANCE", BINANCE}, {"GATEIO", GATEIO}, {"OKX", OKX}};

static std::unordered_map<std::string, InstType> InstTypeStr2EnumMap{
    {"USDT_SWAP", USDT_SWAP}, {"COIN_SWAP", COIN_SWAP}, {"SPOT", SPOT}};
