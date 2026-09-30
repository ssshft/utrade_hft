// Stub for the external securitymanager headers.
// Only get_instrument_info is exercised by the strategy side.
// md::InstrumentInfo 现在定义在 ext/data_struct.h 里（照抄真实头），不再在这里重复定义。
#pragma once

#include <string>
#include <unordered_map>
#include "data_struct.h"

namespace sm {

// 测试可控的合约信息表：key = "EXCHANGE.INSTTYPE.SYMBOL"
//
// 真实头里有**两个**重载（securitymanager.h:194 / :206）：
//     get_instrument_info(const char* exchId, const char* instType, const char* instId, ...)
//     get_instrument_info(ExchangeType, InstType, const char* instId, ...)
// 后者转发给前者。旧桩只留了枚举版，于是 PositionManager.cpp:464 那种按
// 字符串调用（`smc->get_instrument_info(v[0].c_str(), ...)`）直接编译不过。
// 这里把两个都补上，字符串版用 Str2EnumMap 还原成枚举再查表。
class SecurityManager {
public:
    bool get_instrument_info(const char* exchId, const char* instType, const char* instId,
                             md::InstrumentInfo& out) const {
        const std::string key = std::string(exchId) + "." + instType + "." + instId;
        auto found = m_info.find(key);
        if (found == m_info.end()) {
            return false;
        }
        out = found->second;
        return true;
    }

    bool get_instrument_info(ExchangeType ex, InstType it, const char* symbol, md::InstrumentInfo& out) const {
        const std::string key = std::string(ExchangeTypeEnum2StrMap.at(ex)) + "." +
                                InstTypeEnum2StrMap.at(it) + "." + symbol;
        auto found = m_info.find(key);
        if (found == m_info.end()) {
            return false;
        }
        out = found->second;
        return true;
    }

    void Set(const std::string& key, const md::InstrumentInfo& info) {
        m_info[key] = info;
    }

    void Clear() {
        m_info.clear();
    }

    std::unordered_map<std::string, md::InstrumentInfo> m_info;
};

} // namespace sm
