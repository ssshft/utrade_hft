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
    std::string JsoneEcaped(const std::string& msg);
    void Run();
    void SendMsg(const std::string& msg, bool atAll = false);

private:
    LarkRebot();
    void Send(const std::string& s, bool atAll = false);
    std::string url;
    std::string tag;

    bool running;
    std::thread* runningThread;

    struct ContentMsg {
        char msg[1024];
        bool atAll;
    };

    ConcurrentQueue<ContentMsg, 100> queue;
};