# AlgoType_Rebalance —— 敞口再平衡算法单

> 2026-11-01 初版；**2026-11-05 修订**（四处按用户意见改掉）：
> ① 判断"平多少"改用**实盘持仓**（`activeRealPosition` / `passiveRealPosition`），
> 不再用账本 `pairTotalVolume`；
> ② 撤单改成**定时撤单**（按单子年龄），删掉"每 tick 刷新 `satisfyTime` 续命"那套；
> ③ `role` 改成 **`CLOSE_SIGNAL`**（原先为兼容老判据写成 `OPEN_SIGNAL`），
> 与 §5 的残量判据配套 —— 见 §5 末段；
> ④ **不再新增 `rebalanceStartTime`**：`satisfyTime` 在 `stopFlag` 下刷不动，
> 它本身就是建单时刻 —— 见 §7.1。
>
> 本文只讲**新增的自动触发路径**。`AlgoRebalanceOrder` 这个类本身
> （单腿执行器 + 量阶梯）早就存在，只是从来没有创建路径 —— 全仓 `new AlgoRebalanceOrder()`
> 除本文这条外为 0。
>
> 配套代码：`PairTradingContext::ProcessExposureRebalance` /
> `BuildExposureRebalanceOrder` / `CalcExposureExcess` / `CheckAlgoOrderTimeout`，
> `AlgoContext::OnTimer` 的结束判据，`AlgoRebalanceOrder::GetTargetPairOrder` 的两处修正。
> 测试：`tests/algo_exec/run.sh` 的 **J 组（J1–J13）**。

---

## 1. 补的是哪个洞

`CheckExposureAbnormal`（`PairTradingContext.cpp`）原本是这样收尾的：

```cpp
pi.stopFlag = true;                       // 这个对子停手
if (pi.hasActiveAlgoOrder) RequestCancelAlgoOrder(pi);
LOG_ERROR("... -> stopFlag + cancel");
```

判据是净敞口 > `exposureCancelTimes * ttTargetVolume`（默认 4 倍目标量）。
也就是说：**对冲已经破了 4 倍，系统能做的只有"停手 + 喊人"**，
仓位一动不动地挂在那里，等运维看到飞书消息再手工处理。

`AlgoType_Rebalance` 正好是补这个洞的形状：它是一个**单腿执行器**，
`GetTargetPairOrder` 的阶梯终点（`*CLEndVolume` / `*CSEndVolume`）就是"平到多少为止"。
把终点写成阈值而不是 0，就是"只平超出阈值的部分"。

两个设计决定（用户 2026-11-01 确认）：

| 问题 | 选择 |
|---|---|
| 这张单什么时候被创建 | **风控敞口异常时自动建** |
| 把仓位平到什么程度 | **只平超出阈值的部分**（残量 = 阈值，不是 0） |

---

## 2. 一张图看完

```
OnTimer 第 4 步   CheckExposureAbnormal
                  netExposure > 4 * ttTargetVolume      ← 实盘持仓算
                    ├─ pi.stopFlag  = true      （对子停手，开仓/平仓都不发）
                    ├─ pi.reBalance = true      （待执行再平衡）
                    └─ 撤掉在跑的算法单（异步）

OnTimer 第 3 步   ProcessRisk（下一轮，约 1 秒后）
                  CheckRisk.needForceClose == false ?
                    └─ ProcessExposureRebalance
                         敞口还在阈值外？对子空出来了？
                           ├─ 挑腿：active 等效敞口大的那条（实盘口径）
                           ├─ 平多少 = excess（实盘口径，被动腿 × am/pm）
                           ├─ 阶梯终点 = 账本当前量 ∓ 平多少（写回账本坐标系）
                           └─ 建 AlgoType_Rebalance 单（TT / 只开一个平仓开关）

OnSpread（每 tick）AlgoContext 的 Rebalance 分支
                  activeTrade == 1 -> CreateActiveOrder
                  activeTrade == 0 -> CreateVolumePassiveOrder
                  量 = min(|账本持仓 - 阶梯终点|, ttTargetVolume)

OnTimer          |账本持仓| - |阶梯终点| <= 最小报单量 且无在途 pairOrder
                   -> ALGO_OS_FILLED

OnTimer 第 4 步   CheckAlgoOrderTimeout（定时撤单，与普通单同一套年龄判据）
                  now - satisfyTime > algoOrderTimeoutMs(150s)
                    （satisfyTime 在 stopFlag 下刷不动 ⇒ 它就是建单时刻）
                   -> CANCEL（撤完释放对子，下一轮按**最新实盘持仓**重算再建）
```

