# 算法单执行 + 全链路串联 测试套件

覆盖 **算法单对象 → 拆单报单 → 回报更新 → 撤单 → 终结** 这一段
（`AlgoContext` / `BaseAlgoOrder` / `AlgoPairOrder` / `PairManager` / `OrderManager` /
`PositionManager`），外加两条把策略侧和执行侧**串起来跑**的全链路：

```
G. 成交闭环   行情 → 建单 → 拆单报主动腿 → 成交 → 补报被动腿 → 被动腿成交 → 收口
H. 撤单闭环   行情 → 建单 → 拆单报主动腿 → 风控命中 → 撤单 → 撤在途腿 → OnTimer → 终结
```

与 `../market_to_algo` 的分工：

| 套件 | 范围 | 边界 |
|---|---|---|
| `../market_to_algo` | 策略侧「行情 → 创建算法单」 | 算法单对象交出去为止，执行端是桩 |
| `algo_exec`（本套件） | 执行侧「算法单 → 子单 → 回报 → 更新 → 撤单」 | 出向指令换成进程内记录器；策略侧桩换成**真实** `PairTradingContext` 来跑 G/H 两条链 |

两边共用 `../market_to_algo/stubs/ext/**`（dbp / pubsub / fmt / securitymanager / …），
不重复造桩。

## 怎么跑

```bash
./run.sh              # 编译并运行
./run.sh -v           # 额外打开 -Wall -Wextra（suite.cpp 自身零 warning）
CXX=g++-13 ./run.sh   # 指定编译器
```

**编译器自动挑选**，顺序是 `$CXX` → `/usr/bin/g++` → `/usr/bin/c++` → `/usr/bin/clang++`
→ PATH 上的 `g++` → `c++` → `clang++`；挑到后会打印用的是哪一个，并校验可执行
（不合格会打印中文错误并 `exit 1`）。目标环境是 Ubuntu 服务器（g++ + 系统已装的三方库），
macOS 上 clang++ 也能跑。

> 注意 **macOS 上 `/usr/bin/g++` 是 clang++ 的软链**，所以这个顺序两边都安全。

期望输出结尾：

```
PASS: 140   FAIL: 0
ALL PASS
```

## 完全自足，不需要任何外部依赖

**不读配置文件、不起 `dbprocess` / `tb`、不碰共享内存、不联网、不需要交易所。**

所有输入都是进程内手工构造的：`dbp::DbpTopic` / `DbpData`（行情与价差）、
`pubsub::OrderResponse`（回报）、`pubsub::Balance`（资金）。
**唯一的外部边界是出向指令** —— `om::TradeClient` 被换成一个进程内记录器，
持有 `std::vector<pubsub::TCommand>`，于是报单/撤单/查询可以逐字段断言：

```cpp
fx.NNew();                 // 报单条数
fx.NCancel();              // 撤单条数
fx.NQuery();               // 查单条数
fx.News();                 // 只挑 CMD_NEW_ORDER
fx.Cancels();              // 只挑 CMD_CANCEL_ORDER
fx.NewBySoid(clientOrderId);
```

编译产物只链标准库与 libc，无落盘副作用（快照/日志/发 redis 全部是 no-op 桩）。

## 为什么需要一棵临时编译树

这台机器上编译不了整个工程：缺 `fmt` / `rapidjson` / `boost` / `dbp` / `pubsub` /
`securitymanager`，`build/` 里的 include 路径还指向部署机 `/workspace/...`。

所以 `run.sh` 会搭一棵 `/tmp/algosuite_build` 临时树，把**真实源码软链进去、只给外部依赖打桩**：

