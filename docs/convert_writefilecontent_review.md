# Convert.h / WriteFileContent.h 代码复核

> 复核对象：当前工作区的 `quant_library/basic/Convert.h`（168 行）、
> `WriteFileContent.h`（395 行），以及配套改动的 `Utility.h`、`DataStruct.h`。
> **本文档只做复核与修法说明，不改任何源代码。**

---

## 0. 结论速览

方向是对的，`Convert.h` 从 448 行降到 168 行，`content` 已经变成 POD 快照。
但目前是**半成品**：**7 处编译不过 + 5 处逻辑错误 + 性能目标没达成**。

| 类别 | 数量 | 说明 |
|---|---|---|
| A. 编译不过 | 7 | 必须修，否则编不过 |
| B. 逻辑错误（能编过也是错的） | 5 | 其中 1 处会让下游脚本直接报错 |
| C. 性能回退 | 2 | 这次改造的核心目的没达成 |
| D. 健壮性 / 清理 | 7 | 建议一起修 |
| E. 有意的行为变更，需要你确认 | 3 | 不一定错，但要知情 |

---

## A. 编译不过（7 处）

### A1. `FormatContent` 里 `r` 从未声明（3 处）

`WriteFileContent.h:95` 签名是 `std::string FormatContent(const content& c)`，
但函数体里（`:103`、`:165`、`:245` 起）直接用了 `r.strategyName`、`r.pairId`、`r.algoType`……
**`r` 没有声明**。

修法：每个分支开头加一行绑定。

```cpp
std::string FormatContent(const content& c) {
    if (c.type == 1) {
        const stra::QuantOrderRecord& r = c.quantOrder;      // ← 缺这行
        ...
    }
    else if (c.type == 2) {
        const stra::PairOrderRecord& r = c.pairOrder;        // ← 缺这行
        ...
    }
    else if (c.type == 3) {
        const stra::AlgoOrderRecord& r = c.algoOrder;        // ← 缺这行
        ...
    }
}
```

### A2. `RotateAll` 调用了但从未定义

`WriteFileContent.h:326` 调用 `RotateAll(date)`，类里没有这个成员函数。
（全仓 grep 只有这一处出现。）

修法（注意 `clear()` 那行，见 B5）：

```cpp
void RotateAll(const std::string& newDate) {
    for (FileSink& s : mSinks) {
        if (s.opened) {
            s.f.flush();
            s.f.close();
            s.opened = false;
        }
        s.headerWritten = false;
        s.f.clear();          // ★ close() 之后必须 clear()，否则下次 open() 会静默失败
    }
    mCurDate = newDate;
}
```

### A3. `HeaderOf` 的参数类型不匹配

`WriteFileContent.h:337` 写的是 `sink.f << HeaderOf(c.type);`，
但 `HeaderOf` 的签名是 `std::string HeaderOf(const content& c)`（`:51`）——
**把 `int` 传给 `const content&`，编译不过**。

修法：把参数改成 `int`，内部所有 `c.type` 改成 `ty`。

```cpp
std::string HeaderOf(int ty) {
    if (ty == 1) { ... }
    else if (ty == 2) { ... }
    else if (ty == 3) { ... }
    return "";               // ← 顺带解决 D6 的缺 return
}
```

### A4. `HeaderOf` type 3 分支缺分号

`WriteFileContent.h:79` 结尾是
`"...ttTargetVolume,fishingSlippagePct,activeTrade"` —— **后面没有 `;`**。
（`:69` 的 type 2 分支有 `;`，`:59` 的 type 1 也有。）

### A5. `FormatContent` type 1 第 139 行用了 `;` 而不是 `,`

```cpp
:139                r.dbp.generateTs;          // ← 这里应该是 ,
:140                r.dbp.activeDepthTs,
```

`;` 会把 `fmt::format(` 的括号提前截断，导致 `:144` 的 `)` 无法匹配。

### A6. 文件末尾 `#endif` 没有对应的 `#if`

`WriteFileContent.h:1` 是 `#pragma once`，但 `:395` 写了 `#endif` ——
**没有 `#if`/`#ifdef` 开头，`#endif` 是语法错误**。删掉 `#endif` 即可。

### A7. 枚举映射表用 `int` 作 key（约 20 处）