**为什么建单在 `ProcessRisk` 而不在 `CheckExposureAbnormal` 里**：
顺序即优先级。强平（ADL / 价差不回归 / 资金费异常）是把两条腿都平干净，
覆盖面比"只平超出阈值的部分"更大；两者同时成立时必须让强平先走。
放在 `ProcessRisk` 的 `!needForceClose` 分支里，强平需求成立时**根本走不到**这一行，
不需要额外的互斥标记。

**为什么不在第 4 步直接建**：判死那一刻对子通常还被上一张算法单占着
（撤单是异步两段式的）。占着就不能建 —— `SetActiveAlgoOrder` 会被覆盖，
而且新单的 `algoOrderId` 和 `pi.currentAlgoOrderId` 对不上，
`OnAlgoOrderUpdate` 会把回传全部丢掉。

---

## 3. 挑哪条腿、"平多少"、以及两个坐标系

设 `ra` = `activeRealPosition`、`rp` = `passiveRealPosition`（**实盘**），
`a` = `pairTotalVolume`、`p` = `pairPassiveTotalVolume`（**账本**），
`am` / `pm` = 两腿 `multiple`。沿用 `PairInfo` 的符号约定：**多负空正**。

### 3.1 敞口与"平多少"：只用实盘持仓

```
netExposure = |ra*am + rp*pm| / am          （active 等效单位）
threshold   = exposureCancelTimes * ttTargetVolume
excess      = netExposure - threshold        ← 这就是"平多少"，active 单位
```

🔴 **必须用实盘**。敞口是交易所上的事实；账本 `pairTotalVolume` 只是我们自己的记账，
重启未对账、漏成交、推送延迟都会让它偏离实盘。用账本算"该平多少"，算出来的就是
"我们以为的敞口"，而风控判死恰恰说明"我们以为的"和事实对不上。

- **挑腿**：active 等价敞口 `aEq = |ra|*am`、`pEq = |rp|*pm`，取大的那条。
  理由不是"它更危险"，而是**只有它单独就够把净敞口压回阈值** ——
  挑小的那条会平不干净（那条腿全平了敞口还超）。两腿都不够时（例如两腿同向、
  根本不是对冲）就把那条腿平干净，打 `LOG_ERROR` 告诉运维一张单解决不了。
- **换成被操作那条腿的单位**：主动腿 = `excess`；被动腿 = `excess * am / pm`
  （被动腿动 1 个单位，净敞口只变 `pm/am`）。

### 3.2 阶梯终点：必须落在**账本坐标系**

`AlgoRebalanceOrder::GetTargetPairOrder` 读的是

```
expectVolume = GetExpectActiveVolume() = pairTotalVolume + 在途已成交 + 冻结
报单量       = min(|expectVolume - endVolume|, ttTargetVolume)
```

所以终点只能写成"**账本当前量 ∓ 平多少**"：

| 腿 | 平多少（该腿单位） | 阶梯终点 `endVolume` |
|---|---|---|
| 主动腿 | `excess` | `a - sign(a) * excess` |
| 被动腿 | `excess * am / pm` | `p - sign(p) * excess*am/pm` |

🔴 **不能把终点写成实盘残量**（`ra ∓ excess`）。那是混用两个坐标系：
账本与实盘差 δ 时，`|expect - end|` 会变成 `excess ± δ` —— 多平或少平 δ。
而"平多少"用实盘算、终点落在账本坐标系，则差值恰好等于 `excess`：
**实盘和账本被同一批成交推动，差值才是真实发生的事，绝对水位只是记账。**

**必须是带符号的**。`GetTargetPairOrder` 里 `CLOSE_LONG` 要求
`endVolume > expectVolume`、`CLOSE_SHORT` 要求 `endVolume < expectVolume`；
写绝对值会让"平多"那一支永远不成立（一笔都报不出去）。

### 3.3 三个守卫（都是"算不出来就别猜"）

