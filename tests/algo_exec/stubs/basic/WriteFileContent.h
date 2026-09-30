#pragma once
// 真实的是后台线程把 contentQueue 排空写 CSV。Convert.h 在本套件里已经是 no-op，
// 所以 contentQueue 永远是空的，这个类完全不参与。保留 GetInstance / 同签名即可。
#include <string>
#include "Utility.h"

class WriteFileContent {
public:
    static WriteFileContent& GetInstance() { static WriteFileContent w; return w; }
    void Run() {}
    void Stop() {}
    void Start(const std::string&) {}
    void Start() {}

    std::string SuffixOf(int ty) {
        if (ty == 1) return "_quantOrder.csv";
        if (ty == 2) return "_pairOrder.csv";
        if (ty == 3) return "_algoOrder.csv";
        return "";
    }
};