`FormatContent` 里所有 `XxxEnum2Str[...]` 传的都是 `int`（record 里存的是原始枚举值），
但 C++ **不允许 int 隐式转 enum**，而这些 map 的 key 都是枚举类型：

| map | key 类型 | 定义位置 |
|---|---|---|
| `OrderTypeEnum2StrMap` | `OrderType` | `include/data_struct.h:112` |
| `DirectionEnum2StrMap` | `Direction` | `include/data_struct.h:154` |
| `OrderStatusEnum2StrMap` | `OrderStatus` | `include/data_struct.h:182` |
| `stra::AlgoTypeEnum2Str` | `AlgoType` | `DataStruct.h:273` |
| `stra::AlgoOrderStatusEnum2Str` | `AlgoOrderStatus` | `DataStruct.h:108` |
| `stra::DriveTypeEnum2Str` | `DriveType` | `DataStruct.h:173` |
| `stra::CheckTypeEnum2Str` | `CheckType` | `DataStruct.h:198` |
| `stra::TradingTypeEnum2Str` | `TradingType` | `DataStruct.h:364` |
| `stra::TargetSpredPriceEnum2Str` | `TargetSpredPrice` | `DataStruct.h:321` |
| `stra::ActiveVolumeCalcualteTypeEnum2Str` | `ActiveVolumeCalcualteType` | `DataStruct.h:341` |

修法：全部显式转换。建议加一个两行的辅助模板，避免 20 处手写：

```cpp
template <typename E>
inline E AsEnum(int v) { return static_cast<E>(v); }
```

然后：

```cpp
OrderTypeEnum2StrMap[AsEnum<OrderType>(r.orderType)],
DirectionEnum2StrMap[AsEnum<Direction>(r.direction)],
OrderStatusEnum2StrMap[AsEnum<OrderStatus>(r.orderStatus)],
stra::AlgoTypeEnum2Str[AsEnum<stra::AlgoType>(r.algoType)],
stra::TradingTypeEnum2Str[AsEnum<stra::TradingType>(r.tradingTypeOrder)],
stra::CheckTypeEnum2Str[AsEnum<stra::CheckType>(r.activeDepthMakerCheckType)],
...
```

> **顺带一个坑**：这些 map 是 `std::unordered_map`，`operator[]` 在 key 不存在时**会插入**。
> `BaseAlgoOrder` 的 `activeDepthMakerCheckType` / `activeDepthTakerCheckType` /
> `passiveDepthMakerCheckType` / `passiveDepthTakerCheckType` 四个字段
> **本身就没有初始化**（`BaseAlgoOrder.h:36/38/49/51` 无 NSDMI，
> 构造函数里对应的赋值在 `BaseAlgoOrder.cpp:39-48` 被注释掉了），
> 所以这里传进来的很可能是垃圾值 → `operator[]` 会往静态 map 里插一堆垃圾 key。
> 建议同时在 `BaseAlgoOrder` 构造函数里把这四个字段初始化成 `stra::CheckType_MIN`。

---

## B. 逻辑错误（能编过也是错的）

### B1. ⚠️ type 2 的表头写成了 algoOrder 的表头（影响下游）

| | 表头 | 数据 | 文件名 |
|---|---|---|---|
| type 1 | 35 列（quantOrder）✓ | 35 列 ✓ | `_quantOrder.csv` ✓ |
| **type 2** | **55 列（algoOrder 表头）✗** | **48 列（pairOrder）✗** | `_pairOrder.csv` ✓ |
| type 3 | 57 列 ✓ | 57 列 ✓ | `_algoPairOrder.csv` ✓ |

`WriteFileContent.h:62-69` 的 type 2 分支是从 `WriteAlgoPairOrderToFile` 复制过来的，
列名是 `algoType,algoStrategyName,algoOrderId,...`，应该是原来 `WritePairOrderToFile` 的 48 列
（`pairId,algoPairId,strategyName,baseAsset,tradingTypeOrder,tradingTypeOffset,targetVolume,...`）。

**影响是实打实的**：`script/read_pair_order.py` 里有
`df = df[df['passiveInstrumentKey'] == ...]` 和 `df[df['status'] == 0]`，
表头错了会直接 **KeyError**；`script/Run.py:16` 也会把 `_pairOrder.csv` 喂给统计脚本。

