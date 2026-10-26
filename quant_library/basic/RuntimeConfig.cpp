#include "RuntimeConfig.h"

#include "rapidjson/document.h"

#include <fstream>
#include <sstream>

namespace pt {

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out) {
    if (path.empty()) {
        return false;
    }

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

    auto& op = v["op"];
    if (op.HasMember("pairTrading")) {
        auto& s = op["pairTrading"];
    
        if (s.HasMember("closeOnly")) {
            out.closeOnly = s["closeOnly"].GetBool() ;
        }
    }
    else {
        return false;
    }

    return true;
}

} // namespace pt
