#ifndef LIMIT_BOARD_H
#define LIMIT_BOARD_H

#include "DataStruct.h"
#include <list>

/*for limit units */
struct LimitUnit{
    int accountId{0};
    int maxPersec{20};
    int maxCancelPersec{30};
    // 查单限频。**没有配置入口**（strategy.ini 里只有 maxpersec / maxcancelpersec），
    // 用默认值：查单不是交易动作，只需要防住"卡单时每个 tick 都去查"这种风暴，
    // 30/秒 已经远高于正常用量（OnTimer 里那两处查单都被 stuckOrderReportCount
    // / errorOrderReportCount 卡着，正常一分钟才几条）。
    int maxQueryPersec{30};
    int orderNum{0};
};

// 报单 / 撤单 / 查单 **三本独立的账**。
//
// 为什么必须分开：2026-11-17 之前三者共用同一个 `ordertimes` 链表，
// 而 QuantTrade 的 CreateOrder / CancelOrder / QueryOrder 都会往里面记一笔
// ⇒ **查一次单就少一次报单额度**，撤单也一样。
// 行情剧烈、卡单多的时候，查单/撤单会把 30 笔/秒的报单配额吃光，
// 外部表现就是"策略突然不报单了"，而且日志里只看到 PassLimit 打的那行
// ordertimes.size，根本看不出是谁占的。
// 现在各记各的、各查各的，`PassLimit` 只反映真正的报单量。
class LimitCalcBoard
{
public:
    LimitCalcBoard() {}
    ~LimitCalcBoard() {}

    void SetLimitUnit(const LimitUnit& unit);

    bool CheckPersec();         // 报单配额
    bool CheckCancelPersec();   // 撤单配额
    bool CheckQueryPersec();    // 查单配额
    bool CheckNum();

    void OnCreate();   // 记一笔报单
    void OnCancel();   // 记一笔撤单
    void OnQuery();    // 记一笔查单

    LimitUnit& GetLimitUnit() {return limitval;}
    int GetOrderNum();
    int GetPerSec();

private:
    // 滑动 1 秒窗口：先弹掉超过 1000ms 的记录，再看剩下的够不够限额。
    bool CheckWindow(std::list<int64_t>& times, int limit, const char* what);
    // 与 CheckWindow 同一套窗口逻辑，只是不判限额 —— 给 OnXxx 用来防止链表无界增长
    // （某些报单路径没有先过 PassLimit，只记账不检查）。
    void PruneWindow(std::list<int64_t>& times, int64_t nowMs);

    int orderNum{0};
    std::list<int64_t> orderTimes;
    std::list<int64_t> cancelTimes;
    std::list<int64_t> queryTimes;
    LimitUnit limitval;
};

#endif
