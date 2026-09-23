# WriteFileContent 改造方案：把 fmt::format 移出热路径

> 目标：落库路径（`WriteQuantOrder` / `WritePairOrder` / `WriteAlgoOrder`）目前把
> **~55 个字段的 `fmt::format` 放在行情线程上做**，这是热路径上最大的一块纯 CPU。
> 本方案把格式化搬到 `WriteFileContent` 的写线程，行情线程只做一次 POD 拷贝 + 入队。
> 本文档只给方案，**不改任何源代码**。

---

## 1. 现状

### 1.1 链路

```
行情线程（heavy_work）
  WriteQuantOrder(order, pdata)        Convert.h:12-65
  WritePairOrder(order, pdata)         Convert.h:67-146
  WriteAlgoOrder(ord)                  Convert.h:148-446
      └─ fmt::format(...) → std::string s          ★ 全部在行情线程
      └─ content c; c.type = N; c.msg = s;         ★ 又一次 string 拷贝
      └─ contentQueue.Push(c)                      ★ Push(T data) 按值传参 → 第三次拷贝
                                                        └─ moodycamel::ConcurrentQueue<content, 100000>

写线程（WriteFileContent::Run）        WriteFileContent.h:146-157
      └─ contentQueue.Pop(c) → WriteFile(c)        WriteFileContent.h:132-144
          └─ WriteQuantOrderToFile(c.msg)          → ofstream open/append/close
```

### 1.2 `content` 的定义

```cpp
// quant_library/basic/Utility.h:17-26
struct content {
    int type;
    string msg;                 // ← 已格式化好的整行 CSV
};
typedef ConcurrentQueue<content, 100000> CONTENTQUEUE;
extern CONTENTQUEUE contentQueue;
```

### 1.3 各 Write 函数的调用频率（决定改造优先级）

| 函数 | 调用点 | 频率 |
|---|---|---|
| `WriteQuantOrder` | `AlgoContext.cpp` 14 处（`OnSpread` 报单）、`BaseAlgoOrder.cpp` 424~637 共 14 处（`CancelOrderOnSpread` 撤单）、`BaseAlgoOrder.cpp:820`、`AlgoContext.cpp:1773`（`OnOrder` 成交回报） | **最高**，每次报单/撤单/成交都写 |
| `WritePairOrder` | `AlgoContext.cpp` 14 处（`OnSpread` 建配对单）、`BaseAlgoOrder.cpp:714`（配对单完结）、`AlgoRebalanceOrder.cpp:554` | **高**，每次建配对单都写 |
| `WriteAlgoOrder` | `AlgoContext.cpp:97`（建算法单）、`BaseAlgoOrder.cpp:719`（配对单完结且有成交）、`AlgoContext.cpp:1935/1968/1999/2017`（终结） | **低**，都是低频事件 |

**结论：`WriteQuantOrder` 和 `WritePairOrder` 是热路径（`OnSpread` 内），必须优先改；
`WriteAlgoOrder` 是低频，可以放第二阶段。**

### 1.4 单条记录的代价链（当前）

以 `WriteQuantOrder`（33 个字段）为例：

1. `fmt::format` 拼 33 个字段，其中 **14 个是 `{:.13f}`**（固定 13 位小数）
2. 返回 `std::string s` → 一次堆分配
3. `c.msg = s` → 第二次堆分配 + 拷贝
4. `Push(c)` 按值传参 → 第三次拷贝（`ConcurrentQueue::Push(T data)`，`ConcurrentQueue.h:15`）
5. 入队

`WritePairOrder` 更重：**48 个字段，其中 24 个是 `{:.13f}`**。

> ⚠️ **`{:.13f}` 是这里最贵的东西**。固定 13 位小数的 double → 字符串走的是 fmt 的定点格式化慢路径，
> 每个 double 大概 100~300ns，14~24 个就是 **1.5~7µs**，再加字符串拼接和 3 次分配，
> 单条 `WritePairOrder` 在行情线程上花掉 **5~15µs** 完全有可能。
> 一次报单要写 1 条 pairOrder + 1 条 quantOrder → **单次报单在落库上花 10~30µs**。
>
> 这个量级已经足以解释"行情过来时执行得不够快"。**建议第一步先量一下 `WritePairOrder` 的耗时**，
> 如果确实是这个量级，那本方案就是性价比最高的一刀（比拆线程简单得多）。

