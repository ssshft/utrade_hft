// Stub for the external include/time_util.h.
//
// 真实头里有两块在 arm64 macOS 上编译不过的东西：
//   1. x86 专用的 rdtsc 汇编（`asm volatile("rdtsc")`、`__builtin_ia32_rdtsc()`）
//   2. boost/date_time
// 其余函数逐字照抄（它们只用 POSIX / std，两边都能编）。
//
// ⚠️ 这里有一处**语义修正**，不是随手的：
//    旧桩的 rdtscp() 是 `return getCurrentTime();` —— 微秒时间戳。
//    真实的 rdtscp() 是 `(ts.tv_sec + atomic_rdtscp_count++) * 1e9 + ts.tv_nsec`，
//    **带一个自增计数器**，保证同一纳秒内连续调用也返回不同的值。
//    而 rdtscp() 正是 GenerateStrategyOrderId() / GenerateStrategyPairId() /
//    GenerateStrategyAlgoPairId() 的实现 —— 它是**订单号**。
//    微秒分辨率下，一个循环里连续拆出的几笔子单会拿到同一个订单号，
//    而真实实现不会。执行侧用例要在一个 tick 里连开多单，这个差别会直接改变行为，
//    所以必须照抄真实实现（clock_gettime 在 macOS 10.12+ 和 Linux 上都有）。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>
#include <sys/time.h>

namespace crypto {

// ---- 逐字照抄（含那个保证唯一性的自增计数器）----
static std::random_device rd;
static std::atomic<long> atomic_rdtscp_count((rd() % (100 - 1)) + 1);

inline int64_t rdtscp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec + atomic_rdtscp_count++) * 1000000000LL + ts.tv_nsec;
}

inline int64_t getCurrentTimeNano() {  // ns
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

inline int64_t getCurrentTime() {  // us
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

inline int64_t getCurrentTimeMilli() {  // ms
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000 + tv.tv_usec * 0.001);
}

inline int64_t getCurrentTimeSeconds() {  // s
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

inline std::string getTimestampIso() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms.count()));
    return buf;
}

inline std::string get_date_str() {
    char dateStr[16] = {0};
    time_t now;
    struct tm* tm_now;
    time(&now);
    tm_now = localtime(&now);
    sprintf(dateStr, "%d%02d%02d",
            tm_now->tm_year + 1900,
            tm_now->tm_mon + 1,
            tm_now->tm_mday);
    return std::string(dateStr);
}

// ---- 下面两个真实实现是 x86 汇编，这里退化成纳秒时钟 ----
// 本套件没有任何断言依赖"tsc 周期数"这个语义，只依赖单调递增。
inline int64_t getCurrentTimeNs() { return getCurrentTimeNano(); }
inline int64_t get_rdtsc_timestamp() { return getCurrentTimeNano(); }
inline uint64_t rdtsc() { return (uint64_t)getCurrentTimeNano(); }

}  // namespace crypto
