// Stub for dbp/sig/dbp/dbpreader.h.
//
// 真实的 DbpReader 内部是 sp::Reader（mmap 共享内存），本机没有 shmpool。
// 这里只保留 QuantDbp / AlgoContext 用到的接口，并且**让 Subscribe 把主题记下来** ——
// "算法单创建时订阅了哪个价差" 是一个真实可观察的行为，值得断言。
// FetchLast/FetchNext 是拉取循环，测试里由用例直接调 OnSpread，不需要。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "dbp/include.h"

namespace dbp {

typedef std::function<void(dbp::DbpTopic* topic, dbp::DbpData* data, uint32_t jumpedNum)> dbpcallback;

class DbpReader {
public:
    explicit DbpReader(const std::string& = "", const std::string& = "") {}
    ~DbpReader() {}

    void SetCallback(dbpcallback ck) { _dbpcallback = ck; }

    void Subscribe(const std::vector<std::string>& topics) {
        for (const auto& t : topics) Subscribe(t);
    }

    bool Subscribe(const std::string& topic) {
        subscribed.push_back(topic);
        return true;
    }

    void UnSubscribe(const std::string& topic) {
        unsubscribed.push_back(topic);
    }

    void FetchLast() {}
    void FetchNext() {}

    // ---- 测试用 ----
    std::vector<std::string> subscribed;
    std::vector<std::string> unsubscribed;

private:
    dbpcallback _dbpcallback;
};

} // namespace dbp