### 1.5 写线程自身的问题

```cpp
void Run() {
    while (running) {
        try {
            content c;
            if (contentQueue.Pop(c)) {      // 每次循环只取 1 条
                WriteFile(c);
            }
        } catch(exception& e) {             // ← 空 catch，异常被静默吞掉
        }
        usleep(1000);                       // ← 每条固定睡 1ms
    }
}
```

- **一次循环只处理 1 条记录 + 固定 `usleep(1000)`** → 写线程吞吐上限约 **1000 条/秒**，
  而且一旦积压就永远追不上（每次只消费 1 条，不是把队列排空）。
- `WriteXxxToFile` 每条都做 `access()` + `open()` + `close()`，**3 次系统调用/条**。
- `CovertToUtcDate(GetCurrentTimeUs())` 用的是**写入时刻**而不是记录自身的时刻，
  跨零点时记录会落到错误日期的文件里；另外 `CovertToUtcDate` 内部用的是**非可重入的 `gmtime`**
  （`Utility.h:67`），现在只有写线程调用所以没事，但这是个隐患（`log_engine.h:20` 用的是 `localtime_r`）。
- `catch(exception& e) {}` 是空的：格式化一旦抛异常，记录静默丢失。

**所以：如果只把格式化搬到写线程而不改 `Run()`，写线程会变成新的瓶颈。两件事必须一起做。**

---

## 2. 方案总览

| 阶段 | 内容 | 收益 | 风险 |
|---|---|---|---|
| **一** | `WriteQuantOrder` / `WritePairOrder` 改为「POD 记录 + 写线程格式化」 | 热路径去掉全部 format + 2 次 string 分配 | 低 |
| **二** | `WriteAlgoOrder` 同样改为 POD | 建单/终结路径去掉 format | 低 |
| **三** | 写线程效率：排空队列、复用 ofstream、`gmtime_r`、按记录时间分文件 | 消除新的瓶颈 | 中（要动 5 个 Write 函数的文件管理） |

阶段一 + 三 是必须配套的，阶段二可以晚点做。

---

## 3. 阶段一：具体改法

### 3.1 思路

把 `content` 从「**已格式化好的字符串**」改成「**原始字段的 POD 快照**」，
格式化动作从生产者（行情线程）挪到消费者（写线程）。

顺带解决两个已有问题：
- 热路径上的 2 次 `std::string` 分配彻底消失（POD 入队是纯 memcpy）。
- 这正是之前线程拆分方案里 `DbpData*` 不能跨线程的那个问题——`DbpSnapshot` 一起解决。

### 3.2 新增 `DbpSnapshot`（一次性拷完，保证同一 tick 的一致视图）

```cpp
// 只装落库真正用到的字段；必须在一次 memcpy 里拷完，
// 不能逐字段读共享内存里的 DbpData（ring 会被下一圈覆盖，逐字段读会拿到半新半旧的值）
struct DbpSnapshot {
    double  activeBidPrice1{0}, activeBidVolume1{0}, activeAskPrice1{0}, activeAskVolume1{0};
    double  passiveBidPrice1{0}, passiveBidVolume1{0}, passiveAskPrice1{0}, passiveAskVolume1{0};
    double  spreadBidAsk{0}, spreadBidBid{0}, spreadAskBid{0}, spreadAskAsk{0};
    int64_t generateTs{0};
    int64_t activeDepthTs{0}, passiveDepthTs{0};
    int64_t activeDepthDelay{0}, passiveDepthDelay{0};

    // pdata 可能为 null（BaseAlgoOrder.cpp:713 拿到的 GetSpread 就可能返回空），要判空
    void From(const dbp::DbpData* p) {
        if (!p) { return; }
        activeBidPrice1  = p->activeBidPrice[0];
        activeBidVolume1 = p->activeBidVolume[0];
        activeAskPrice1  = p->activeAskPrice[0];
        activeAskVolume1 = p->activeAskVolume[0];
        passiveBidPrice1  = p->passiveBidPrice[0];
        passiveBidVolume1 = p->passiveBidVolume[0];
        passiveAskPrice1  = p->passiveAskPrice[0];
        passiveAskVolume1 = p->passiveAskVolume[0];
        spreadBidAsk = p->spreadBidAsk;
        spreadBidBid = p->spreadBidBid;
        spreadAskBid = p->spreadAskBid;
        spreadAskAsk = p->spreadAskAsk;
        generateTs        = p->generateTs;
        activeDepthTs     = p->activeDepthTs;
        passiveDepthTs    = p->passiveDepthTs;
        activeDepthDelay  = p->activeDepthDelay;
        passiveDepthDelay = p->passiveDepthDelay;
    }
};
```

