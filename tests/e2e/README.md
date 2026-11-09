# tests/e2e —— 一个功能一个脚本

给 `utrade_hft` 的**功能级**回归用。和另外两个套件的分工：

| 套件 | 测什么 | 形态 |
|---|---|---|
| `market_to_algo` | 策略侧（行情 -> 信号 -> 建算法单） | 一个大 `suite.cpp`，550 条断言 |
| `algo_exec` | 执行侧（算法单 -> 子单 -> 回报 -> 状态机） | 一个大 `suite.cpp`，212 条断言 |
| **`e2e`（本目录）** | **一条完整功能链路，从配置/信号一直到落盘** | **一个功能一个 `.cpp`，每个都能单独读** |

本目录刻意不复用那两个 `suite.cpp`：它们是"按函数分节"的白盒测试，读起来要跳来跳去。
这里每个文件从上往下读就是一条业务链路，改起来只改那一个文件。

---

## 怎么跑

```bash
cd tests/e2e
./run.sh              # 全跑，最后一行是总计
./run.sh 03           # 只跑 03（前缀匹配，写 03 或 03_close 都行）
./run.sh 01 03 07     # 跑几个
./run.sh --list       # 列出所有用例
./run.sh -f           # 先强制重编真实源码（改了 quant_library 之后必须用）
```

`-f` 必须放在**最前面**。

编译代价只在 `libreal.a` 上付一次（`build.sh` 把 `quant_library` 里的真实源码编成静态库），
之后每个用例链接只要不到一秒。**用例本身没有任何 mock**：调的都是生产代码，
只有 `LarkRebot` / `dbp` / `TradeClient` 这些外部边界用桩（在
`../algo_exec/stubs/` 和 `../market_to_algo/stubs/`，`build.sh` 会拷过来）。

---

## 文件结构

```
harness.h        所有用例共用的东西（只有这一个共享文件）
build.sh         编译真实源码 -> $E2E_BUILD/obj/libreal.a
run.sh           发现 / 编译 / 逐个跑，每个用例一个干净的临时目录
NN_xxx.cpp       一个功能一个文件，每个都有独立的 int main()
```

`harness.h` 提供：

| 名字 | 用途 |
|---|---|
| `CHECK(cond, "说明")` / `Near(a,b)` | 断言。说明要用**中文讲清"为什么这条重要"**，不要写 `CHECK(x==1, "x")` |
| `RunCase(name, fn)` | 跑一个用例，打印 `--- <name>: PASS n FAIL m`（`run.sh` 靠这行统计） |
| `Fx` | 夹具：`PairTradingContext` + `AlgoContext` + 记录用的 `TradeClient`，还有 `News()` / `Cancels()` / `ChildOrders(o)` / `PairOrders(o)` / `pi()` |
| `Boot(fx, keepState=false)` | 初始化。`keepState=true` 表示**不删落盘文件** = 模拟进程重启 |
| `GoTrading(fx)` | 跳过启动对账，直接进交易态（要测对账本身就别调它） |
| `SeedFunds(fx)` | 给执行侧注入资金（走真实 `OnBalance`） |
| `MakeHoldable(pi, vol)` | 把报单量参数摆到"能报单"的状态（`targetVolume` / `maxVolume` / 腿价 / 开关） |
| `ArmOpen(pi, which, vol)` / `ArmClose(...)` | 摆一个"信号一定成立"的局面，`which` 取 `ttOL/ttOS/ttCL/ttCS/mtOL/...` |
| `OnlySwitch(pi, which)` | 八个开关里只留一个 |
| `MakeRtn(...)` / `MakeRef(o, pairId)` | 造交易所回报 |
| `ApplyRuntimeConfig(fx, json)` | 写 `config.json` 并走完"T_signal 解析 -> m_cfgQ -> OnTimer 落地" |
| `PairCmdJson(type, extra)` / `GlobalSwitchJson(name, on)` | 拼上面那个 json |

---

## 怎么加一个用例

1. `cp 03_close.cpp 14_xxx.cpp`
2. 改 `int main()` 里的名字：`RunCase("14 xxx", Case)`
3. 从上往下改 `Case()` 里的场景和断言 —— 链路照抄，只改"摆什么局面 / 期望什么"
4. `./run.sh 14`