| 类别 | 内容 |
|---|---|
| 真实源码（软链，零改写） | `algo/AlgoContext.cpp/.h`、`algo/PairTradingContext.cpp/.h`、`basic/DataStruct.h`、`basic/BaseAlgoOrder.cpp/.h`、`basic/AlgoPairOrder.cpp/.h`、`basic/PositionManager.*`、`basic/OrderManager.*`、`basic/PairManager.*`、`basic/LimitManager.*` + `LimitBoard.*`、`basic/AlgoOrderManager.*`、`basic/AccountManager.*`、`basic/SpreadManager.*`、`basic/QuantTrade.h`、`basic/PairInfo.h`、`basic/PairInfoManager.*`、`signal/SignalGenerator.*`、`signal/SpreadStatsBuilder.*`、`risk/RiskManager.*` |
| 桩（`stubs/basic`，只补外部依赖和纯副作用） | `Utility.h`（`ConcurrentQueue` 换成单线程 `deque`，丢掉 moodycamel / `perf.h`）、`Convert.h`（落盘日志 → no-op）、`QuantPub.h`（发 redis → no-op）、`QuantDbp.h` / `LarkRebot.h` / `WriteFileContent.h`（同上）、`StrategyConfig.h`（读 ini 的 boost 换成固定值）、`AlgoFishingOrder.h` / `AlgoRebalanceOrder.h`（只留类声明，`AlgoContext` 里有 `static_cast`） |
| 桩（`../market_to_algo/stubs/ext`，与策略侧共用） | `data_struct.h`、`pubsub_protocol.h`、`dbp/include.h`、`fmt`、`json`、`time_util.h`、`log_engine.h`、`securitymanager.h`、`StraException.h`、`crypto_errors.h`、`command_helper.h`（`om::TradeClient` 记录器） |

### 为什么这次能用真实 `DataStruct.h`

`market_to_algo` 里 `DataStruct.h` 是桩（只留 `stra::` 常量/枚举 + `AlgoOrderModify`）。
执行侧不行：`AlgoContext.cpp` 会直接读 `stra::TradingTypeEnum2Str` / `DirectionEnum2StrMap`
/ `OrderTypeEnum2StrMap` / `OrderStatusEnum2StrMap` 这些 **map**，还会用 `stra::MIN_FLOAT`
/ `stra::MSG_LEN` 做数值判定。桩一份 map 就等于把"枚举 → 字符串"的语义抄了一遍，
一旦生产加了枚举值就漂移。所以这里软链真实的 `DataStruct.h`，让编译器自己去校验。

## 怎么绕过 private

`AlgoContext::OnOrder` / `OnTimer` / `SubmitAlgoOrder`、`BaseAlgoOrder::CancelOrderOnSpread`
/ `PairOrderTrade`、`AlgoPairOrder::GetTargetPairOrder` 这些目标方法**全是 private**，
`orderMgr` / `pairOrderMgr` 这些容器也是。

`suite.cpp` 用：

```cpp
// ⚠️ 所有 std 头必须先包含完，否则会破坏标准库内部的访问限定
#include <algorithm> ... <vector> <queue> <unordered_set> <thread> <optional> <random>

#define private public
#include "algo/AlgoContext.h"
#include "algo/PairTradingContext.h"
#include "basic/SpreadManager.h"
#include "basic/AccountManager.h"
#include "basic/LimitManager.h"
#include "basic/AlgoOrderManager.h"
#include "basic/AlgoPairOrder.h"
#include "basic/QuantTrade.h"
#undef private
```

这样测的是**真实的类、真实的成员状态、真实的调用链**。代价有两个：

1. `std` 头必须在宏生效之前先包含完（`suite.cpp` 顶部那份清单是**刻意冗余**的 ——
   libstdc++ 比 libc++ 更容易在这里炸）。
2. **单例的头必须包含在宏区域内** —— `SpreadManager` / `AccountManager` / `LimitManager`
   的私有容器要在 `ResetWorld()` 里清空，否则用例之间会互相污染。

## 用例之间的隔离：`ResetWorld()`

执行侧有五个全局/单例在用例之间**不会自己清**，每个 `Boot()` 开头统一重置：

| 对象 | 为什么必须清 |
|---|---|
| `SpreadManager` | `mSpread` 是 `unordered_map<string, unique_ptr<DbpData>>`，`AddSpreadPara` 有 `find != end → return` 的短路。不清就会带着上一个用例的 `pdata`（含 `generateTs`），新用例的"行情延迟"判定全乱 |
| `AccountManager` | 账户余额是累加的，不清会带着上一个用例的 1e6 |
| `LimitManager` | 限流计数（`PassLimit` / `PassCancelLimit`）是滑动窗口，不清会把后面的报单挡掉 |
| `StrategyConfig` | 账户 → strategyId 映射表 |
| `mAccountNameAccountId` | 全局表（`AlgoContext.cpp` 里定义），`LimitManager::Init` 遍历它 |
| `PairInfoManager` | 策略侧那本账（G/H 两条链要用） |