### 3.3 新增两个记录结构（纯基本类型，与 CSV 列 1:1）

关键点：**两个 record 都只用 double / int64 / int / bool / char[]，不内嵌 `stra::QuantOrder` 或 `PairOrder`。**
这样它们可以定义在 `Utility.h`（`content` 旁边），不产生 `DataStruct.h` 的循环包含。

```cpp
// Utility.h，紧挨着 content

struct QuantOrderRecord {
    // 身份（对应 quantOrder.csv 第 1~5 列）
    char     strategyName[32]{""};
    int64_t  strategyOrderId{0};
    char     systemOrderId[64]{""};
    char     exchangeOrderId[64]{""};
    char     instrumentKey[128]{""};
    // 枚举（存原始值，写线程再查 Enum2StrMap）
    int      orderType{0};
    int      direction{0};
    int      orderStatus{0};
    // 订单自身（第 9~14 列）
    double   targetPrice{0}, price{0}, volume{0};
    double   totalPriceOnOrder{0}, totalVolumeOnOrder{0}, tradeVolume{0};
    // 行情快照（第 15~22 列）
    DbpSnapshot dbp;
    // 其余（第 23~33 列）
    int64_t  updateTime{0};
    int      errorId{0};
    char     originErrorMsg[128]{""};
    bool     reduceOnly{false};
    int64_t  pairId{0};
    int64_t  algoPairId{0};
    bool     isActiveOrder{false};
    bool     rebalance{false};
};

struct PairOrderRecord {
    // 与 pairOrder.csv 的 48 列表头 1:1，这里只列分类，落地时逐列写全
    int64_t  pairId{0}, algoPairId{0};
    char     strategyName[32]{""};
    char     baseAsset[32]{""};
    int      tradingTypeOrder{0}, tradingTypeOffset{0};   // 存原始枚举值
    double   targetVolume{0};
    char     activeInstrumentKey[128]{""};
    int      activeDirection{0};
    double   activeTargetPrice{0};
    char     passiveInstrumentKey[128]{""};
    int      passiveDirection{0};
    double   passiveTargetPrice{0};
    DbpSnapshot dbp;                                      // 第 11~14、18~30 列里的行情部分
    double   activeTotalPriceOnOrder{0}, activeTotalVolumeOnOrder{0};
    double   passiveTotalPriceOnOrder{0}, passiveTotalVolumeOnOrder{0};
    double   pairTotalVolume{0}, pairActiveTotalPrice{0}, pairPassiveTotalPrice{0};
    double   activeFrozenPrice{0}, activeFrozenVolume{0};
    double   passiveFrozenPrice{0}, passiveFrozenVolume{0};
    int      activeAccountId{0}, passiveAccountId{0};
    int      status{0};
    bool     rebalanceFlag{false};
    int64_t  updateTime{0}, createTime{0};
    double   pairTargetSpread{0};
};
```

> **为什么 `QuantOrderRecord` 不直接内嵌 `stra::QuantOrder`？**
> `stra::QuantOrder` 本身是 trivially copyable 的（约 800 字节，全是 char[]/double/enum/bool），
> 内嵌它确实更省事。但那样 `content` 就要定义在 `DataStruct.h` 里，而 `CONTENTQUEUE` 的 typedef
> 和 `extern contentQueue` 在 `Utility.h`，会形成包含顺序上的纠缠；而且日志表结构会被
> 运行时结构体的改动"意外"影响。**显式 POD 更啰嗦但更稳。**
> 如果你更看重省事，备选做法见 §5.1。
>
> **为什么 `PairOrderRecord` 不能内嵌 `PairOrder`？**
> `PairOrder` 里带两个 `unordered_set<int64_t>`（`PairManager.h:39/60`），整体拷贝会触发隐式分配，
> 在热路径上反而是负优化。

### 3.4 改造 `content` 与队列

```cpp
// Utility.h
struct content {
    int type{0};
    union {
        QuantOrderRecord quantOrder;
        PairOrderRecord  pairOrder;
        AlgoOrderRecord  algoOrder;   // 阶段二再加
    };
    content() {}                       // 成员都是 POD，无需构造/析构
};

typedef ConcurrentQueue<content, 100000> CONTENTQUEUE;   // ← 容量要重新评估，见 §3.7
```

