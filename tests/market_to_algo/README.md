# 行情 → 算法单 半程测试套件

覆盖 **行情进来 → 策略决策 → 创建算法单对象** 这一段（`PairTradingContext` /
`SignalGenerator` / `RiskManager` / `PairInfoManager`）。

算法单**执行**那一段（`AlgoContext::OnCommand` 建单后怎么报、怎么追、怎么撤）不在
本套件范围内 —— 那部分已经在 `OnCommand` 上单独测过了。本套件只到"把算法单对象
交出去"为止，用回调把对象截获下来断言它的字段。

## 怎么跑

```bash
./run.sh          # 编译并运行
./run.sh -v       # 额外打开 -Wall -Wextra
```

期望输出结尾：

```
PASS: 315   FAIL: 0
ALL PASS
```

## 完全自足，不需要任何外部依赖

**不读配置文件、不起 `dbprocess` / `tb`、不碰共享内存、不联网、不需要交易所。**
所有输入都是进程内手工构造的（`dbp::DbpTopic` / `DbpData` / `pubsub::Position` /
`Balance` / `TotalAccount`），报单与撤单出口换成回调记账。

编译产物只链两个系统库：

```
$ otool -L "$TMPDIR/ptsuite_build/suite"
	/usr/lib/libc++.1.dylib
	/usr/lib/libSystem.B.dylib
```

唯一落盘副作用是 `/tmp/ptsuite/out/*.csv`（快照往返那几个用例）。

## 为什么需要一棵临时编译树

这台机器上编译不了整个工程：缺 `fmt` / `rapidjson` / `boost` / `dbp` / `pubsub` /
`securitymanager`，`build/` 里的 include 路径还指向部署机 `/workspace/...`。

所以 `run.sh` 会搭一棵临时树，把**真实源码软链进去、只给执行端和外部依赖打桩**：

| 类别 | 内容 |
|---|---|
| 真实源码（软链，零改写） | `algo/PairTradingContext.cpp/.h`、`basic/PairInfoManager.cpp/.h`、`basic/PairInfo.h`、`signal/SignalGenerator.cpp/.h`、`signal/SpreadStatsBuilder.cpp/.h`、`risk/RiskManager.cpp/.h` |
| 桩 | `stubs/basic/DataStruct.h`（`stra::` 常量/枚举/`AlgoOrderModify` + `LOG_*`）、`stubs/basic/BaseAlgoOrder.h`（字段集与真实头一致，去掉方法声明）、`stubs/basic/AlgoPairOrder.h`（只留构造函数）、`stubs/ext/**`（dbp / pubsub / securitymanager / fmt / json） |

桩只补**外部依赖**，不改任何被测逻辑。`stubs/basic/BaseAlgoOrder.h` 的字段名、类型、
顺序都照抄真实头文件，因为 `BuildAlgoOrderJson` 写的就是这份字段契约。

## 怎么绕过 private

`PairTradingContext` 的目标方法（`ProcessPairSignal` / `ProcessRisk` /
`BuildAlgoOrderJson` / `SubmitAlgoOrder` / `CheckAlgoOrderTimeout` /
`CheckExposureAbnormal` / `ProcessModify` / `OnTimer` / `OnAlgoOrderUpdate` /
`TryReconcile` / `ReconcilePair` …）**全是 private**。

`suite.cpp` 用：

```cpp
// 所有 std 头必须先包含完，否则会破坏标准库内部的访问限定
#include <algorithm> ... <vector>

#define private public
#include "algo/PairTradingContext.h"
#undef private
```

这样测的是**真实的类、真实的成员状态、真实的调用链**，而不是把方法体抄进 `.inc`
（那种做法会随源码漂移）。代价是：`std` 头必须在宏生效之前先包含完。

## 覆盖范围

110 个用例 / 315 条断言，分十组：

| 组 | 主题 | 用例数 |
|---|---|---|
| A | 创建（`BuildAlgoOrderJson` / `SubmitAlgoOrder`） | 16 |
| B | 开仓（TT/MT × OL/OS） | 10 |
| C | 平仓（TT/MT × CL/CS） | 8 |
| D | 定时更新（`OnTimer` / `ProcessModify` / 快照周期） | 12 |
| E | 撤单（超时 / 敞口异常 / 异步语义） | 14 |
| F | 风控触发（`ProcessRisk` + 档位升级） | 14 |
| G | 重启（闸门 / 对账 / 快照 / 孤儿单） | 16 |
| H | 持仓推送（`OnPosition` + 闸门真实放行路径） | 14 |
| I | 资金推送（`OnBalance`，钉住当前行为含已知坑） | 5 |
| J | 账户总览推送（`OnTotalAccount`，当前是空实现） | 1 |

逐条说明见 `../测试用例-行情到算法单.md`。

### 桩保真度

桩可以少实现，但**不能改变语义**。踩过的坑记在
`../测试用例-行情到算法单.md` §6，其中最有代表性的一条：

`stubs/ext/fmt/core.h` 最早那版直接 `((os << args), ...)` 忽略格式串，于是
`fmt::format("{}.{}.{}", "BINANCE", "USDT_SWAP", "DOGE-USDT")` 返回
`"BINANCEUSDT_SWAPDOGE-USDT"`（两个点没了）—— `UpdateOnBalance` 拼的 `symKey`
因此永远匹配不上，资金推送的写入被静默吞掉，I2/I3 两条用例被这个桩骗红。
现在桩里实现了真正的 `{}` 顺序替换 + `{{`/`}}` 转义。
