#pragma once
// 真实的 LarkRebot 是往飞书发 HTTP 消息（LarkRebot.cpp 里是 curl）。
// 纯通知副作用，对被测状态零影响，这里保留**完全一致的方法名和签名**，实现留空。
// 保留签名是有意义的：调用点的重载解析、以及"哪条路径会发通知"的编译期检查都还在。
#include <string>
#include <unordered_map>
#include <vector>

#include "DataStruct.h"
#include "Utility.h"

using namespace std;

class LarkRebot {
public:
    static LarkRebot& GetInstance() { static LarkRebot r; return r; }
    ~LarkRebot() {}

    void Run() {}
    void SendMsg(string) {}
    void SendMsg(string, ReceiveInfo&) {}
    void SendGroupMsg(string, ReceiveGroupInfo&) {}
    void SendGroupMsgCard(MsgCard&, ReceiveGroupInfo&) {}
    void SendVoiceCall(string, string) {}
    void SendGroupVoiceCall(string, string, vector<string>&) {}

    // 测试用：记录被发出去的消息（真实实现里这是 HTTP 请求）
    vector<string> sent;
};