| 守卫 | 判据 | 处理 |
|---|---|---|
| 账本与实盘同号 | `legLedger * legReal > 0` | 不同号 ⇒ 账本已不可信（漏成交 / 重启未对账）。按开关硬报只会得到空单（安全侧），但会一直空转 ⇒ 清标记、`LOG_ERROR`、等人工对账 |
| 待平量够一手 | `excess >= pi.minVolume` | 不够 ⇒ 清标记、`LOG_ERROR`。不退出的话会陷入"建单 → 报不出去 → 年龄超时撤 → 再建"，每 150s 刷一轮 |
| 该腿够平 | `closeAmount <= \|legReal\|` | 超过 ⇒ 全平该腿 + `LOG_ERROR`（两腿同向，一张单解决不了） |

---

## 4. 建单时每一处都是刻意的

| 字段 | 值 | 为什么 |
|---|---|---|
| `activeTrade` | 1 或 0 | 见 §3 挑腿。它同时决定阶梯读 `GetExpectActiveVolume` 还是 `GetExpectPassiveVolume`、报 `CreateActiveOrder` 还是 `CreateVolumePassiveOrder`、结束判据读哪个量 |
| `tradingTypeOrder` | `TAKER_TAKER` | 对冲已经破了，要的是"确定地平掉"而不是"便宜地平掉"。风控强平单用 MT 是因为它要**同时**平两条腿、主动腿挂 maker 能压价差；这里只动一条腿，速度优先。改成 maker 只需把这一行换成 `MAKER_TAKER` 并打开 `mtRebalanceSwitch` / `mtXXSwitch` |
| `*CLEndVolume` / `*CSEndVolume` | 阶梯终点（带符号，账本坐标系） | 阶梯终点。**不是** `op.*EndVolume`（配置里通常是 0 = 平干净），也不是实盘残量（见 §3.2） |
| 八个开关 | 只开**一个**平仓开关 | 方向由**账本符号**决定（`CreatePairOrder` 按 `expectVolume` 的符号选平多/平空），不是由终点的符号决定。另一个方向的开关关着 ⇒ 万一账本符号和这里算的不一致，`CreatePairOrder` 会走另一支并因为开关关着返回空单，**不会反手把敞口做大**（这是本功能最坏的失败模式） |
| 四个开仓开关 | 全 `false` | 单腿单不许开仓，理由同风控强平单 |
| `role` | `CLOSE_SIGNAL` | 这张单确实"只平不开"，语义就是平仓单。**能这么写的前提是 §5 那条残量判据**：老判据 `\|持仓\| < minSize` 下它停在残量上永远不结束，所以早期版本只能退而求其次写 `OPEN_SIGNAL`。现在判据是 `\|持仓\| - \|终点\| < minSize`，"刚建单"（20-15=5 > 0.1）和"到位"分得开 ⇒ 按真实语义写 `CLOSE_SIGNAL`。它比 `OPEN_SIGNAL` 更稳的一点：万一有人误开了某个开仓开关，`noOpenPermission` 变 false，但 `role != OPEN_SIGNAL` 仍然进得了终结判据。不用 `RISK_CLOSE`：那会把 `pi.activeAlgoIsRiskClose` 置真（"这是强平单"的派生镜像），而本单只是削超额，不是强平 |
| `ttRebalanceSwitch` / `ttRebalanceFlag` | `true` | `OnSpread` 的 Rebalance 分支要求 `*RebalanceFlag` 为真，否则一条子单都不报 |
| ~~`isExposureRebalance`~~ | **未实现** | 原计划用它区分"风控自动单"与"人工建的 Rebalance 单"（滑点熔断豁免 + 终结日志措辞）。`quant_library/basic/AlgoRebalanceOrder.h` 里**没有这个字段**，`git log -S` 在 quant_library 下也无任何记录 ⇒ 现在靠 `PairInfo::reBalance` 识别来源；滑点熔断**没有**豁免，见 §6.3 |
| `targetResidualVolume` | 阶梯终点 | 结束判据用它（`\|持仓\| - \|终点\| <= minSize`） |
| `pairTotalVolume` 等账本 | 照抄 `pi` | `OnAlgoOrderUpdate` 会把算法单上的量整份写回 `PairInfo`；换个源（比如用实盘持仓当种子）会让两边从此对不上，还会把在途成交"退回去" |

