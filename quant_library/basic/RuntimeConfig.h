#pragma once

#include <string>

namespace pt {

struct RuntimeConfig {
    bool closeOnly{false}; // 只平不开（运维急停）：true 时压掉全部开仓信号，平仓照常。
};

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out);

} // namespace pt
