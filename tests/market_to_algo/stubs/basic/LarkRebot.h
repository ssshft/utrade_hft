#pragma once
// Stub for quant_library/basic/LarkRebot.h.
//
// 真实的 LarkRebot 往飞书发 HTTP 消息（LarkRebot.cpp 里走 Net.h / curl），还带一个
// 后台线程 + ConcurrentQueue。本套件不碰网络、不起线程，所以**只保留与真实头一致的
// 公开接口**，实现留空。保留签名是有意义的：调用点的重载解析、"哪条路径会发通知"
// 的编译期检查都还在（例如孤儿单告警只有冻结前那一支会发）。
//
// ⚠️ 这个桩是补 drift 的：stubs/basic/BaseAlgoOrder.h 顶掉了真实的 BaseAlgoOrder.h，
//    而真实那个在 :12 处 `#include "LarkRebot.h"`。于是 PairTradingContext.cpp 里的
//    `LarkRebot::GetInstance().SendMsg(msg)`（孤儿单告警）变成了未声明标识符。
#include <string>

class LarkRebot {
public:
    static LarkRebot& GetInstance() {
        static LarkRebot r;
        return r;
    }
    ~LarkRebot() {}

    void Run() {}
    void SendMsg(const std::string&) { ++sentCount; }

    // 测试用：记录被发出去的消息条数（真实实现里每条是一次 HTTP 请求）
    int sentCount{0};
};