**被动腿的平多/平空是反的**：主动腿空 ⇔ 被动腿多。所以
`activeTrade == 0` 时 `useCloseShort = !legShort`（见 `BuildExposureRebalanceOrder`）。

---

## 5. 结束判据：为什么必须换

原判据（`AlgoContext.cpp`）：

```cpp
orderAmount = |pairTotalVolume|（或 |pairPassiveTotalVolume|）;
if (orderAmount < minSize && allPairOrders.size() == 0) -> ALGO_OS_FILLED
```

它回答的是"**这一单是不是什么都还没做**"（平到 0 的单，平完了量就是 0）。
敞口再平衡单的终点不是 0 —— 平完之后剩余量恰好等于残量，这条判据**永远不成立**，
于是这张单会一直活着、一直占着对子，而 `GetTargetPairOrder` 每个 tick 都返回空单
（阶梯已经到头）。表现出来就是"对子被一张什么也不做的单锁死"。

改成（`AlgoContext.cpp` 的 `AlgoType_Rebalance` 那一支）：

```cpp
// 判据本身对所有 Rebalance 单统一，靠 targetResidualVolume 的默认值兼容老路径
if ((|持仓| - |targetResidualVolume|) < minSize && allPairOrders.size() == 0)
    -> ALGO_OS_FILLED
```

- 风控单：`targetResidualVolume` = 阶梯终点 ⇒ 到位才结束。
- 人工建的 Rebalance 单不动这个字段 ⇒ 默认 `0` ⇒ 退化成原来的 `|持仓| < minSize`，
  行为一个字节没变。
- 允许差值为负（打过头了也算到位），所以是减法比较而不是绝对值相等。

⚠️ 这里**不能用 `isExposureRebalance` 当判据**：那个字段只管"滑点熔断豁免 + 日志措辞"
（而且它本身也未实现，见 §4）。
判据要覆盖所有 Rebalance 单，否则人工单会一直活到 150s 空闲超时。

**这条判据与 `role = CLOSE_SIGNAL` 是一对，必须同时存在**：
`role != OPEN_SIGNAL` 只是让 OnTimer **进入**终结判据，判据本身仍是残量口径，
所以"刚建单"（20-15=5 > minSize）不会被误判成 FILLED，而"到位"（15-15=0）才算完。
两者任一缺失都会坏：只剩角色 ⇒ 停在残量上永远不结束；只剩判据 ⇒ 靠
`noOpenPermission` 这个"四开关全 false"的代理进分支，一旦有人误开一个开仓开关就再也进不去。

---

## 6. 顺带修掉的三个坑

这三处都在 `AlgoRebalanceOrder` 里，之前没有创建路径所以从来没被跑到过；
本功能第一次真的用这条腿，它们都会直接让功能失效。

### 6.1 `CLOSE_SHORT` 少了 `fabs`（被动腿一支永远报不出单）

```cpp
// 原来
targetVolume = min(expectActiveVolume - targetActiveVolume, ttTargetVolume);
// 现在
targetVolume = min(fabs(expectActiveVolume - targetActiveVolume), ttTargetVolume);
```

`activeTrade == 0` 时 expect/target 是**被动腿**的量，符号与主动腿相反，
直接相减得到负数 ⇒ 下面 `targetVolume < MIN_FLOAT` 把它当"没有量"丢掉。
兄弟分支 `CLOSE_LONG` 本来就带 `fabs`，这一支是漏的。
`activeTrade == 1` 时两个数同号，`fabs` 是空操作，行为完全不变。

### 6.2 报单量不取整（交易所直接拒单）

原来只有 `activeTrade == 0` 那一支做了 `round(v / activeInfo.lotSize) * lotSize`，
而且用的是**主动腿**的步长；`activeTrade == 1` 那一支完全不取整。
敞口再平衡单的量 = "当前持仓 - 目标残量"，两个数都不是 `lotSize` 的整数倍
⇒ 不取整必然被拒。现在两条腿都按**被操作那条腿**的步长取整。

### 6.3 滑点熔断会把风控单挂死

