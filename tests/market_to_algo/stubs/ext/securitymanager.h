// Stub for the external securitymanager headers.
// Only get_instrument_info + md::InstrumentInfo are exercised by the strategy side.
#pragma once

#include <string>
#include <unordered_map>
#include "data_struct.h"

namespace md {

struct InstrumentInfo {
    double value{1.0};      // 合约乘数
    double tickSize{0.0001};// 最小变价
    double minSize{0.1};    // 最小下单量
    int calcType{0};        // 0 = u本位 / 1 = 币本位
};

} // namespace md

namespace sm {

// 测试可控的合约信息表：key = "EXCHANGE.INSTTYPE.SYMBOL"
class SecurityManager {
public:
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