## 几个必须先知道的执行侧语义

写执行侧用例时最容易踩的不是"桩不准"，而是**对生产语义的想当然**。
下面每条都对应本套件里一条踩过的坑，注释也写在了 `suite.cpp` 对应位置。

### 1. 子单报出去时是 `OS_PEND`，不是 `OS_NEW`

`PairOrder::CreateActiveOrder` 里 `order.orderStatus = OS_PEND`（PairManager.cpp:128）。
而 `CancelOrderOnSpread` 只处理 `OS_NEW || OS_PARTFILLED || OS_FILLED` ——
**任何撤单用例都必须先喂一条交易所确认回报把子单推到 `OS_NEW`**，
否则撤单路径根本进不去，表现为"撤单条数 = 0"（D3 / E1 / E3 / H4 都栽在这上面）。

### 2. 终态子单会被从 `orderMgr` 删掉，不是留在里面变 `OS_FILLED`

`AlgoContext::OnOrder` 对 `FILLED` / `REJECTED` / `CANCELED` 三种终态会调
`UpdateAlgoPairOrderByDeleteQuantOrder` → `orderMgr.DeleteOrderByOrder`
（BaseAlgoOrder.cpp:160-172，注释是"订单完结解冻"）。

所以**主动腿成交之后，`orderMgr` 里留下的只有那条新报的被动腿**，
成交量要去看 `pairOrder.activeTotalVolumeOnOrder`（C1 / G3）。

### 3. `strategyRef` 的格式是 `algoPairId_pairId`，不是 `algoOrderId_pairId`

`QuantTrade::CreateOrder` 里 `sprintf(ref, "%ld_%ld", algoPairId, pairId)`。
`AlgoContext::OnOrder` 解析出 `v[0]` 当 `algoId`（拿去 `AlgoOrderManager` 查），
`v[1]` 当 `pairId`。而 `PairOrder.algoPairId` 在 `AlgoPairOrder::GetTargetPairOrder`
（AlgoPairOrder.cpp:573）里是从 `unsignedAlgoOrderId` 赋的，所以**两者当前恰好相等** ——
但这是巧合，用例里一律用 `MakeRef(o, pairId)` 生成，不要手写。

### 4. `CancelOrderOnSpread` 的两条分支不对称

| 分支 | 条件 | 撤谁 | 时间门槛 |
|---|---|---|---|
| `CANCELLING` | `algoOrderStatus == ALGO_OS_CANCELLING` | **只撤主动腿**（被动腿有在途单 = 主动腿已成交、存在单边敞口，必须等它成交把敞口平掉） | `nowTime - updateTime > 1000*10`（10ms） |
| `else` | 其他状态 | 按时间 / 被动腿价格偏离 / 主动腿价格偏离 三条规则撤 | 时间撤单有门槛；**价格偏离那两条没有** |

`AgeChildOrders(o, us)` 就是把子单的 `updateTime` 往回拨 —— 用来跨时间门槛，
比 `sleep` 稳（不受 CI 负载影响，也不拖慢用例）。

### 5. `RiskManager::RiskConfig` 里没有"强制平仓"开关

风控是**算出来的**，不是配置开的。`CheckRisk` 的第一条是 `CheckTinyClose`（碎单）：

```
|pairTotalVolume| > 1e-9                              有持仓
CalcPositionValue() = |pairTotalVolume * 腿价 * multiple| < 25 USDT
```

`ArmTinyCloseRisk(pi)` 就是构造这个条件（`pairTotalVolume = 0.01`、腿价 100、`multiple = 1`
→ 市值 1 USDT < 25）。这是一条**真实存在**的风控分支，不是为了测试新造的开关。

### 6. `OnAlgoOrderUpdate` 只对**终态**释放对子

`PairTradingContext.cpp:1011-1029` 分两档：

- 非终态 → 只同步量/价，**不释放对子、不做结算**；
- 终态 → `OnAlgoFinished` + `RecalcOrderParams` + `ClearActiveAlgoOrder`。

所以"两腿成交、pairOrder 收口"之后算法单状态仍是 `ALGO_OS_NEW`（四个 switch 还开着，
策略会继续追），`pi.hasActiveAlgoOrder` **仍然为 true**（G5 断言的就是这个）。

