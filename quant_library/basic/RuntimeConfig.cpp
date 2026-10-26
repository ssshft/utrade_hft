#include "RuntimeConfig.h"
#include "rapidjson/document.h"
#include <fstream>
#include <sstream>

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
    // Release 下是 UB、Debug 下直接 assert。config.h 也是先查这一步再用。
    if (d.HasParseError() || !v.IsObject()) {
        return false;
    }

    auto& op = v["op"];
    if (op.IsObject() && op.HasMember("pairTrading")) {
        auto& s = op["pairTrading"];
    
        if (s.HasMember("closeOnly")) {
            out.closeOnly = s["closeOnly"].GetBool() ;
        }
        if (s.HasMember("stopAllAlgo")) {
            out.stopAllAlgo = s["stopAllAlgo"].GetBool() ;
        }
    }
    else {
        return false;
    }

    return true;
}

} // namespace pt