修法：把 type 2 的表头换回原来的 48 列版本（见原实现 `git show HEAD:quant_library/basic/WriteFileContent.h`）。

### B2. 表头后面缺 `"\n"`

`WriteFileContent.h:337`：`sink.f << HeaderOf(c.type);` ——
原来的写法是 `f << "header..." << "\n";`。**少了换行，表头会和第一条数据粘在同一行**。

修法：`sink.f << HeaderOf(c.type) << "\n";`

### B3. `WriteAlgoOrder` 一律用 `type = 3`，Fishing / Rebalance 丢了独立文件

`Convert.h:90` 无条件 `c.type = 3;`，而原实现是三个分支分别 `type = 3 / 4 / 5`，
对应三个文件：`_algoPairOrder.csv` / `_alogFishingOrder.csv` / `_algoRebalanceOrder.csv`
（注意原文件名里 `alog` 少个 i，是历史拼写，改名字之前先确认没人依赖）。

现在 Fishing / Rebalance 的算法单会全部写进 `_algoPairOrder.csv`。

好消息是：`script/` 下的脚本只引用 `_pairOrder.csv` 和 `_quantOrder.csv`，
没有引用 algo 系列文件，所以**下游影响为零**；但落库数据本身是错的。

修法（二选一）：
- **(a) 恢复三分支**：`Convert.h` 按 `ord->algoType` 设 `c.type = 3/4/5`；
  `SuffixOf` / `HeaderOf` / `RecordTimeUs` / `FormatContent` 都要加 4、5 两个分支，
  `mSinks` 改成 `[5]`，索引用 `c.type - 1`。
- **(b) 合并成一个文件**（当前实际效果）：保留 `type = 3`，
  但要明确接受「三种算法单混在一个文件里」，并把文件名从 `_algoPairOrder.csv` 改成
  `_algoOrder.csv` 之类的通用名，避免误导。

我个人建议 **(a)**，因为「一个文件一类单」的现状是有意设计的，且 (b) 会让 `algoType` 列之外的
`fishingSlippagePct`/`activeTrade` 两列对 PairTrading 单永远是空值。

### B4. ⚠️ `content()` 不会初始化 union，未赋值的字段是垃圾值

`Utility.h:21-29`：

```cpp
struct content {
    int type{0};
    union {
        stra::QuantOrderRecord quantOrder;
        stra::PairOrderRecord  pairOrder;
        stra::AlgoOrderRecord  algoOrder;
    };
    content() {}                       // ← 这行不会执行任何 NSDMI
};
```

我实测验证过（clang, `-std=gnu++17`）：

```cpp
struct S { int a{0}; char b[8]{""}; double c{0}; };
struct content { int type{0}; union { S quantOrder; S pairOrder; }; content() {} };
content c;
printf("a=%d b0=%d c=%f\n", c.quantOrder.a, c.quantOrder.b[0], c.quantOrder.c);
// 输出: a=1804260288 b0=1 c=0.000000     ← a 和 b0 是垃圾值
```

**用户提供的构造函数不会初始化任何 variant member，NSDMI 全部不生效。**

后果（结合 B3 就是当前的实际 bug）：
`FormatContent` 的 type 3 分支**无条件输出** `r.fishingSlippagePct`（`:314`）和
`r.activeTrade`（`:315`），而 `Convert.h:156-163` 只在 `algoType` 匹配时才赋值。
→ **PairTrading 算法单落库时，这两列是垃圾值。**

修法（最小改动）：

```cpp
content() { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }
```

三个 record 都是纯 POD（我验证过 `__is_trivially_copyable(content) == 1`），
整体清零是安全的。

> 更严格（完全避免 union 的非活跃成员访问）的写法是把 union 换成一个字节缓冲：
> ```cpp
> struct content {
>     int type{0};
>     unsigned char raw[sizeof(stra::PairOrderRecord)]{};
> };
> ```
> 生产者 `memcpy(&c.raw, &rec, sizeof(rec))`，消费者 `*reinterpret_cast<const ...*>(c.raw)`。
> 不引入 UB，但可读性差一点。**只清零 + 保留 union 就够了。**