### 3.5 生产者侧（`Convert.h`）：只拷贝，不格式化

```cpp
inline void WriteQuantOrder(const stra::QuantOrder& order, const dbp::DbpData* pdata) {
    content c;
    c.type = 1;
    QuantOrderRecord& r = c.quantOrder;
    strncpy(r.strategyName,    order.strategyName,    sizeof(r.strategyName) - 1);
    r.strategyOrderId = order.strategyOrderId;
    strncpy(r.systemOrderId,   order.systemOrderId,   sizeof(r.systemOrderId) - 1);
    strncpy(r.exchangeOrderId, order.exchangeOrderId, sizeof(r.exchangeOrderId) - 1);
    strncpy(r.instrumentKey,   order.instrumentKey,   sizeof(r.instrumentKey) - 1);
    r.orderType   = int(order.orderType);
    r.direction   = int(order.direction);
    r.orderStatus = int(order.orderStatus);
    r.targetPrice = order.targetPrice;
    // ... 其余字段逐一赋值（共 33 项）
    r.updateTime = order.updateTime;
    r.errorId    = order.errorId;
    strncpy(r.originErrorMsg, order.originErrorMsg, sizeof(r.originErrorMsg) - 1);
    r.reduceOnly    = order.reduceOnly;
    r.pairId        = order.pairId;
    r.algoPairId    = order.algoPairId;
    r.isActiveOrder = order.isActiveOrder;
    r.rebalance     = order.rebalance;
    r.dbp.From(pdata);

    contentQueue.Push(c);
}
```

`WritePairOrder` 同理（48 项）。**函数签名不变**，所有 30 个调用点一行都不用改。

### 3.6 消费者侧（`WriteFileContent.h`）：在这里格式化

把 5 个 `WriteXxxToFile(const string& s)` 保留不动（它们只负责写文件），
**在前面加一层 `FormatXxx(const Record&) -> string`**：

```cpp
string FormatQuantOrder(const QuantOrderRecord& r) {
    return fmt::format(
        "{},{},{},{},{},{},{},{},",
        r.strategyName, r.strategyOrderId, r.systemOrderId, r.exchangeOrderId, r.instrumentKey,
        OrderTypeEnum2StrMap[OrderType(r.orderType)],
        DirectionEnum2StrMap[Direction(r.direction)],
        OrderStatusEnum2StrMap[OrderStatus(r.orderStatus)],
        // ... 与原来完全一致的格式串，只是数据来源从 order.xxx / pdata->xxx 换成 r.xxx
    );
}

void WriteFile(const content& c) {
    if (c.type == 1) {
        WriteQuantOrderToFile(FormatQuantOrder(c.quantOrder));
    } else if (c.type == 2) {
        WritePairOrderToFile(FormatPairOrder(c.pairOrder));
    } else if (c.type == 3) {
        WriteAlgoPairOrderToFile(FormatAlgoPairOrder(c.algoOrder));      // 阶段二
    }
    // ...
}
```

`Enum2StrMap` 那些是 `static` 的命名空间级 `unordered_map`，静态初始化后只读，
写线程查表是安全的（每个 TU 一份副本，无所谓）。

### 3.7 容量与内存

`ConcurrentQueue<content, 100000>` 的第二个模板参数是**初始容量提示**。
`content` 从约 40 字节涨到约 **900 字节**（union 取最大者 `PairOrderRecord`）：

- 100000 × 900B ≈ **90MB**（队列还会按需增长）
- 建议降到 **8192~16384**（7~15MB）

同时要确认写线程能跟上（见 §4）。上线前建议打一条监控：
`contentQueue.size_approx()`，正常应长期贴近 0。

---

## 4. 阶段三：写线程必须一起改

只搬格式化不改 `Run()` 的话，写线程会变成瓶颈。三处改动：

```cpp
void Run() {
    while (running) {
        bool idle = true;
        content c;
        // 1) 排空队列，而不是一次只取 1 条
        while (contentQueue.Pop(c)) {
            idle = false;
            try {
                WriteFile(c);
            } catch (const std::exception& e) {
                // 2) 不要空 catch：格式化失败要能看到
                LOG_ERROR("WriteFileContent failed, type:{} err:{}", c.type, e.what());
            }
        }
        // 3) 只有真的空了才睡
        if (idle) {
            usleep(1000);
        }
    }
}
```

