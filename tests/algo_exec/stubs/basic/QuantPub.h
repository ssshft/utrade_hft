#pragma once
#include "DataStruct.h"
#include "command_helper.h"
// 真实的 QuantPub 往 redis 发快照。纯副作用，测试里 no-op。
class QuantPub {
public:
    static QuantPub& Instance() { static QuantPub p; return p; }
    void Publish(const std::string&) {}
    void PublishAlgoOrder(const std::string&) {}
    void PublishPairOrder(const std::string&) {}
};