不用注册、不用改 `run.sh`：它按文件名 `[0-9][0-9]_*.cpp` 自动发现，文件名前缀就是执行顺序。

**写断言的两条规矩**（都是从踩过的坑里来的）：

- **行为断言之前先断言前提**。例如要测"stopFlag 下不建单"，必须先
  `CHECK(sg.CheckSignalForSatisfy(pi).ttOLSignal, "前提：信号确实成立")`。
  否则信号本来就没成立，"没建单"和"被拦住"看起来一模一样 —— 假阴性。
- **改字段的断言和读字段的断言分开写**。`CHECK(pi.stopFlag, ...)` 测的是"命令落地了"，
  再单独 `CHECK(!sg.CanOpen(...))` 测"闸门真的挡住了"。只测前者的话，闸门写错也照样绿。

---

## 六个坑（每个都真实踩过）

1. **价差槽位要自己补**。生产在 `PairTradingStrategy::pre_start` 里做
   `SpreadManager::AddSpreadPara` + `dbpReader.Subscribe`，测试没有 `pre_start`，
   `Boot()` 替你做。漏了的话 `GetSpread` 返回 nullptr，症状是**全部用例静默不报单**。

2. **子单初始状态是 `OS_PEND`，不是 `OS_NEW`**。所以撤单用例要先喂一条 `OS_NEW` 确认，
   否则撤单流程根本不启动。

3. **`pairTotalVolume` 符号是反的**：负数 = 多（`IsLong()` 是 `vol < -1e-9`）。
   "平多"要喂 `-5.0`。写同号的话平仓判据永远不成立。

4. **`OnTimer` 会用 cfg 重算报单量**。第 1 步 `RecalcVolumeParams` 按 `cfg.maxAmount` /
   `targetAmount` 重算 `maxVolume` / `ttTargetVolume`，把 `MakeHoldable` 手摆的 10 / 10
   覆盖成 5 / 0.5（`maxAmount` 默认 50、腿价 100 时）。凡是调过 `ApplyRuntimeConfig`
   （内含 `OnTimer`）的用例，都要再 `MakeHoldable` 一次，否则依赖报单量和阶梯末端的
   断言测的是另一组数。

5. **子单的 `offsetFlag` 恒为 `OF_OPEN`**（`PairManager.cpp` 里是硬编码的赋值）。
   开/平的意图只体现在 `pairOrder.tradingTypeOffset`（`CLOSE_LONG` 等）和方向上，
   不要断言 `offsetFlag == OF_CLOSE`。

6. **写完 `config.json` 必须作废指纹，不能指望 mtime 变**。`RunSignalOnce` 只在
   `(ino, mtimeNs, size)` 变了才重读文件。**Linux 的文件时间戳取自粗粒度时钟**
   （1 jiffy ≈ 1~4 ms），同一 tick 内连写两次**等长**内容（如 `cmdType:1` → `cmdType:3`，
   都是 122 字节）⇒ mtime 完全相同 ⇒ 第二次配置被**静默跳过**。macOS 的 APFS 是 ns 级，
   本机永远复现不出来 —— 症状是部署机上 `11` 从 10/0 变成 **4/5**。
   `ApplyRuntimeConfig` 里已经 `m_cfgStamp.valid = false`，新用例照抄它就行。

---

## 用例清单

