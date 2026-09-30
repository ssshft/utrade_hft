// Stub for <fmt/core.h>.
//
// ⚠️ 这个桩**必须真的做 `{}` 替换**，不能"把参数拼起来就算完"。
//    最早那版直接 `((os << args), ...)` 忽略格式串，结果
//        fmt::format("{}.{}.{}", "BINANCE", "USDT_SWAP", "DOGE-USDT")
//    返回 "BINANCEUSDT_SWAPDOGE-USDT" —— 两个点没了。
//    PairInfoManager::UpdateOnBalance 用它拼 symKey，于是 strstr 永远匹配不上，
//    资金推送的写入被静默吞掉（I2/I3 用例就是被这个桩骗红的）。
//    桩可以少实现，但不能改变语义。
//
// 支持：`{}` 顺序替换、`{{` / `}}` 转义。
// 不支持（本工程也没用到）：`{0}` 位置参数、`{:.2f}` 之类格式说明符。
#pragma once
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fmt {

namespace detail {
template <typename T>
inline std::string Render(T&& v) {
    std::ostringstream os;
    os << std::forward<T>(v);
    return os.str();
}
} // namespace detail

template <typename... Args>
inline std::string format(const char* f, Args&&... args) {
    std::vector<std::string> a;
    a.reserve(sizeof...(Args));
    ((a.push_back(detail::Render(std::forward<Args>(args)))), ...);

    std::string out;
    std::size_t ai = 0;
    for (std::size_t i = 0; f[i] != '\0';) {
        if (f[i] == '{' && f[i + 1] == '{') {
            out += '{';
            i += 2;
        } else if (f[i] == '}' && f[i + 1] == '}') {
            out += '}';
            i += 2;
        } else if (f[i] == '{' && f[i + 1] == '}') {
            if (ai < a.size()) {
                out += a[ai++];
            }
            i += 2;
        } else {
            out += f[i];
            ++i;
        }
    }
    return out;
}

inline std::string format(const char* f) {
    return std::string(f);
}

template <typename... Args>
inline void print(const char* f, Args&&... args) {
    (void)f;
    ((void)args, ...);
}
template <typename... Args>
inline void print(std::FILE*, const char* f, Args&&... args) {
    (void)f;
    ((void)args, ...);
}

} // namespace fmt