置位点在 **`BaseAlgoOrder::PairOrderTrade`**，不在 `AlgoRebalanceOrder` 里
（`BaseAlgoOrder.cpp:784` 置 `mtSlipageFlag`、`:797` 置 `ttSlipageFlag`），
而且**整段被 `if (pairOrder.rebalanceFlag && pairOrder.passiveTotalVolumeOnOrder > 0)`
包着**（`:776`）——也就是说这个 flag **只可能由"再平衡中的对子单"抬高**，
判据是 `ttSlipage > 2 * passiveTakerSlippage + profitPct`（`:796`）。

抬高之后，`OnSpread` 见到它就**停止为该单报子单**，三个分支各有一道同款闸门：
`AlgoContext.cpp:571`（`AlgoType_PairTrading`）/ `:790`（`AlgoType_FishingTrading`）/
`:966`（`AlgoType_Rebalance`），条件都是
`!ttSlipageFlag && !ttSpreadFlag && tradingTypeOrder == TAKER_TAKER`。
清除只发生在 `OnTimer`（`AlgoContext.cpp:1477-1480`，`slippageCount >= 60*5` 个 1 Hz 计数）
⇒ **最长挂 5 分钟**。

> 🔴 **原计划用 `!isExposureRebalance` 跳过这段，但那个字段从未实现**（见 §4）
> ⇒ **现状是没有豁免**。
>
> ⚠️ 两点容易搞反：
> 1. 这个 flag 对 `AlgoType_Rebalance` 是**自伤**——只有再平衡对子单会抬高它，
>    抬高后挡住的又正是自己。对 `AlgoType_PairTrading` 的再平衡阶段则是正常保护
>    （滑点打穿 `2*passiveTakerSlippage + profitPct` 就该停手，别继续追）。
> 2. 因此"豁免"不是显然正确的：**要不要让风控单也停手 5 分钟**是策略取舍，
>    不是 bug。想豁免的话最省的办法是在上面三处判据里按
>    `algoType == AlgoType_Rebalance` 放行，不需要新增字段；但那样就把这条
>    保护对风控单彻底关掉了。
>
> ✅ **决定：保持现状，不改代码**（2026-11-10）。理由是判据本身已经足够保守
> —— 要打穿 `2*passiveTakerSlippage + profitPct` 才停手，对一张正在追价的单
> 停 5 分钟是安全阀而非故障；豁免等于把这条保护对风控单永久关掉。
> 本节只作为"已知行为"记录，**不要再当缺口重提**。

---

## 7. 与既有机制的交互

| 交互点 | 行为 | 为什么 |
|---|---|---|
| `CheckAlgoOrderTimeout` | **定时撤单**：`now - satisfyTime > algoOrderTimeoutMs`（默认 150s）就撤，与普通单同一套判据 | 见 §7.1 |
| `ProcessModify` / `SubmitAlgoOrder(MODIFY)` | **忽略** | `BuildNormalModify` 整份照搬 `pi.orderParams`，会把终点覆盖成配置里的 0（"只平超出部分"变成"平干净"），还会把四个开仓开关打开（单腿单就能反手加仓） |
| `ProcessRisk` 强平 | 强平优先，本单被撤 | 见 §2 |
| `stopAllAlgo` | 不建、在跑的被撤 | 与"不再创建新的算法单"一致 |
| `pairCommands` `RESUME` | 清 `pi.reBalance` | 人工恢复 = 撤掉这条自动补救路径，否则下一轮又按旧敞口建一张，把恢复操作顶回去 |
| `stopFlag` 的来源 | 只有"敞口异常"会触发再平衡 | `pairCommands STOP` / 启动孤儿冻结也置 `stopFlag`，但那是人或启动流程明确要求停手，不能自动动手 |
| 启动 / 重启 | **不自动再平衡** | `reBalance` 不进快照；重启后 `hasActiveAlgoOrder` 的孤儿冻结会置 `stopFlag`，`CheckExposureAbnormal` 因 `stopFlag` 早退 ⇒ 不会在未对账的账本上自动动手。要自动收敛请先人工 `PairCmd_RESUME` |
| 快照 / DB schema | **不动** | `targetResidualVolume` 是运行态字段（`isExposureRebalance` 未实现，见 §4）；`WriteAlgoOrder` 的 57 列超集里只有 `activeTrade` 能区分这类单，终点看创建时的 `LOG_WARN` |

### 7.1 定时撤单：为什么**不需要**单独记一个时间戳