### B5. `AlgoOrderRecord::updateTime` 没有初始化器

`DataStruct.h` 的 `AlgoOrderRecord` 里其它字段都写了 `{0}` / `{""}`，
只有 `int64_t updateTime;` 是裸的。

单独看影响不大（`Convert.h:98` 会赋值），但配合 B4 就是双重不确定。
建议统一补成 `int64_t updateTime{0};`。

---

## C. 性能回退（这次改造的核心目的没达成）

### C1. `Run()` 完全没改

`WriteFileContent.h:381-392` 还是原样：

```cpp
void Run() {
    while (running) {
        try {
            content c;
            if (contentQueue.Pop(c)) {   // 一次只取 1 条
                WriteFile(c);
            }
        } catch(exception& e) {          // 空 catch
        }
        usleep(1000);                    // 每条固定睡 1ms
    }
}
```

**写线程吞吐上限仍然是约 1000 条/秒，而且积压后永远追不上。**

### C2. 单条记录在写线程上更慢了

`WriteFile` 现在每条都要做 `CovertToUtcDate(RecordTimeUs(c))` + `FormatContent(c)`，
比原来「直接写一个现成的 string」**更重**。
C1 + C2 叠加的结果是：**行情线程轻了，但写线程成了新瓶颈，`contentQueue` 会持续积压。**

修法（这两条必须一起做）：

```cpp
void Run() {
    while (running) {
        bool idle = true;
        content c;
        while (contentQueue.Pop(c)) {          // ★ 排空，而不是一次一条
            idle = false;
            try {
                WriteFile(c);
            } catch (const std::exception& e) {
                LOG_ERROR("WriteFileContent failed, type:{} err:{}", c.type, e.what());
            }
        }
        if (idle) {                            // ★ 只有真空了才睡
            usleep(1000);
        }
    }
}
```

另外 `CovertToUtcDate` 每条都调一次（`gmtime` + `strftime` + `string` 构造）。
既然已经缓存了 `mCurDate`，可以只在 `date != mCurDate` 时才算 —— 现在的写法是**每条都算**，
只是**比较结果**才决定轮转。改成「先比较再算」不现实（要算才知道日期），
但可以按秒缓存：`nowSec` 没变就复用上次的日期字符串。

---

## D. 健壮性 / 清理

### D1. `Stop()` 定义了但没人调用，`running` 不是 atomic

- 全仓 grep `WriteFileContent::GetInstance()` 只有 `AlgoContext.cpp:45` 一处（只启动，不停止）
- `running` 是普通 `bool`（`WriteFileContent.h:25`），`Stop()` 的注释里自己写了
  「要改成 std::atomic<bool>」但没改 → **写线程读、主线程写，数据竞争**
- 类现在没有析构函数了（原 `~WriteFileContent() {}` 被删掉），`runningThread` 泄漏

修法：

```cpp
std::atomic<bool> running{true};
```
并在 `PairTradingStrategy::pre_stop()`（`src/strategy/PairTradingStrategy.cpp:71-76`，
现在已经在那里存 CSV 快照）里加一行：

```cpp
WriteFileContent::GetInstance().Stop();
```

> 背景：`src/main.cpp:13-21` 的 `signal_handler` 只调 `pre_stop()` 就 `exit()`，
> `exit()` 会执行静态析构，而写线程还在循环里访问全局 `contentQueue`
> （定义在 `BaseAlgoOrder.cpp:11`）→ 可能访问已析构对象。
> 这个隐患现在就有，但**改成缓存 ofstream 之后，未 flush 的缓冲区会直接丢**，问题会变明显。

### D2. `RecordTimeUs` 混用了两种时钟，且可能返回 0

```cpp
int64_t RecordTimeUs(const content& c) {
    if (c.type == 1) return c.quantOrder.updateTime;   // 来自 crypto::getCurrentTime()（墙钟）
    if (c.type == 2) return c.pairOrder.updateTime;    // 来自 GetCurrentTimeUs()（high_resolution_clock）
    if (c.type == 3) return c.algoOrder.updateTime;    // 来自 crypto::getCurrentTime()（墙钟）
}
```

两个问题：

