#pragma once
#include <cstdint>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "time_util.h"

// 真实的 ConcurrentQueue 是 moodycamel 的并发队列；这里换成单线程 deque，
// Push/Pop 的 FIFO 语义一致。差别只有"容量上限 16384"这一条 —— 测试里不可能填满。
template <class T, size_t Size>
class ConcurrentQueue {
public:
    void Push(T data) { q.push_back(std::move(data)); }
    bool Pop(T& data) {
        if (q.empty()) return false;
        data = std::move(q.front());
        q.pop_front();
        return true;
    }
private:
    std::deque<T> q;
};

#include "DataStruct.h"

typedef ConcurrentQueue<std::string, 10000> RQUEUE;
typedef ConcurrentQueue<content, 16384> CONTENTQUEUE;

extern RQUEUE rLarkMsg;
extern CONTENTQUEUE contentQueue;

inline std::string CovertToUtcStr(int64_t tUs, bool hasUs = true) {
    if (tUs <= 0) return "0";
    time_t ts = tUs / 1000000;
    long us = tUs % 1000000;
    struct tm tmT = *gmtime(&ts);
    char s[64];
    strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tmT);
    std::stringstream ss;
    if (hasUs) ss << s << "." << us; else ss << s;
    return ss.str();
}

inline std::string CovertToUtcDate(int64_t tUs) {
    if (tUs <= 0) return "0";
    time_t ts = tUs / 1000000;
    struct tm tmT = *gmtime(&ts);
    char s[64];
    strftime(s, sizeof(s), "%Y-%m-%d", &tmT);
    return std::string(s);
}

inline void splitString(const std::string& source, std::vector<std::string>& v, const std::string delimiters = " ") {
    std::string::size_type lastPos = source.find_first_not_of(delimiters, 0);
    std::string::size_type pos = source.find_first_of(delimiters, lastPos);
    while (std::string::npos != pos || std::string::npos != lastPos) {
        v.push_back(source.substr(lastPos, pos - lastPos));
        lastPos = source.find_first_not_of(delimiters, pos);
        pos = source.find_first_of(delimiters, lastPos);
    }
}

inline int64_t GenerateStrategyOrderId()    { return crypto::rdtscp(); }
inline int64_t GenerateStrategyPairId()     { return crypto::rdtscp(); }
inline int64_t GenerateStrategyAlgoPairId() { return crypto::rdtscp(); }