再平衡单**没有"机会"这个概念** —— 它不对应任何开平仓信号。而 `satisfyTime` 的语义是
"机会最后一次成立的时间"。所以第一反应是"得另记一个 `rebalanceStartTime`"。
**不必要**，因为下面这条链是闭合的：

```
reBalance == true  ⟹  stopFlag == true
     （CheckExposureAbnormal 同时置两个；清 stopFlag 的只有 RESUME，它也一起清 reBalance）
stopFlag == true   ⟹  SignalGenerator::CanOpen() == false && CanClose() == false
     （SignalGenerator.cpp 里两个函数都判 stopFlag）
                    ⟹  UpdateSatisfyTime 的 openSatisfied / closeSatisfied 都不成立
                       且 manualReady 要求 !stopFlag ⇒ 也不成立
                    ⟹  satisfyTime **一次都刷不到**
```

`satisfyTime` 的写入点全仓只有两个：`UpdateSatisfyTime`（上面已证明刷不到）和
`SubmitBuiltAlgoOrder`（建单时写一次）。所以它**冻在建单时刻**，
`now - satisfyTime` 就是这张再平衡单挂着的时长 —— 这正是"单子年龄"，不用另存一份。

于是 `CheckAlgoOrderTimeout` 只有一处需要区别对待：**豁免 `!autoFlag` 那道门槛**
（普通单"手动不撤"，但手动对子同样会被判死、同样建再平衡单，套上门槛就永远撤不掉、
对子卡死）。判据本身、超时值、撤单动作三者与普通单完全共用。

⚠️ 这也意味着 **`ProcessExposureRebalance` 绝不能刷 `satisfyTime`**（早先那版就是每 tick
刷它"续命"）—— 一刷，撤单就永不发生，单子永久占住对子。`UpdateSatisfyTime` 里那条
`manualReady` 一旦哪天把 `stopFlag` 放开口子，同样会破坏这条链：`stopFlag` 是这里的
**承重结构**，不是顺手加的判断。

撤掉之后 `OnAlgoFinished` 释放对子，`ProcessExposureRebalance` 用**最新的实盘持仓**
重算一次再建 —— 这个"撤-建"循环正是目标量能跟着成交往前走的原因：
一张单的阶梯终点是**建单那一刻**算死的，成交推进后必须重算才准。

`algoOrderTimeoutMs` 沿用配置里那一个（默认 150s），不新增配置项、不新增字段。
子单层面另有 `activeTakerCancelOrderTime`（默认 5s）在不断撤挂，所以 150s 内
价格并不是死的；150s 到点撤的是**算法单**，语义是"这一轮重算"。

---

## 8. 它**不**做什么（已知边界）

1. **不会解开 `stopFlag`**。压回阈值之后对子仍然是"判死、等人工 RESUME"的状态。
   自动解开会与 `pairCommands` 的单向语义打架（见 `RuntimeConfig.h` 的注释）。
2. **只动一条腿**。如果两腿同向（根本不是对冲），一张单压不回阈值，
   会先把大那条腿平干净并打 `LOG_ERROR`，剩下的等人工。
3. **不平到 0**。这是刻意的（用户的选择）。要平干净请用 `pairCommands` 的
   `closeRemainVolume = 0` 或风控强平。
4. **不解决"CANCELLING 下被动腿无上界"**（`docs/执行层缺口清单.md` §1.3）。
   那是"敞口可能一点都没超、但一条腿卡在撤单里"的场景，与本功能的判据无关。
5. **不做成本优化**。吃单、不择价。超出量有上界（`netExposure - threshold`），
   但极端行情下这笔成本是实打实的。
6. **待平量不足一手时不动手**（`excess < pi.minVolume`）。直接打 `LOG_ERROR` 并清标记 ——
   报出去也会被交易所拒，而且会变成"建单 → 不报 → 超时撤 → 再建"的 150s 死循环。
   这种情况说明 `maxAmount` / `targetAmount` 配得比最小报单量还小，该改配置而不是改代码。
   已知残余边界：若 `excess >= minVolume` 但取整到 `lotSize` 后为 0，仍会各 150s 试一次
   （每次都有 `LOG_INFO`/`LOG_ERROR`），不会无限快循环。
