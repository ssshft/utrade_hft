#pragma once

#include <string>
#include "DataStruct.h"
#include "ConcurrentQueue.h"
#include "Utility.h"
#include "Net.h"


class LarkRebot {
public:
    static LarkRebot& GetInstance();
    ~LarkRebot();
    void Run();
    void SendMsg(const std::string& msg);

private:
    LarkRebot();
    void Send(const std::string& s);
    std::string url;
    std::string tag;

    bool running;
    std::thread* runningThread;

    ConcurrentQueue<string, 10000> queue;
};