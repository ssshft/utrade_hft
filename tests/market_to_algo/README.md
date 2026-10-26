# 行情 → 算法单 半程测试套件

覆盖 **行情进来 → 策略决策 → 创建算法单对象** 这一段（`PairTradingContext` /
`SignalGenerator` / `RiskManager` / `PairInfoManager`）。

算法单**执行**那一段（`AlgoContext::OnCommand` 建单后怎么报、怎么追、怎么撤）在
**另一个套件** `../algo_exec` 里覆盖，并且那边有两条把两边串起来跑的全链路
（行情 → 建单 → 拆单 → 成交 / 风控 → 撤单 → 终结）。本套件只到"把算法单对象
交出去"为止，用回调把对象截获下来断言它的字段。

## 怎么跑

```bash
./run.sh              # 编译并运行
./run.sh -v           # 额外打开 -Wall -Wextra
CXX=g++-13 ./run.sh   # 指定编译器
```

**编译器自动挑选**，顺序是 `$CXX` → `/usr/bin/g++` → `/usr/bin/c++` → `/usr/bin/clang++`
→ PATH 上的 `g++` → `c++` → `clang++`；挑到后会打印用的是哪一个，并校验可执行
（不合格会打印中文错误并 `exit 1`）。目标环境是 Ubuntu 服务器（g++ + 系统已装的三方库），
macOS 上 clang++ 也能跑。

期望输出结尾：

```
PASS: 484   FAIL: 0
ALL PASS
```

## 完全自足，不需要任何外部依赖

**不读配置文件、不起 `dbprocess` / `tb`、不碰共享内存、不联网、不需要交易所。**
所有输入都是进程内手工构造的（`dbp::DbpTopic` / `DbpData` / `pubsub::Position` /
`Balance` / `TotalAccount`），报单与撤单出口换成回调记账。

编译产物只链标准库与 libc：

```
$ ldd "$TMPDIR/ptsuite_build/suite"      # Linux
	libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6
	libm.so.6 => ...
	libgcc_s.so.1 => ...
	libc.so.6 => ...

$ otool -L "$TMPDIR/ptsuite_build/suite" # macOS
	/usr/lib/libc++.1.dylib
	/usr/lib/libSystem.B.dylib
```

唯一落盘副作用是 `/tmp/ptsuite/out/*.csv`（快照往返那几个用例，硬编码路径）。
重复运行不会互相影响 —— 每次运行都会先覆写这些文件。

## 为什么需要一棵临时编译树

这台机器上编译不了整个工程：缺 `fmt` / `rapidjson` / `boost` / `dbp` / `pubsub` /
`securitymanager`，`build/` 里的 include 路径还指向部署机 `/workspace/...`。

所以 `run.sh` 会搭一棵临时树，把**真实源码软链进去、只给执行端和外部依赖打桩**：

| 类别 | 内容 |
|---|---|
| 真实源码（软链，零改写） | `algo/PairTradingContext.cpp/.h`、`basic/PairInfoManager.cpp/.h`、`basic/PairInfo.h`、`signal/SignalGenerator.cpp/.h`、`signal/SpreadStatsBuilder.cpp/.h`、`risk/RiskManager.cpp/.h` |
| 真实公共头（软链） | `include/SpinBackoff.h`（仓库顶层，不属于 `quant_library`）。生产靠 `include_directories(${SYSTEM_INC_DIR})` 找到，测试树对应地用 `-I ext` 找。**当前已无人 include**（`signal_work` 的等待从 `spin::backoff_sleep` 改成了 `sleep_for`），软链保留备用 |
| 桩 | `stubs/basic/DataStruct.h`（`stra::` 常量/枚举/`AlgoOrderModify` + `LOG_*`）、`stubs/basic/BaseAlgoOrder.h`（字段集与真实头一致，去掉方法声明）、`stubs/basic/AlgoPairOrder.h`（只留构造函数）、`stubs/basic/LarkRebot.h`（飞书告警，接口照抄真实头、实现留空）、`stubs/ext/**`（dbp / pubsub / securitymanager / fmt / json / log_engine） |

桩只补**外部依赖**，不改任何被测逻辑。`stubs/basic/BaseAlgoOrder.h` 的字段名、类型、
顺序都照抄真实头文件，因为 `BuildAlgoOrderJson` 写的就是这份字段契约。

### boost 依赖（2026-10-16 起）

`PairTradingContext.h` 现在 include `<boost/lockfree/spsc_queue.hpp>` —— 分位数计算的分线程
通道用它做 SPSC 队列（见 `docs/thread_split_design.md` §8/§9）。它是 **header-only**，
不需要链库，但头必须找得到。`run.sh` 的做法是**先探测再决定**：

