#pragma once
// Stub for quant_library/basic/StrategyConfig.h.
//
// 真实的是从 etc/strategy.ini 读（boost::property_tree）。测试里不做文件 IO，
// 但**默认值必须与 utrade_hft/etc/strategy.ini 一致** —— 这不是可选项：
//
//   AlgoContext::Init 会把 curSpreadDelay / curSpreadDepthDelay 读出来乘 1000 当**毫秒**
//   用，然后 AlgoContext::OnSpread 里这么判：
//       if (nowTime - pdata->generateTs < curSpreadDelay) curDelay = true;
//       openOrderFlag = curDelay && curDepthDelay && curTradeDelay;
//   也就是说 curSpreadDelay 是"行情最大允许延迟"，**为 0 时这个条件恒假**，
//   于是 openOrderFlag 永远是 false，一笔子单都不会报。
//   最初这里返回 0，表现就是"OnSpread 进了循环、找到了算法单、但什么也没发生" ——
//   一个纯粹由桩造成的假阴性。
//
//   etc/strategy.ini 里是 curspreaddelay = 50、curspreaddepthdelay = 300（毫秒）。
//
//   ── 第二个坑：tradesThreshold 的真实取值是 1，不是 0 ──
//   真实 StrategyConfig.cpp:36 是
//       tradesThreshold = itemMd.get<int>("tradesshold", 1);
//   注意两点：
//     (1) ini 键名是 **"tradesshold"**（生产代码里的拼写，少了 thre 三个字母），
//         而 etc/strategy.ini 的 [MD] 段里**没有**这个键；
//     (2) 所以它一定走 property_tree 的**默认值分支**，取到的就是 **1**。
//   1 会再被 AlgoContext::Init 乘 1000 变成 1000（毫秒）：
//       tradesDelayThreshold = GetTradesThreshold() * 1000;   // = 1000
//   而 OnSpread 里判的是
//       if (pdata->exchActiveTradeDelay < tradesDelayThreshold &&
//           pdata->exchPassiveTradeDelay < tradesDelayThreshold) curTradeDelay = true;
//   DbpData 里这两个字段的默认值都是 0，于是 0 < 1000 成立 -> curTradeDelay = true。
//   **如果桩返回 0，就变成 0 < 0 恒假**，curTradeDelay 永远 false，
//   openOrderFlag 跟着永远 false，症状和上面 curSpreadDelay=0 一模一样。
#include <string>
#include <unordered_map>

#include "Utility.h"
#include "DataStruct.h"

class StrategyConfig {
public:
    static StrategyConfig& GetInstance() { static StrategyConfig c; return c; }
    ~StrategyConfig() {}

    void LoadConfig() {}
    void LoadStrategy() {}

    std::unordered_map<int, AccountInfo>& GetAccountInfo() { return m_accountInfo; }

    std::string GetStrategyIdByAccountId(int accountId) {
        auto it = m_accountInfo.find(accountId);
        return it == m_accountInfo.end() ? std::string("test1") : it->second.strategyId;
    }

    std::string GetMdAddr() { return "127.0.0.1"; }
    int GetMdPort() { return 9379; }
    std::string GetMdPassword() { return ""; }
    std::string GetLarkUrl() { return ""; }
    double GetMinOrderAmount() { return 5.0; }

    // ---- 与 etc/strategy.ini 对齐（单位：毫秒）----
    int GetCurSpreadDelay() { return 50; }        // curspreaddelay = 50
    int GetCurSpreadDepthDelay() { return 300; }  // curspreaddepthdelay = 300
    int GetCurSpreadTradesDelay() { return 1; }   // ini 无此键 -> 默认 1
    int GetTradesThreshold() { return 1; }        // ini 无 "tradesshold" -> 默认 1
    int GetOnTimerTrade() { return 0; }           // 注意：真实实现里这个成员**从未被赋值**

    // 按 etc/strategy.ini 的 [ACCOUNTn] 段填一个账户。
    // 字段名与取值都对齐真实配置（test1 / 10000 / BINANCE / USDT_SWAP / 杠杆 2 / 频率 30）。
    void SetAccount(int accountId, const std::string& strategyId,
                    const std::string& accountName = "test1",
                    ExchangeType exchangeType = BINANCE,
                    InstType instType = USDT_SWAP) {
        AccountInfo a;
        a.accountName = accountName;
        a.accountId = accountId;
        a.accountType = stra::AT_SWAP;          // accounttype = AT_CLASSIC 对应的实际枚举
        a.exchangeType = exchangeType;
        a.vInstType = {instType};
        a.strategyId = strategyId;
        a.openRealLeverage = 2;
        a.maxRealLeverage = 2;
        a.passiveOpenRealLeverage = 2;
        a.passiveMaxRealLeverage = 2;
        a.openActiveMgnRatio = 5;
        a.openPassiveMgnRatio = 5;
        a.maxPersec = 30;
        a.maxCancelPersec = 30;
        a.orderNum = 100000;
        m_accountInfo[accountId] = a;
    }

    void Clear() { m_accountInfo.clear(); }

private:
    StrategyConfig() = default;
    std::unordered_map<int, AccountInfo> m_accountInfo;
};