7. **不校验账本与实盘的"量级差"**，只校验同号。若 `|账本|` 远小于 `|实盘|`（同号），
   终结判据 `|账本| - |终点|` 可能在建单后第一个 `OnTimer` 就成立 ⇒ 单子提前判到位。
   这种状态只可能来自"算法单回报写回丢失"，而那种情况下启动孤儿冻结会先置 `stopFlag`、
   `reBalance` 又不进快照 ⇒ 自动路径看不到它。真要兜住得在 `ProcessExposureRebalance`
   加一条"量级差 > 待平量就拒绝"的守卫，目前刻意没加（没有可靠的阈值，加了反而会误伤）。

---

## 9. 怎么验

```bash
cd utrade_hft/tests/algo_exec && ./run.sh      # 期望 PASS: 271  FAIL: 0
cd utrade_hft/tests/market_to_algo && ./run.sh # 期望 PASS: 550  FAIL: 16（L 组，与本次无关的既有失败）
```

J 组 13 个用例：

| 用例 | 钉的是什么 |
|---|---|
| J1 | 判据：净敞口 15 > 阈值 10 → `stopFlag` + `reBalance`，且判据本身不建单 |
| J2 | 建单：`algoType` / `role` / `activeTrade` / 终点 15 / 只开 `ttCS` / 四个开仓开关全关 / TT |
| J3 | 报单：单腿、量 = `min(20-15, ttTargetVolume 2.5)` = 2.5、方向 SHORT、打 bid |
| J4 | `MODIFY` 被忽略：终点仍是 15，开仓开关没被打开 |
| J5 | 结束判据：剩余 20 不终结；剩余 15（== 终点）→ FILLED |
| J6 | 被动腿更重：`activeTrade = 0`、终点 -15、走 `CLOSE_SHORT`、买回平多 |
| J7 | 敞口回到阈值内 → 清标记、不建单 |
| J8 | 对子被占着 → 不建第二张，且**不刷新** `satisfyTime`（不续命） |
| J9 | 定时撤单：先钉前提（`stopFlag` 下 `CanOpen`/`CanClose` 都是 false），再钉"挂 10s 不撤 / 挂 151s 撤" |
| J9b | `autoFlag = false` 的再平衡单同样被定时撤掉（普通单的门槛不套到风控单上） |
| J10 | **平多少用实盘**：实盘 18 / 账本 20 ⇒ 终点 17（写成 15 就少平 2 张） |
| J11 | **被动腿换算**：`am=2, pm=1` ⇒ 平 10 张被动腿、终点 -30（漏乘 `am/pm` 只平 5 张） |
| J12 | 账本与实盘不同号 → 不建单 + 清标记（不猜方向） |
| J13 | 待平 5 < `minVolume` 10 → 不建单 + 清标记（不死循环） |

> J6 是 §6.1 的回归：`CLOSE_SHORT` 少了 `fabs` 时这条断言是 `NNew() == 0`。
> J9 是 §7.1 的回归：改回"续命"这一条立刻失败。
> J10 / J11 是 §3 的回归：混用坐标系或漏乘 `am/pm`，这两条立刻失败。

---

## 10. 运维要点

- 日志关键字：`CheckExposureAbnormal`（判死）、`ProcessExposureRebalance`（建单 / 放弃 / 挑腿）、
  `create exposure rebalance algo order`（建单参数）、
  `再平衡单已挂 ... -> cancel`（定时撤单）、`算法单终结(敞口再平衡到位)`（收工）。
- 一轮完整的收敛长这样（都是正常现象）：
  `CheckExposureAbnormal` → `create exposure rebalance` → 150s 后 `再平衡单已挂 151s -> cancel`
  → 再 `create exposure rebalance`（终点按新持仓重算）→ … → `敞口已回到阈值内`。
- 飞书消息里的 `algoStrategyName` 是 **`exposure_rebalance`**（区分于普通单的 `pair_trading`）。
- 建单失败 / 不足一手 / 单腿不够 / 账本与实盘不同号，都会打 `LOG_ERROR` 且**不会自动重试**
  （除了"建单失败"下一轮重试）—— 看到这几条说明需要人工介入。
- 想关掉这个功能：把 `exposureCancelTimes` 调到极大（判据永不成立），
  或直接回退 `ProcessRisk` 里那一行调用。**不要**靠关 `ttRebalanceSwitch` 之类的东西，
  那是给单张算法单用的。