配套（**已单独成文，见 `docs/csv_write_optimization.md`**）：
- `WriteXxxToFile` 里每条的 `access` + `open` + `close` 共 3 次系统调用 → 改成按文件缓存 `ofstream`
- `ofstream` 默认缓冲区在 libc++ 下只有 `BUFSIZ`(1024B)，必须显式 `pubsetbuf` 放大
- `CovertToUtcDate` 的入参改成**记录自身的时间**（`r.updateTime` / `r.createTime`），
  而不是 `GetCurrentTimeUs()`，避免跨零点错文件；同时把 `gmtime` 换成 `gmtime_r`（`Utility.h:67`）
- 写线程的退出收尾（`running` 从不置 false、析构不 join）

---

## 5. 阶段二：`WriteAlgoOrder`

`WriteAlgoOrder` 有 3 个分支（`Convert.h:149 / 246 / 346`），分别格式化了
`AlgoPairOrder`（55 列）/ `AlgoFishingOrder`（56 列）/ `AlgoRebalanceOrder`（56 列）。

**注意这里不能传指针**：`WriteAlgoOrder` 的调用点里，`AlgoContext.cpp:1935/1968/1999/2017`
传的是 `it->second`（`OnTimer` 里马上要 `delete` 的对象），
`BaseAlgoOrder.cpp:719` 传的是 `this`。把裸指针交给写线程就是 use-after-free。

所以同样要 POD 化：

```cpp
struct AlgoOrderRecord {
    int      algoType{0};               // 决定写哪个文件 / 用哪套列
    char     algoStrategyName[32]{""};
    int64_t  algoOrderId{0};
    char     pairInstrumentKey[128]{""};
    char     baseAsset[32]{""};
    int      algoOrderStatus{0};
    // ... active/passive 两腿各约 20 项（instrumentKey、各 Pct、accountId、driveType、
    //     depthCheck、orderType、cancelOrderTime ×4、cancelOrderPct ×5、feeRate ×4、slippage ×4）
    double   pairActiveTotalPrice{0}, pairTotalVolume{0}, pairPassiveTotalPrice{0};
    double   makerTakerFs{0}, takerTakerFs{0};
    double   maxMTOrderSize{0}, maxTTOrderSize{0};
    int      targetSpreadType{0};
    int      activeVolumeCalcualteType{0};
    double   ttTargetVolume{0}, mtTargetVolume{0};
    // 子类专属
    double   fishingSlippagePct{0};     // AlgoFishingOrder
    int      activeTrade{0};            // AlgoRebalanceOrder
};
```

生产者侧需要按 `ord->algoType` 分别填（因为 `fishingSlippagePct` / `activeTrade` 在子类上）：

```cpp
inline void WriteAlgoOrder(BaseAlgoOrder* ord) {
    if (!ord) { return; }
    content c;
    AlgoOrderRecord& r = c.algoOrder;
    FillAlgoOrderCommon(r, ord);                       // 抽公共的 55 项
    if (ord->algoType == stra::AlgoType_PairTrading) {
        c.type = 3;
    } else if (ord->algoType == stra::AlgoType_FishingTrading) {
        c.type = 4;
        r.fishingSlippagePct = ((AlgoFishingOrder*)ord)->fishingSlippagePct;
    } else if (ord->algoType == stra::AlgoType_Rebalance) {
        c.type = 5;
        r.activeTrade = ((AlgoRebalanceOrder*)ord)->activeTrade;
    } else {
        return;
    }
    contentQueue.Push(c);
}
```

这样 `Convert.h` 里那 ~300 行的 `fmt::format`（148-446）整体删掉，搬进 `WriteFileContent.h`。

### 5.1 备选（省事版）

如果不想写 `QuantOrderRecord` 的 33 项字段映射，可以让它直接内嵌 `stra::QuantOrder`：

```cpp
struct QuantOrderRecord { stra::QuantOrder order; DbpSnapshot dbp; };
```

代价：
- `content` 必须搬到 `DataStruct.h`（`CONTENTQUEUE` 的 typedef 和 `extern` 也得跟着搬），
  `Utility.h` 不再定义 `content` —— 涉及 `BaseAlgoOrder.cpp:11` 的定义点。