```bash
# 系统里没有真的 boost 头 -> 才把 stubs/noboost 加进 -I
if ! echo '#include <boost/lockfree/spsc_queue.hpp>' \
        | "$CXX" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    BOOST_INC=(-I "$HERE/stubs/noboost")
fi
```

| 环境 | 走哪条路 |
|---|---|
| Ubuntu 服务器 | 系统自带 boost -> 用**真的**（`stubs/noboost` 根本不进 include 路径，不会把真头挡掉） |
| macOS 开发机 | 没有 boost -> 用 `stubs/noboost/boost/lockfree/spsc_queue.hpp` 这个等价桩 |

桩只实现实际用到的成员（`push` / `pop` / `read_available` / `write_available`），语义严格
对齐 boost 文档：**满则丢最新**、空则返回 false。`#define private public` 对 boost 同样危险
（boost 头里 `private:` 极多），所以 `suite.cpp` 在宏生效**之前**就把 boost 头包含掉了。

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

> `suite.cpp` 顶部那份 std 头清单是**刻意冗余**的。项目头（`PairTradingContext.h` /
> `SignalGenerator.h` / `SpreadStatsBuilder.h` …）在宏生效期间被解析，它们内部还会
> include 若干 std 头；如果那些 std 头此刻才第一次被包含，它们的内部实现就会在
> `private` 已被改写成 `public` 的情况下解析。libc++ 上通常还能过，**libstdc++
> （Ubuntu 的 g++）更容易炸**。所以宁多勿少，让项目头里的 include 全部命中 guard。

## 异步的 T_signal 怎么测（`PumpSignal`）

2026-10-16 起，分位数计算（`SpreadStatsBuilder::Prune + Build`，实测 885 µs/对子）从行情线程
搬到了独立线程 **T_signal**（见 `docs/thread_split_design.md` §8）。行情线程只做
「5s 降频入队 + 消费统计」。

这带来一个测试问题：D4 / G17 / G19 / G20 的断言形态是
**「喂够样本 → `OnTimer` → 统计就该建立、`orderParams` 就该被重算」**，而 T_signal 是
异步线程 —— 测试里既不能等 `spreadStatsUpdateIntervalSec`（默认 60s），也不能依赖调度顺序。

所以 `suite.cpp` 有一个显式的驱动口：

```cpp
// 先停掉 T_signal（RunSignalOnce 会碰 m_spreadWindows / m_statsQ，
// 它们的所有权约定是单线程，两个线程同时进去就是数据竞争），
// 再在**调用线程**里跑一轮。
static void PumpSignal(PairTradingContext& ctx, int64_t nowUs) {
    ctx.StopSignalThread();
    int64_t lastStatsUpdateUs = 0; // 0 -> nowUs - 0 > 60e6 必然成立
    ctx.RunSignalOnce(nowUs, lastStatsUpdateUs);
}
```

`RunSignalOnce` 是**生产代码**里的一轮 T_signal 工作（`signal_work()` 循环调的就是它）。
所以测的是真实函数，不是抄一份逻辑到测试里。

> `RunSignalOnce` 曾经有个 `bool force` 形参，用来绕开「到没到 60s」的门控；后来去掉了。
> 现在靠 `lastStatsUpdateUs = 0` 达到同样效果 —— 门控是 `nowUs - lastStatsUpdateUs >
> spreadStatsUpdateIntervalSec * 1e6`，传 0 时左边就是整个 Unix 时间戳，恒成立。
> 生产路径不传 0：`signal_work()` 用它自己的真实时间，所以仍然只每 60s 建一次统计。

用法就是在 `OnTimer` 之前插一行：

```cpp
for (int i = 0; i < 5; ++i) fx.ctx.OnSpread(&topic, &d);
PumpSignal(fx.ctx, now + SEC);   // 显式驱动 T_signal 一轮
fx.ctx.OnTimer(now + SEC);       // 消费统计 -> UpdateLargeStats -> RecalcOrderParams
```

**只有 4 个用例会调 `OnSpread`**（D4 / G17 / G19 / G20），其余用例的样本窗口恒为空，
`Build` 从来不会为它们跑 —— 所以这次分线程改动不可能影响它们的行为。

> 局限：这样测的是「一轮 T_signal 的**逻辑**」，不是「线程本身真的跑起来了」。
> 线程的调度/退出路径没有确定性用例覆盖 —— 那是异步设计固有的代价，见
> `docs/thread_split_design.md` §10.4。

## 移植到 Linux / g++

脚本本身没有平台相关的东西，另外做了两件事：