## 桩保真度（写桩时踩过的坑）

桩可以少实现，但**不能改变语义**。本套件踩到的前三个都是"静默假阴性"——
测试红了但原因不在被测逻辑上：

| # | 桩/夹具的错 | 症状 | 真值 |
|---|---|---|---|
| 1 | `StrategyConfig` 桩让 `GetTradesThreshold()` 返回 `0` | `tradesDelayThreshold = 0*1000 = 0`，`0 < 0` 恒假 → `curTradeDelay` 永远 false → **一条子单都报不出来** | 真实 `StrategyConfig.cpp:36` 是 `itemMd.get<int>("tradesshold", 1)`，注意键名拼写是 **`tradesshold`**，且 `etc/strategy.ini` 的 `[MD]` 段里**没有**这个键 → 走默认值 **1** |
| 2 | 夹具给两腿盘口填了**一样**的量（都 10000） | MT 单一条子单都报不出来（TT 单正常） | `GetTargetPairOrder` 的 MAKER_TAKER 分支有一道守卫（AlgoPairOrder.cpp:93-114）：`if (activeAskAmount1 >= passiveAskAmount1) return pairOrder;` → 返回空单。被动腿量必须**大于**主动腿（夹具用 30000） |
| 3 | 夹具给策略层喂的行情把 `spreadAskAsk` 也填成 `-1.0` | G 的"信号成立"建不出算法单（`created = 0`） | `SignalGenerator::CanOpen` 有 sanity（SignalGenerator.cpp:266-270）：`if (!isnan(s) && abs(s) > 0.1) → "spreadAskAsk abnormal > 10%" → 拒绝开仓`。而 `PairTradingContext::OnSpread` 会用 `pdata` 覆盖 `pi.rtSpread`。所以夹具分了两个构造器：`FreshData()`（执行侧，四个口径都给 `-1.0`）与 `FreshStrategyData()`（策略层，`spreadBidAsk = -1.0` 让 ttOL 触发，其余三个给 `-0.001`） |

> 第 1 条和第 2 条的共同教训：**桩返回一个"看起来无所谓"的默认值，会把被测逻辑悄悄关掉**。
> 写桩时要回去读真实实现的默认值，而不是随手填 `0`。

## 移植到 Linux / g++

脚本本身没有平台相关的东西，另外做了三件事：

| 项 | 说明 |
|---|---|
| 编译器自动挑选 + 校验 | 见上文。**macOS 上 `/usr/bin/g++` 是 clang++ 的软链**，所以 `g++` 优先的顺序两边都安全 |
| `-pthread` | 编译时带上。G/H 用到策略侧，单例里的锁在 glibc 下可能要求它 |
| bash 3.2 兼容 | 数组展开写成 `${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}`（macOS 自带 bash 是 3.2，`set -u` 下裸展开会报 unbound） |

Ubuntu 上如果装了多个 g++（`g++-13` 等），用 `CXX=g++-13 ./run.sh` 显式指定。

> **本机（macOS）无法验证 libstdc++ 那条构建路径** —— 这台机器上没有真的 GNU g++，
> `/usr/bin/g++` 其实是 clang。所以"`#define private public` 之前那份 std 头清单是否足够"
> 要在 Ubuntu 服务器上跑一次 `./run.sh` 才算确认。

## 覆盖范围

**40 个用例 / 140 条断言**，分八组：

| 组 | 主题 | 用例数 | 断言数 |
|---|---|---|---|
| A | 注册（`SubmitAlgoOrder`：入册 / 订阅价差 / 初始状态） | 4 | 12 |
| B | 拆单报单（`OnSpread`） | 5 | 29 |
| C | 成交回报（`OnOrder`） | 6 | 24 |
| D | 撤单（本地请求 → CANCELLING → 撤子单 → CANCELED） | 7 | 21 |
| E | 超时撤单（`OnSpread` 时间/价格撤单） | 3 | 10 |
| F | 风控平仓（`ProcessRisk` → 撤单 / 强平报单） | 3 | 13 |
| G | **全链路一：成交闭环** | 6 | 16 |
| H | **全链路二：撤单闭环（风控驱动）** | 6 | 15 |

逐条说明见 `../测试用例-算法单执行.md`。