| 文件 | 功能 | 主要入口 |
|---|---|---|
| `01_open.cpp` | 正常开仓：信号 -> 算法单 -> 第一条子单 | `ProcessPairSignal` / `OnSpread` |
| `02_fill.cpp` | 成交回报：主动腿成交 -> 报被动腿 -> 两条腿齐 -> 删 pairOrder | `OnOrder` |
| `03_close.cpp` | 正常平仓：CLOSE_LONG -> 平到 0 -> 算法单终结、对子释放 | `ProcessPairSignal` / `OnOrder` / `OnTimer` |
| `04_cancel.cpp` | 定时撤单：机会连续不成立 -> CANCEL -> 被动腿撤掉 -> 终结 | `OnTimer` / `OnOrder` |
| `05_risk_close.cpp` | 风控强平：碎单命中 -> RISK_CLOSE -> 平干净 | `ProcessRisk` |
| `06_rebalance.cpp` | 敞口再平衡：净敞口超阈值 -> stopFlag + 只平超出部分 | `CheckExposureAbnormal` / `ProcessRisk` |
| `07_snapshot.cpp` | `pair_info.csv` 落库 / 重启读回（32 列，账户真值故意不存） | `SaveSnapshot` / `Init` |
| `08_spread_stats.cpp` | `spread_stats.csv` 落库 / 重启免预热（12 列，含陈旧度判断） | `WriteSpreadStatsFile` / `Init` |
| `09_config_stop.cpp` | 配置停止交易（`cmdType=1`）：开仓平仓都不发 | `ApplyPairCommands` |
| `10_config_close_only.cpp` | 配置只平不开（`cmdType=2`）：开仓压掉、平仓照常、`closeRemainVolume` 决定末端 | `ApplyPairCommands` |
| `11_config_resume.cpp` | 配置恢复（`cmdType=3`）：清 stopFlag / closeFlag / reBalance | `ApplyPairCommands` |
| `12_restart_orphan.cpp` | 重启时场上有未终结的算法单 -> 孤儿冻结 | `Init` / `HandleOrphanAlgoOrders` |
| `13_global_switch.cpp` | 全局 `closeOnly` / `stopAllAlgo`（两者语义不同） | `OnTimer` / `ProcessPairSignal` / `ProcessRisk` |

### 曾经的红：10（已修复，2026-11-09）

`10_config_close_only.cpp` 曾经有 13 条失败，全部来自同一个缺陷：

```
quant_library/algo/PairTradingContext.cpp:1589
    else if (cmd.cmdType != PairCmd_CLOSE) {   // 应为 ==
```

`!=` 写成 `==` 的反面，导致：
- `cmdType=2`（只平不开）落进末尾的 `"not support cmdtype: 2"`，**功能整体不可用**；
- `cmdType=4`（MODIFY，文档写明未实现）反而进这个分支，误置 `closeFlag`。

改成 `==` 之后 10 号 19/19 全绿，其它用例不受影响，断言一条没改。
留这段记录是因为它说明了本目录的断言口径：**按文档语义写，不迁就实现**。

---

## 覆盖率怎么测（想知道"到底测到哪儿了"）

`build.sh` / `run.sh` 都认一个 `E2E_CXXFLAGS` 环境变量，默认空 = 行为不变。
用它注入 clang 的源码级覆盖率，跑完合并 13 个进程的 profile：

```bash
rm -rf /tmp/e2e_cov /tmp/e2e_prof && mkdir -p /tmp/e2e_prof
E2E_BUILD=/tmp/e2e_cov \
E2E_CXXFLAGS="-fprofile-instr-generate -fcoverage-mapping" \
LLVM_PROFILE_FILE='/tmp/e2e_prof/%p.profraw' ./run.sh

llvm-profdata merge -sparse /tmp/e2e_prof/*.profraw -o /tmp/e2e.profdata
llvm-cov report /tmp/e2e_cov/obj/case_13_global_switch -instr-profile=/tmp/e2e.profdata
# 单文件 / 按函数：
llvm-cov report /tmp/e2e_cov/obj/case_13_global_switch /tmp/e2e_cov/algo/AlgoContext.cpp \
  -instr-profile=/tmp/e2e.profdata -show-functions
```

`%p` 是 pid，13 个用例各写各的 `.profraw`，不会互相覆盖。
`llvm-profdata` / `llvm-cov` 在 `/Library/Developer/CommandLineTools/usr/bin/`。

**2026-11-09 实测（13 个用例全跑）：**

| 文件 | 行覆盖 | 分支覆盖 |
|---|---|---|
| `algo/AlgoContext.cpp` | 35.85% | 29.93% |
| └ `OnSpread`（拆单+报单主入口） | **20.91%** | **18.63%** |
| └ `OnOrder`（回报更新） | 55.67% | 58.33% |
| └ `OnTimer` | 37.19% | 41.33% |
| `basic/AlgoPairOrder.cpp`（拆单算法） | 33.64% | 19.37% |
| `basic/BaseAlgoOrder.cpp` | 41.94% | 18.16% |
| `basic/PairManager.cpp` | 22.43% | 6.80% |
| `signal/SignalGenerator.cpp` | 68.67% | 42.71% |
| `algo/PairTradingContext.cpp`（策略侧） | 68.19% | 56.33% |

读法：**策略侧（信号/闸门/落库）测得多，执行侧（拆单/报单/回报）只测了一条最窄的正向路径。**