1. **两种时钟混用**。`PairOrder::updateTime` 在 `AlgoPairOrder.cpp:594` /
   `AlgoRebalanceOrder.cpp:297` / `AlgoFishingOrder.cpp:435` 被赋值为 `GetCurrentTimeUs()`，
   而它用的是 `high_resolution_clock`（`Utility.h:38-40`）。
   在 **libc++ 下 `high_resolution_clock` 是 `steady_clock`**（开机以来的单调时钟，不是 Unix 墙钟），
   libstdc++ 下才是 `system_clock`。也就是说**换个编译器，quantOrder 和 pairOrder 会算出两个完全不同的日期**，
   `mCurDate` 会来回跳，文件被反复 close/open。
2. **可能为 0**。`CovertToUtcDate(0)` 返回 `"0"`，文件名会变成 `0_pairOrder.csv`，
   下一条正常记录又轮转回去 → 反复轮转 + 产生垃圾文件。

修法：

```cpp
int64_t RecordTimeUs(const content& c) {
    int64_t t = 0;
    if (c.type == 1)      { t = c.quantOrder.updateTime; }
    else if (c.type == 2) { t = c.pairOrder.updateTime; }
    else if (c.type == 3) { t = c.algoOrder.updateTime; }
    return (t > 0) ? t : crypto::getCurrentTime();      // ★ 兜底，避免 "0_xxx.csv"
}
```

**并且强烈建议顺手把 `GetCurrentTimeUs()` 改成 `system_clock`**（`Utility.h:38-40`）：

```cpp
inline int64_t GetCurrentTimeUs() {
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}
```

理由：全仓有 16 处 `GetCurrentTimeUs()`，其中 `PairInfoManager.cpp:178/188/317/504` 把它赋给
`pi->modifyTime`，而 `PairInfoManager.cpp:14/16` 又用 `crypto::getCurrentTime()` 赋同一个字段
——**同一个字段两种时钟**。现在能跑是因为部署在 libstdc++ 上，这是运气不是设计。

### D3. `mSinks[c.type - 1]` 没有边界检查

`mSinks` 只有 3 个元素（`:20`）。如果将来有人在某个调用点设了 `type = 4`，
就是**数组越界**（UB，可能写坏相邻内存）。

修法：`WriteFile` 开头加一道：

```cpp
if (c.type < 1 || c.type > (int)(sizeof(mSinks) / sizeof(mSinks[0]))) {
    LOG_ERROR("invalid content type: {}", c.type);
    return;
}
```

### D4. `WriteAlgoOrder` 没判空

`Convert.h:88` `inline void WriteAlgoOrder(BaseAlgoOrder* ord)` 直接解引用 `ord`。
原实现也没判，但现在是**提前**把所有字段拷一遍，风险面一样。加一行 `if (!ord) return;` 很便宜。

### D5. 显式 include

`WriteFileContent.h` 只 `#include "Utility.h"`，但用到了 `std::ofstream`、`std::thread`、
`struct stat`、`std::vector`。现在能过是因为这些通过 `Utility.h → DataStruct.h → ...` 间接进来，
换个头文件就可能挂。建议显式加：

```cpp
#include <fstream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <sys/stat.h>
```

### D6. 四个函数都有分支缺 `return`

`SuffixOf`（`:39-49`）、`HeaderOf`（`:51-81`）、`RecordTimeUs`（`:83-93`）、
`FormatContent`（`:95-320`）末尾都没有 `return`。
对返回 `std::string` 的函数来说，「跑到函数末尾还没 return」是 **UB**（不是简单地返回空串）。

修法：每个函数末尾补一个兜底返回（`HeaderOf` 返回 `""`，`SuffixOf` 返回 `""`，
`RecordTimeUs` 返回 `crypto::getCurrentTime()`，`FormatContent` 返回 `""`）。
配合 D3 的范围检查，正常路径不会走到，但必须写。

### D7. 表头里历史遗留的拼写

`HeaderOf` type 1（`:57`）的 `passiveAsk1Price1` —— 原实现就是这样（应为 `passiveAskPrice1`）。
下游脚本按列名读，**改名字会破坏兼容**，建议保持原样，只在注释里标一下。

---

## E. 有意的行为变更，需要你确认

### E1. `{:.13f}` 全部去掉了