- 以后给 `QuantOrder` 加字段会无声地把队列元素撑大。

`PairOrder` 和 `BaseAlgoOrder` 都不能这么办（前者带 `unordered_set`，后者是多态基类）。

---

## 6. 改动清单

| 文件 | 改动 | 量级 |
|---|---|---|
| `quant_library/basic/Utility.h` | 新增 `DbpSnapshot` / `QuantOrderRecord` / `PairOrderRecord`；`content` 改成 `type + union` | +~120 行 |
| `quant_library/basic/Convert.h` | `WriteQuantOrder` / `WritePairOrder` 删掉 `fmt::format`，改成字段拷贝（`-100 / -80` 行，`+60 / +60` 行）；阶段二删掉 `WriteAlgoOrder` 的三个 format（-300 行） | 净减 ~350 行 |
| `quant_library/basic/WriteFileContent.h` | 新增 `FormatQuantOrder` / `FormatPairOrder`（阶段二加 `FormatAlgoPairOrder` 等 3 个）；`WriteFile` 分发改掉；`Run()` 排空 + 非空 catch + 按需 sleep；`CONTENTQUEUE` 容量下调 | +~250 行 |
| 所有调用点 | **不动**（签名不变） | 0 |

---

## 7. 风险与注意点

### R1（高）写线程吞吐必须先解决
当前 `Run()` 是「1 条 + 睡 1ms」，上限约 1000 条/秒。格式化搬过去之后单条更慢
（format 的 CPU 从行情线程挪到了写线程），**必须同时做 §4 的排空改造**，否则日志延迟会持续增长。
上线后监控 `contentQueue.size_approx()`。

### R2（高）字段映射写错不会报错
`fmt::format` 是按位置取参的，改了数据来源之后**类型对不上会静默输出错误值**
（比如把 `int64` 传给 `{:.13f}`，或者顺序错位）。
建议：改造后**用同一批真实数据跑新旧两条路径做 diff**——把旧实现保留成一个 `WriteQuantOrderOld`，
在测试环境同时写两个文件比对。这是唯一能可靠验证这种改动的方式。

### R3（中）枚举查表从「生产者」变「消费者」
原来 `OrderTypeEnum2StrMap[order.orderType]` 在行情线程做，现在在写线程做。
那些 map 是 `static` 命名空间级对象（每个 TU 一份），静态初始化后只读，跨线程读是安全的。
但要注意：**如果某个 TU 从未触发过静态初始化就会崩**——不过它们都是 `static` 对象，
首次使用即初始化，无风险。别把它们改成 `constexpr` 之外的东西。

### R4（中）`DbpSnapshot::From` 必须判空
`BaseAlgoOrder.cpp:713` 的 `SpreadManager::Instance().GetSpread(pairInstrumentKey)` 可能返回空指针，
现在的 `WritePairOrder` 会直接解引用 `pdata->xxx`（**这是一个已存在的空指针风险**）。
`DbpSnapshot::From` 里判空能顺手修掉它。

### R5（中）`content` 变大后队列内存
见 §3.7。100000 × 900B ≈ 90MB，建议下调容量。

### R6（低）CSV 表头与 record 字段的一致性是人工维护的
`WriteXxxToFile` 里那段列名字符串是唯一权威的表结构定义。
建议在 `QuantOrderRecord` / `PairOrderRecord` 上方加一行注释指向对应的列名，改字段时两边一起改。

### R7（低）`CovertToUtcDate` 用了非可重入的 `gmtime`
现在只有写线程调用，暂时安全；如果按 §4 把时间来源改成记录时间，顺手换成 `gmtime_r`。

---

## 8. 建议的落地顺序

1. **先量**：在 `WriteQuantOrder` / `WritePairOrder` 入口各打一次 `crypto::rdtscp()`，
   看真实耗时。如果只有 1~2µs，那优先级可以降低；如果是 10µs+，就按本文档做。
2. **阶段一 + 阶段三一起上**（`WriteQuantOrder` / `WritePairOrder` + `Run()` 排空）。
3. **验证**：新旧路径 diff（R2）。
4. **阶段二**：`WriteAlgoOrder`。
5. 之后如果还嫌热路径不够快，再回头看线程拆分方案（`docs/thread_split_design.md`）——
   本方案做完之后，落库这条线已经不在热路径上了，拆线程的必要性会明显下降。
