#include "RuntimeConfig.h"
#include "rapidjson/document.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pt {

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) {
        return false;   // 编辑器刚 truncate、还没写进去
    }

    rapidjson::Document d;
    rapidjson::Value& v = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(text.c_str());

    // 半截文件（编辑器保存的中间态）必须当"读失败"返回：往下走 v["op"] 会作用在非对象上，
    if (d.HasParseError() || !v.IsObject()) {
        return false;
    }

    auto& op = v["op"];
    if (!(op.IsObject() && op.HasMember("pairTrading"))) {
        return false;
    }

    auto& s = op["pairTrading"];
    if (!s.IsObject()) {
        return false;
    }

    if (s.HasMember("closeOnly")) {
        out.closeOnly = s["closeOnly"].GetBool() ;
    }
    if (s.HasMember("stopAllAlgo")) {
        out.stopAllAlgo = s["stopAllAlgo"].GetBool() ;
    }

    // ---- pairCommands ----
    out.pairCmdCount = 0;

    if (s.HasMember("pairCommands")) {
        const auto& arr = s["pairCommands"];
        if (!arr.IsArray()) {
            LOG_ERROR("运行时配置: pairCommands 不是数组 -> 本次忽略，保留上一次的命令");
            return false;
        }

        if (arr.Size() > static_cast<rapidjson::SizeType>(kMaxPairCommands)) {
            LOG_ERROR("运行时配置: pairCommands 有 {} 条，超过上限 {} -> 本次忽略（请分批下发）", arr.Size(), kMaxPairCommands);
            return false;
        }

        try {
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
                if (arr[i].HasMember("cmdType")) {
                    out.pairCommands[i].cmdType = PairCommandType(std::stoi(arr[i]["cmdType"].GetString()));
                }

                if (arr[i].HasMember("pairKey")) {
                    // -1 而不是 INST_KEY_LEN：正好 128 字符时 strncpy 不写终止符，
                    // 后面 std::string(cmd.pairKey) 会一路读到相邻内存。
                    strncpy(out.pairCommands[i].pairKey, arr[i]["pairKey"].GetString(), stra::INST_KEY_LEN - 1); 
                    out.pairCommands[i].pairKey[stra::INST_KEY_LEN - 1] = '\0';
                }

                if (arr[i].HasMember("profitPct")) {
                    out.pairCommands[i].profitPct = std::stod(arr[i]["profitPct"].GetString());
                }

                if (arr[i].HasMember("closeRemainVolume")) {
                    out.pairCommands[i].closeRemainVolume = std::stod(arr[i]["closeRemainVolume"].GetString());
                }

                ++out.pairCmdCount;
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("运行时配置: pairCommands 解析失败（{}）-> 本次忽略，保留上一次的命令。检查 cmdType / profitPct / closeRemainVolume 是不是数字", e.what());
            out.pairCmdCount = 0;
            return false;
        }
    }

    return true;
}

} // namespace pt