原实现里 quantOrder 有 14 个、pairOrder 有 24 个 `{:.13f}`，现在**全部是裸 `{}`**。

- **好处**：省掉定点格式化的慢路径（这是性能优化的重点之一），输出变短。
- **风险**：`{}` 对 double 用的是「最短可往返表示」，极小/极大的值会输出**科学计数法**
  （如 `1e-17`）。我确认了 `script/` 下的脚本都用 `pandas.read_csv` + **按列名取值**
  （`row['makerTakerFs']`、`df['status']`、`df['passiveInstrumentKey']`），
  pandas 能正确解析科学计数法，所以**下游是安全的**。
  但如果有别的消费方（MySQL 导入、Excel、按固定宽度的解析）就需要再确认。

如果确定要保留 13 位小数，建议只在**价格/量**这类字段上用 `{:.13f}`，其余用 `{}`——
既保证精度又只付一次慢路径的代价。

### E2. quantOrder 表头新增了 `generateTs` 列

`HeaderOf` type 1 现在是 35 列（原 34 列），多的是 `generateTs`，数据侧也确实输出了。
按列名读取的 pandas 脚本不受影响（新列只是多出来）。
**但**：`script/Run_Quick.py:844` 有 `str_name.rstrip("pairOrder.csv")` 这类字符串处理，
建议扫一遍有没有依赖列数的逻辑。另外新旧文件表结构不一致，混在一起统计时要注意。

### E3. `BaseAlgoOrder` 的四个 `CheckType` 字段本身未初始化

见 A7 的说明。这不是本次改动引入的，但本次改动把它**固化进了落库记录**，
所以现在会被写进 CSV。建议顺手在 `BaseAlgoOrder` 构造函数里初始化。

---

## F. 建议的修复顺序

1. **A 组全修**（否则编不过）：`r` 绑定 → `RotateAll` 实现 → `HeaderOf(int)` → 两处分号 →
   删 `#endif` → 枚举 cast。
2. **B1 + B2**（表头错误 + 缺换行）：这两个会让所有落库数据不可用。
3. **B4**（`content()` 清零）+ **B5**（`updateTime{0}`）。
4. **B3**：决定 Fishing/Rebalance 是恢复独立文件还是合并，然后改 `SuffixOf`/`HeaderOf`/
   `RecordTimeUs`/`FormatContent`/`mSinks`。
5. **C1 + C2**：`Run()` 排空 + 非空 catch + 按需 sleep。**这条不做，前面全白改。**
6. **D1**（`Stop()` + atomic + `pre_stop` 调用）、**D2**（时钟统一 + 兜底）、
   **D3/D6**（边界检查 + 兜底 return）、**D5**（显式 include）。
7. **验证**：保留一份旧实现做新旧双写 diff（类型不匹配的字段搬运不会报错，只能靠比对）。

---

## G. 复核确认过、没问题的部分

为了不让你重复检查，这些我逐项核对过了：

- 三个 `fmt::format` 的**占位符数量与参数个数完全匹配**（type1 35=35、type2 48=48、type3 57=57）
- `content` 是 `trivially_copyable`（实测 `__is_trivially_copyable(content) == 1`），
  `moodycamel::ConcurrentQueue` 入队没问题
- `WriteQuantOrder` / `WritePairOrder` 的字段**赋值覆盖完整**（record 里每个字段都被显式赋值了）
- `DbpSnapshot::From` 做了判空，顺手修掉了 `BaseAlgoOrder.cpp:713` 的潜在空指针解引用
- `pubsetbuf` 在 `open()` 之前调用 ✓（顺序对了）
- `FileSink::buf` 是成员变量 ✓（不是局部数组，不会被回收）
- `OpenSink` 用 `::stat` + `st_size > 0` 判断表头 ✓（修掉了原实现「残留 0 字节文件导致跳过表头」的问题）
- 三个表头的列数与各自的数据列数**在 type1/type3 上是一致的**
- 下游脚本只依赖 `_quantOrder.csv` / `_pairOrder.csv` 两个文件名，`SuffixOf` 的前两个后缀保持原样 ✓
- `CovertToUtcStr` 是死代码（全仓无调用），所以 `gmtime` 的非可重入问题暂时没有实际竞争
