#pragma once

#include <string>
#include "DataStruct.h"
#include "Utility.h"

using namespace std;


class LarkRebot {
public:
    static LarkRebot& GetInstance();
    ~LarkRebot();
    void Run();
    void SendMsg(string msg);
    void SendMsg(string msg, ReceiveInfo& receiveInfo);
    void SendGroupMsg(string msg, ReceiveGroupInfo& receiveGroupInfo);
    void SendGroupMsgCard(MsgCard& msgCard, ReceiveGroupInfo& receiveGroupInfo);
    void SendVoiceCall(string msg, string receiveId);
    void SendGroupVoiceCall(string msg, string groupId, vector<string>& vUserId);
private:
    LarkRebot();
    string url;
    string authorization;
    string groupUrl;
    string voiceCallUrl;
    string groupVoiceCallUrl;
    unordered_map<string, int64_t> mIdTime;
    int channel;
    double voiceCallInterval;
    bool running;
    thread* runningThread;

    string tag;
};