| 项 | 说明 |
|---|---|
| 编译器自动挑选 + 校验 | `$CXX` → `/usr/bin/g++` → `/usr/bin/c++` → `/usr/bin/clang++` → PATH 上的 `g++` → `c++` → `clang++`。挑到后打印并校验可执行，不合格报中文错误并退出。注意 **macOS 上 `/usr/bin/g++` 是 clang++ 的软链**，所以这个顺序两边都安全 |
| `-pthread` | 编译时带上。2026-10-16 起**真的需要**了：`Init()` 会起 T_signal 线程（`PumpSignal` 随即把它停掉）。此外 `<atomic>` / 单例里的锁在 glibc 下本来就要求它 |

Ubuntu 上如果装了多个 g++（`g++-13` 等），用 `CXX=g++-13 ./run.sh` 显式指定。

> **本机（macOS）无法验证 libstdc++ 那条构建路径** —— 这台机器上没有真的 GNU g++，
> `/usr/bin/g++` 其实是 clang。所以上面那份"刻意冗余的 std 头清单"是否足够，
> 要在 Ubuntu 服务器上跑一次 `./run.sh` 才算确认。

## 覆盖范围

123 个用例 / 484 条断言（2026-10-26 实测），分十组：

| 组 | 主题 | 用例数 |
|---|---|---|
| A | 创建（`BuildAlgoOrderJson` / `SubmitAlgoOrder`） | 17 |
| B | 开仓（TT/MT × OL/OS） | 10 |
| C | 平仓（TT/MT × CL/CS） | 8 |
| D | 定时更新（`OnTimer` / `ProcessModify` / 快照周期） | 20 |
| E | 撤单（超时 / 敞口异常 / 异步语义） | 14 |
| F | 风控触发（`ProcessRisk` + 档位升级） | 14 |
| G | 重启（闸门 / 对账 / 快照 / 孤儿单 / **分位数快照免预热**） | 20 |
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

### 桩漂移：套件曾经"编译不过但没人知道"

2026-10-16 修了 3 处桩漂移，套件才重新跑得起来（之前 `./run.sh` 直接 `exit 1`，
当时 README 顶上的 PASS 数是旧值，没跟上）：

| 漂移 | 症状 | 修法 |
|---|---|---|
| `stubs/ext/log_engine.h` 的 `log_sink(const char*, A&&...)` | `LOG_ERROR(msg)`（整条消息已经是 `std::string`，见孤儿单告警）没有匹配的重载 | 第一个参数也改成万能引用 |
| 缺 `stubs/basic/LarkRebot.h` | 真实 `BaseAlgoOrder.h:12` 会 include 它，桩顶掉之后 `LarkRebot` 成了未声明标识符 | 补一个接口与真实头一致的桩 |
| `stubs/basic/BaseAlgoOrder.h` 漏了 `tradingTypeOrder` | 真实头 `:25` 有它，`BuildAlgoOrderJson` 会写它 | 按真实头的位置补上（`algoType` 之后、`algoStrategyName` 之前） |

教训：桩顶掉真实头时，**真实头的 include 清单和字段集都得跟着抄**，
否则被测 `.cpp` 里那些"靠传递包含拿到"的符号会静默变成编译错误。

### 用例也会过期：F5 碎单阈值

`F5` 原来断言 `MakeHoldable(pi, -0.1)`（= 10 USDT）触发碎单强平，注释写的是
"< 25 USDT"。但 `RiskManager.h:48` 的默认值和 `etc/config.json` 都已经是
**`tinyCloseThresholdUsdt = 5`** —— 10 USDT 早就不算"碎单"了，所以这条一直是红的。
已改成 `-0.04`（= 4 USDT < 5）。`docs/实盘接入与参数配置.md:408` 里那张表还写着 25，也是旧的。

## 相关套件

| 套件 | 覆盖 |
|---|---|
| `../config_keys/` | **参数配置化**：`etc/config.json` 四个 `op.*` 段的 key 名与字段级静态检查（**不编译**，见该目录 README） |
| `../algo_exec/` | 算法单执行端（拆单 / 报单 / 成交 / 撤单 / 风控平仓 / 两条全链路） |
| `../param_calc/` | 开仓阈值反解（把"价差 0.0002 能不能开仓"算成数字） |

本套件的 **A12b** 是配置化的端到端验证：`PairTradingConfig` → `BaseAlgoOrder`，
含 `*CancelOrderTimeMs -> *CancelOrderTime` 的 ×1000 换算（解析阶段不换算，
`×1000` 只发生在 `BuildAlgoOrderJson` 一处）。
