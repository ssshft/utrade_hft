# PairInfo 五个 flag 的审计（附"单对子紧急平仓"改动的碰撞检查）

> 2026-10-28 复核。**只读审计，没有改任何代码。**
> 声明位置：`quant_library/basic/PairInfo.h:314-318`。
> 你给的定义：`autoFlag` 可开可平；`stopFlag` 停止开仓；`closeFlag` 只平不开；`errorFlag` 错误不开仓。
>
> ⚠️ 2026-10-21 那一版的行号**全部失效**（中间重构过：`RecalcOrderParams` 从 `PairTradingContext`
> 搬到 `SignalGenerator`，`manualFlag` 字段已删）。本版按 HEAD 逐条重新核过。
>
> ⚠️ **§1–§5 描述的是"现状"（HEAD 的行为）；§6 是"目标模型"** —— 2026-10-29 定：
> 三个 flag 就是一个三态枚举（正常 / 只平不开 / 全停），`autoFlag` 退出权限模型。
> 两者冲突时以 §6 为准，但代码还没改。
>
> ✅ **2026-10-29 已落地**（`00d7b3f` / `d7913b4` / `f8af754`）：`limitFlag` / `errorFlag` 已删，
> 只留 `autoFlag` / `closeFlag` / `stopFlag`。落地复核见 **§7**。

---

## 0. 一张总表：谁写、谁读、进不进快照

| flag | 写入点 | 读取点 | 快照列 | 实际效果 |
|---|---|---|---|---|
| `autoFlag` | `PairInfoManager.cpp:437`（f[26]）、`:1000`（STOP→false）、`:1016`（RESUME→true） | `SignalGenerator.cpp:194-197`、`PairTradingContext.cpp:484/488/528/556/980` | 26 | **三重语义**：开仓总开关 + 强平走 TT/MT + 免超时撤单 |
| `stopFlag` | `PairInfoManager.cpp:438`（f[27]）、`:999`、`:1014` | `SignalGenerator.cpp:249/349`、`PairTradingContext.cpp:528/758` | 27 | 挡开仓；**空仓时把全部信号（含平仓）一起掐掉** |
| `closeFlag` | `PairInfoManager.cpp:439`（f[28]）、`:1003`、`:1015` | `SignalGenerator.cpp:191-192/354`、`PairTradingContext.cpp:758` | 28 | **只平不开**，语义唯一、正确 |
| `limitFlag` | **无** | `SignalGenerator.cpp:359` | 不在快照里 | 死闸门，永远不成立 |
| `errorFlag` | `PairTradingContext.cpp:285`（孤儿单冻结）、`:606`（敞口失控）；`PairInfoManager.cpp:1012`（RESUME 清除） | `SignalGenerator.cpp:364`、`PairTradingContext.cpp:372/575` | 30 | **开仓和平仓一起挡** |

⚠️ 前提：`ApplyCommand`（`PairInfoManager.cpp:991-1039`）全仓**只有 1 个调用点，还是测试**
（`tests/market_to_algo/suite.cpp:2466` 的 G16）。生产零调用。
⇒ 除 `errorFlag` 外，其余四个 flag 的"运行期写入"全部不可达；生产上它们的值只来自
**CSV 快照**（`PairInfoManager.cpp:437-443`），也就是"上次重启前的值"，首启取头文件默认值。

---

## 1. 逐条判

### 1.1 `autoFlag`：三重语义，其中"决定强平方式"是错的

```cpp
// SignalGenerator.cpp:194-197  —— 语义①：开仓总开关
op.ttOLSwitch = canOpenLong && pi.autoFlag;

// PairTradingContext.cpp:484 / 488  —— 语义②：强平走 TT 还是 MT
std::string mode = pi.autoFlag ? "TT" : "MT";
SubmitAlgoOrder(pi, mode, "CL", risk.forgoProfit, AlgoRole::RISK_CLOSE);

// PairTradingContext.cpp:556  —— 语义③：手动单免超时撤单
if (!pi.autoFlag) { return; }
```

语义①可以接受（"自动模式才开仓"）。语义②是错的：强平最需要**立刻成交**，
而"是不是自动模式"跟成交方式毫无关系。更糟的是它和 `ApplyCommand STOP` 联动 ——
`STOP` 会顺手 `autoFlag = false`（`PairInfoManager.cpp:1000`），
于是**一个刚被"停止开仓"的对子，它的风控强平会从 TT 变成 MT**（主动腿挂单等成交）。

语义③有连锁副作用，见 §1.2。

### 1.2 ⚠️ `STOP` 连带关掉"超时撤单"，并可能造成活锁

`autoFlag = false` 之后 `CheckAlgoOrderTimeout` 直接 return（`:556`），
同时 `UpdateSatisfyTime` 里 `manualReady = !autoFlag && !stopFlag`（`:528`）
**每 tick 刷新 `satisfyTime`**。两件事叠起来：算法单永不因超时被撤、永不终结、对子被永久占住。

方向是反的：停止之后应该更容易撤单，而不是永不撤。

### 1.3 ⚠️ `errorFlag` 把平仓也挡了，而且清除路径不可达

```cpp
// PairTradingContext.cpp:370-374
if (pi.errorFlag) {
    return;          // ← 在派发之前，开仓和平仓一起挡
}
```

`CanOpen:364` 挡开仓是对的；问题在 `:372` 把平仓也挡了。
而 `errorFlag` 恰恰是被 `CheckExposureAbnormal:606` 在**敞口失控**时置位的 ——
**敞口刚失控，策略平仓路径就被堵死**，只剩 `ProcessRisk` 的强平能走（它不判 `errorFlag`，见 `:430-497`）。
若那一刻没有任何风控条件满足，这个仓位就一直挂着等人工。

唯一清除路径是 `PairCmd_RESUME`（`PairInfoManager.cpp:1010-1013`），而 `ApplyCommand` 生产不可达 ⇒
实际只能"改快照 + 重启"。

> 补充：`CheckExposureAbnormal:575` 有 `if (pi.errorFlag) return;`，所以置位后不会重复处理；
> 但如果有人清了它而敞口没解决，下一轮会**立刻再置位并撤单**（`:611-613`）。
> 这意味着"命令里顺手清 errorFlag"是错的：清了 → 报单 → 下一轮又被置位+撤单 → 死循环。

### 1.4 `stopFlag`：语义对，但比 `closeFlag` 更"重"

`SignalGenerator.cpp:249`：

```cpp
if (pi.stopFlag && !pi.HasPosition()) {
    return result;      // 空仓时连平仓信号一起没有
}
```

`HasPosition()` 是 `|pairTotalVolume| > 1e-9`（`PairInfo.h:397-399`）。
所以它只在**已经空仓**时才连平仓一起掐 —— 逻辑上无害（空仓本来没得平），
但语义上它更接近"这个对子停机"，而不是"只平不开"。

### 1.5 ✅ `closeFlag` 是唯一语义干净、且能作用到运行中算法单的

三处读取，方向都对：

1. `SignalGenerator.cpp:191-192`：`canOpenLong/Short` 直接置假（**不动四个开关**，
   所以在"开关刚被压、信号还没重算"的空窗里也不会漏）；
2. `SignalGenerator.cpp:354`：`CanOpen` 里再判一次，只为日志能说出原因；
3. `PairTradingContext.cpp:758`：`ProcessModify` 把四个开仓开关压成 `false` 推给**正在跑的算法单**。

平仓侧完全不受影响（`CanClose:397-410` 只判持仓 / dust）。
⇒ 这就是"只平不开"，而且运行期生效。

⚠️ 但 `closeFlag` **不发平仓单**。`ApplyCommand` 的 `PairCmd_CLOSE`（`:1002-1004`）只置位。
"平"必须靠 `profitPct` + `closeRemainVolume` 走正常信号链路。名字叫"强制平仓"，实际是"只平不开"。

### 1.6 ❌ `limitFlag` 是死闸门

全仓只有 `CanOpen:359-360` 一处读，**没有任何写入点**，**也不在 33 列快照里** ⇒ 恒为 `false`。

"持仓上限"这件事已经由**全局持仓闸门**（`SignalGenerator::m_globalPosExceeded`，
输入 `maxPositionValue`）承担，而它**刻意不复用 `limitFlag`** —— 理由写在 `PairTradingContext.cpp:1226-1227`：
`limitFlag` 会进快照、会被 RESUME 清掉，拿它当"算出来的闸门"用会跟运维操作互相覆盖。

⇒ 当**待删字段**看待，不要再往里写东西。

### 1.7 已消失的字段

`manualFlag` 已从 `PairInfo.h` 删除。`BaseAlgoOrder::isManual` 还在，由 `PairTradingContext.cpp:980`
从 `!pi.autoFlag` 派生，但全仓**只写不读**（`BaseAlgoOrder.cpp:97`、`AlgoContext.cpp:365` 都是清零）。
`tests/market_to_algo/suite.cpp:672` 的断言文案还写着 `pi.manualFlag`，只是文案陈旧，不影响编译。

---

## 2. 运行期入口：机制写好了，入口断了

| 环节 | 状态 |
|---|---|
| `PairTradingStrategy::on_command`（`PairTradingStrategy.cpp:587-588`） | 只往 `algoContext.OnCommand(json)` 转发 |
| `AlgoContext::OnCommand`（`AlgoContext.cpp:240`） | **函数体从 `:243` 起整段被 `/*` 注释掉**，只剩硬编码 DOGE 测试单残骸 |
| `PairInfoManager::ApplyCommand`（`PairInfoManager.cpp:991`） | **零生产调用点**（唯一调用者是测试 G16） |

⇒ 生产上没有任何一条路能在运行中把 `stopFlag` / `closeFlag` 置 true。
唯一办法是改 CSV 快照后重启，而**运行中**改 CSV 无效：`OnTimer` 第 6 步周期性 `SaveSnapshot`
会把它写回去。

---

## 3. 与"单对子紧急平仓"改动的碰撞检查

> ⚠️ **本节写于五 flag 模型（`errorFlag` / `limitFlag` 还在，快照 33 列）。**
> 三态模型落地后，**§3.2 / §3.3 / §3.5 的论据已失效**（结论大部分仍成立）。
> 修订见 **§3.9**，要落地只看 §3.9 的清单。

被审的改动三件套（尚未落码）：

- **A**：`PairInfo::closeRemainVolume{0.0}` + `SignalGenerator::RecalcOrderParams` 里 4 行，
  把 `ttCLEndVolume / ttCSEndVolume / mtCLEndVolume / mtCSEndVolume` 从 `0.0` 改成 `closeRemainVolume`；
- **B**：`RuntimeConfig` 加 `pairCommands`（复用现有热更通道），`OnTimer` 0.6 应用，
  `m_appliedCmdPairs` 做差集 ⇒ "删掉某对子 = 恢复"；
- **C**（可选）：`AlgoContext.cpp:1426` 的终结判据。

### 3.1 碰撞矩阵

| flag | A（改 EndVolume） | B（命令通道） | C（终结判据） |
|---|---|---|---|
| `autoFlag` | 无影响（`RecalcOrderParams` 不读它） | **绝对不要写** | 无影响 |
| `stopFlag` | 无影响 | 不要写（语义重叠，但不是同一个东西） | 无影响 |
| `closeFlag` | 无影响 | **就是它** —— park 机制 | 无影响 |
| `limitFlag` | 无影响 | 不要写（死字段、不进快照） | 无影响 |
| `errorFlag` | 无影响 | **会静默吞掉整条命令** ⚠️ | 无影响 |

### 3.2 `closeFlag` 是 park 的正确选择，但有两个时序坑

为什么不用 `stopFlag`：两者在 `ProcessModify:758` 和 `CanOpen:349/354` 上完全等价，
差别只有 `SignalGenerator.cpp:249` 那一处（空仓时连平仓一起掐）。
`closeFlag` 的注释、`PairCmd_CLOSE` 的语义、快照列 28 的运维含义，三者一致 ⇒ **用它**。

坑 1：**60 秒节流**。`ProcessModify:739` 受 `modifyTimespanSec`（config = 60）节流
⇒ 在跑的单最多 60 秒内还带着旧的开仓开关。
修法不用新写：`OnTimer:1262` 已经有 `pi->algoModifyTime = 0`（`closeOnly` 用的同一招），
命令应用后照做即可。

坑 2：**`closeFlag` 不挡平仓，也不发平仓单**。
所以一条"紧急平仓"命令至少得写三个字段：`closeFlag`（park）+ `profitPct`（放松平仓门槛）
+ `closeRemainVolume`（平到哪）。少写 `profitPct` 就只是"停了不开"，不会真的平。

### 3.3 ⚠️ P1：`errorFlag` 会让命令"静默无效"

`ProcessPairSignal:372` 在派发之前 return，而且它在 `:380` 那行"信号触发"日志**之前**
⇒ 运维视角是"我发了紧急平仓，日志里什么都没有"。

而 `errorFlag` 的两个写入方都是**主动**发生的：
- `:285` 孤儿单冻结 —— 重启时快照 `hasActiveAlgoOrder == true` 就会触发
  （`PairTradingContext.h:144` 的 `freezeOnOrphanAlgoOrder`，默认 true）；
- `:606` 敞口失控。

⇒ 恰恰是"最需要平仓"的两种场景，命令会失效。

**修法（不扩大范围）**：命令应用时若 `pi.errorFlag` 为真，`LOG_ERROR` 明确拒绝，
文案要写清"该对子已被冻结，需要人工在交易所处理或发 RESUME"。
**不要**顺手清 `errorFlag` —— 见 §1.3 末尾：清了会被下一轮重新置位并撤单，变成死循环。

（想根治就得把 `:372` 改成只挡开仓，那是另一项改动，见 §4 的 P1。）

### 3.4 ⚠️ P2：`closeRemainVolume` 会传播到风控强平

`BuildAlgoOrderJson:982-1029` 是"32 个参数整份拷 `pi.orderParams`"，
所以 `closeRemainVolume` 一改，**风控强平单也会平到残余量就停**。
强平的定义是"平干净"，停在半路是错的。

同样，`BuildNormalModify:653/669` 每 60 秒把 `orderParams` 整份推给在跑的单
⇒ 一张**正在跑的强平单**也会被改回部分平仓。

修法（两处，都复用现成的 role 分支）：

- `BuildAlgoOrderJson:1038-1043` 那个 `if (role == AlgoRole::RISK_CLOSE)` 里，
  顺手把四个 `*CLEndVolume / *CSEndVolume` 归零；
- `BuildNormalModify:636-643` 那个 `if (pi.activeAlgoIsRiskClose)` 里，同样归零。

`BuildCloseModify`（`aggressiveClose`，两腿流动性告急）也走 `BuildNormalModify` 继承，
要不要一起归零是另一个决定 —— 我倾向归零（流动性告急更该平干净）。

### 3.5 ⚠️ P3：重启会把"部分平仓"退化成"全平"

| 字段 | 快照 | 重启后 |
|---|---|---|
| `closeFlag` | 列 28 ✅ | 保持，对子仍是"只平不开" |
| `profitPct` | 列 29 ✅ | 保持，平仓门槛还是放松的 |
| `closeRemainVolume` | **不在快照里** ❌ | 归 0 ⇒ 阶梯末端变回 0 ⇒ **平到 0** |

方向是安全的（少留仓比多留仓安全），但必须写进运维文档：
**重启后"平一半"会变成"平干净"**。

反方向也不行：加一列会移动 `errorFlag / hasActiveAlgoOrder / currentAlgoOrderId`
（列 30/31/32）三个索引，`LoadSnapshot` 是按下标硬读的（`PairInfoManager.cpp:443` 起）⇒ **不要加列**。

### 3.6 ⚠️ P4：恢复路径少清一个字段就会震荡

"删掉 `pairCommands` 里的条目 = 恢复"这条，必须等于 `PairCmd_RESUME` 的全部动作 **加** 新增字段：

```
errorFlag = false          // 与 :1010-1013 一致（风险自限，敞口没解决会被重新置位）
stopFlag  = false
closeFlag = false
autoFlag  = true
closeRemainVolume = 0      // ← 新增，漏了这条：恢复后每次平仓仍只平到旧残余量
profitPct = 0.0001         // ← 恢复默认（也可以不回，但要明确）
然后 RecalcOrderParams(*pi) + algoModifyTime = 0
```

漏 `closeRemainVolume` 的后果：恢复后 `closeFlag=false`、开仓恢复，
但**平仓阶梯末端仍是 r** ⇒ 一旦平仓信号触发，只平到 r 就停，永远回不到 0，
下一次信号又触发、又停在 r ⇒ 反复报单/撤单。

`m_appliedCmdPairs` 的种子要在启动时从 `closeFlag == true` 的对子初始化，
否则重启后"删条目"这个动作对重启前就存在的命令不生效。

### 3.7 ⚠️ P5：残余量有 dust 下限

`CanClose:404`：`|pairTotalVolume| < activeParam.minVolume` ⇒ `reason = "dust"`，直接不报单。
所以 `closeRemainVolume` 若落在 `(0, minVolume)` 区间，那一段永远平不掉，
命令应该在校验阶段就拒绝（或直接归零 = 全平）。

### 3.8 C（终结判据）可以不动

部分平仓完成后 `|pairTotalVolume| = |r| >> minSize`，`:1426` 判不出终结
⇒ 算法单空转 150 秒（`satisfyTime` 不再刷新，因为 `vol < CLEndVolume - 1e-9` 已经不成立）
⇒ 超时撤单 → `CANCELED` → `OnAlgoFinished` 释放对子。

`OnAlgoFinished` 在 `fullyFlat == false` 时只对**已触发的**风控条件做 `tierNTimes++`
（`RiskManager.cpp:313-358`），手工平仓不触发风控 ⇒ 没有副作用。

代价只是一次 `CANCELED` + 一条 Lark 消息。**建议不动**（改动越小越好）。
如果一定要它干净地 `FILLED`，判据必须用**算法单自己的** `*CLEndVolume`
（这样 RISK_CLOSE 归零后自然按 0 判），不能用 `pi.closeRemainVolume`。

---

### 3.9 修订（2026-10-29，三态模型落地后）

**结论：主体不变** —— `closeFlag` 当 park、`profitPct` 放松门槛、`closeRemainVolume` 决定平到哪、
`pairCommands` 差集即恢复。但有 6 处要改，其中 **§3.9.3 是唯一会改变实现范围的一条**。

| # | 原文 | 现在 | 处理 |
|---|---|---|---|
| 1 | §3.2「`stopFlag` 与 `closeFlag` 差别只有 `SignalGenerator.cpp:249` 一处」 | ❌ **失效**：`CanClose:389` 现在也判 `stopFlag` ⇒ 拿它 park 连平仓单都发不出去 | 结论不变（用 `closeFlag`），理由改成"`closeFlag` 是**唯一**能只平不开的状态" |
| 2 | §3.3「`errorFlag` 让命令静默无效，日志里什么都没有」 | 🟡 **换壳 + 部分改善**：`errorFlag` 已删，`stopFlag` 接管；但 `ProcessPairSignal` 那个早退删了 ⇒ **有日志**（"信号触发…canClose:false reason:stopFlag = true"）却不下单 | 仍必须**显式拒绝 + LOG_ERROR**；"顺手清 `stopFlag`"现在**更**不能做（§3.9.1） |
| 3 | §3.5「加一列会移动 30/31/32 三个索引」 | ❌ **失效**：`errorFlag` 已删、快照 32 列，加在尾部不动任何索引 | **仍不加**，换理由：32+1 = 33 撞上刚废弃的旧格式，而校验只数个数（§3.9.2） |
| 4 | §3.1 碰撞矩阵 | `limitFlag` / `errorFlag` 两行作废；`autoFlag` 现在只是标签，"绝对不要写"从"危险"降级为"无意义" | 按三态重画 |
| 5 | 方案 B「`RuntimeConfig` 加 `pairCommands`」 | ⚠️ **新约束**：`m_cfgQ` 是 `boost::lockfree::spsc_queue<RuntimeConfig, capacity<16>>`（`PairTradingContext.h:31`），元素整份按值拷，现在 `RuntimeConfig` 是可平凡拷贝的 2 个 bool | 必须用**定长 POD**，不能 `std::string` / `std::vector` |
| 6 | §3.6 恢复路径 | ⚠️ `ApplyCommand` 与三态脱节：`STOP` 仍写 `autoFlag=false`、`RESUME` 写 `autoFlag=true`，都不 `RecalcOrderParams`、不清 `algoModifyTime` | 差集恢复只清 `closeFlag`（+ `closeRemainVolume`），**不碰** `autoFlag` / `stopFlag` |

#### 3.9.1 为什么"顺手清 `stopFlag`"现在更不能做

`CheckExposureAbnormal` 开头现在有 `if (pi.stopFlag) return;`（one-shot，2026-10-29 补的）。
命令把 `stopFlag` 清掉 ⇒ 下一 tick 敞口仍异常 ⇒ 重新置位 + `RequestCancelAlgoOrder`
⇒ **刚发出去的平仓单被撤**；每一轮都这样 ⇒ 永远平不掉。
敞口没解决之前 `stopFlag` 只能人工在交易所处理，不能靠策略自己解。

#### 3.9.2 `closeRemainVolume` 仍然不进快照（理由换了）

`kSnapshotColumns` 现在是 32，再加一列就是 **33** —— 与 2026-10-29 之前的旧格式**列数相同**。
而 `LoadSnapshot` 的表头校验只比较**个数**、不比名字 ⇒ 一份遗留的旧 33 列文件会被当成
"新格式 + `closeRemainVolume`"读进来：`f[30]` 读到的是旧 `errorFlag` 的 `0/1` 却被当成
`hasActiveAlgoOrder`，`f[31]` 读到的是旧 `hasActiveAlgoOrder` 却被当成 `currentAlgoOrderId`
—— **整行错位且不报错**。
⇒ 保持"不进快照"：重启后"平一半"退化成"平干净"（方向安全），写进运维文档即可。

#### 3.9.3 ⚠️ per-pair `closeFlag` 与全局 `m_closeOnly` 的"只平不开"不是同一道闸门

| 来源 | 压 4 个开仓开关的地方 | 新建的平仓单带不带开仓能力 |
|---|---|---|
| 全局 `m_closeOnly`（`RuntimeConfig`） | `RecalcOrderParams` 末尾的 `ApplyNoOpenGate`（`SignalGenerator.cpp:219`）—— **两个出口都过** | 不带 |
| per-pair `pi.closeFlag` | `RecalcOrderParams:194-197` 的 `&& !pi.closeFlag`（**只在正常出口**）+ `ProcessModify:717`（60s 节流） | 正常出口不带；**早退出口仍带** |

⇒ 差别只剩"早退出口"，见 §3.9.3b。

**修法**：把 per-pair 判定收进 `ApplyNoOpenGate`，让两个出口共用同一道闸门。

#### 3.9.3b 现状更新（2026-10-29 晚）：`!closeFlag` 已加回，但只盖住**正常出口**

工作区已改成 `op.ttOLSwitch = canOpenLong && !pi.closeFlag;`（`:194-197`，4 行都是），
所以**正常路径**上"closeFlag 期间建出的平仓单不带开仓能力"已经成立。

⚠️ 但 `RecalcOrderParams` 有**两个出口**，`!closeFlag` 只写在正常出口上：

| 出口 | 条件 | 4 个开仓开关 |
|---|---|---|
| 早退 `:58-63` | `!pi.largeStats.IsValid()`（刚重启 / 统计没攒够 / 分位数文件丢了） | **保持上一轮的值**，只过 `ApplyNoOpenGate`（只管全局 `m_closeOnly` / `m_globalPosExceeded`）⇒ **`closeFlag` 在这里不生效** |
| 正常 `:194-197` → `:219` | 统计有效 | 被 `!closeFlag` 压掉 ✅ |

而 `:59-60` 那行注释自己就写着："刚重启 + 统计还没攒够 正是带着恢复的大仓位、最该只平不开的时刻"
—— per-pair `closeFlag` 恰恰漏在这个时刻。

**修法（最小）**：把 per-pair 判定收进 `ApplyNoOpenGate`，两个出口共用 ——
`ApplyNoOpenGate(OrderParams&)` → `ApplyNoOpenGate(const PairInfo&, OrderParams&)`，
判据 `m_globalPosExceeded || m_closeOnly || pi.closeFlag`；`:194-197` 的 `&& !pi.closeFlag` 留着也无害（幂等）。
两个调用点（`:61`、`:219`）`pi` 都在手。

> `pi.stopFlag` **不必**并进来：`CanClose:389` 已经挡死 stopFlag 下新建平仓单，而 `ApplyNoOpenGate`
> 只影响"建单那一刻"，所以加不加对行为无影响 —— 不加，少一处要同步的判据。

✅ **已落地（2026-10-29）**：签名改成 `ApplyNoOpenGate(const PairInfo&, OrderParams&)`，判据加了
`pi.closeFlag`，两个调用点都传 `pi`；`:194-197` 的 `&& !pi.closeFlag` 保留（计算点先落一次，
中间多插一个 return 也不会漏，末尾闸门再兜）。回归：**D8b**（正常出口）+ **D8c**（早退出口，
用 `ttOLStartSpread` 哨兵证明真的走了早退分支）。

> 遗留（未改）：早退路径下 4 个**平仓**开关也会停在默认 `false`（`PairInfo.h:179-216` 默认全 false，
> 而 `:200-201` 的 `= true` 在早退点之后）⇒ 预热期信号平仓单建不出来。**故意不动**：
> 把它置 true 会让"阈值还是默认 0"的平仓单在预热期立刻成交（等于市价平），是行为变化，要单独决定。

#### 3.9.4 修订后的落点清单

| 项 | 改动 | 位置 |
|---|---|---|
| A | `PairInfo::closeRemainVolume{0.0}`，4 处 `*CLEndVolume / *CSEndVolume` 用它替代 `0.0` | `SignalGenerator.cpp:37/43/49/55` |
| A' | 强平单把这 4 个归零（原 §3.4，不变；`ProcessRisk:434` 明确不判三态 ⇒ 一定会传播） | `BuildAlgoOrderJson` 的 `RISK_CLOSE` 分支、`BuildNormalModify` 的 `activeAlgoIsRiskClose` 分支 |
| A'' | ✅ 已做：`ApplyNoOpenGate(const PairInfo&, OrderParams&)` 并入 `pi.closeFlag`，补上**早退出口**；`stopFlag` 不必并 | `SignalGenerator.h:184`、`.cpp:224` + 调用点 `:61` / `:219` |
| B | `RuntimeConfig` 加**定长 POD** 的 `pairCommands`（`char[64]` 键 + 三态 + `profitPct` + `closeRemainVolume`），差集 = 恢复 | `RuntimeConfig.h`、`ReadRuntimeConfigFile` |
| B' | 命令落地：写三态 + `RecalcOrderParams` + `algoModifyTime = 0`；`stopFlag` 为真则**拒绝 + LOG_ERROR** | `OnTimer` 0.6（`:1188` 那段） |
| B'' | `m_appliedCmdPairs` 启动时从 `closeFlag == true` 的对子播种（`closeFlag` 在快照里，下标 `f[28]`） | `Init` |
| C | 终结判据**不动**（§3.8 结论不变：多一次 `CANCELED` + 一条 Lark） | — |

> ⚠️ **别"顺手清理"`BuildAlgoOrderJson:1039` 的 `*pSwitch = true`**：
> `OrderParams` 的 8 个开关默认**全 false**（`PairInfo.h:173-216`），而 `RecalcOrderParams` 在
> `!largeStats.IsValid()` 时早退、跳过 `op.*CLSwitch = true` 那两行 ⇒ 预热期建出来的强平单
> `op.mtCLSwitch` 可能就是 false，这行是它"真的会去平"的唯一保证。
> 它只把 (algoMode, direction) 对应的**那一个**开关置 true，碰不到 4 个开仓开关（`A11b` 钉住）。

---

## 4. 真问题清单（按优先级）

| # | 问题 | 影响 | 是否属于本次改动 |
|---|---|---|---|
| P1 | `errorFlag` 挡平仓（`PairTradingContext.cpp:372`） | 最需要平仓的两种场景下命令静默失效 | 否，但命令必须显式拒绝并报错 |
| P2 | `closeRemainVolume` 传播到 RISK_CLOSE | 强平只平到半路 | **是**，必须一起改 |
| P3 | `closeRemainVolume` 不进快照 | 重启后部分平仓变全平 | 是，只能文档化 |
| P4 | 恢复路径必须清 `closeRemainVolume` | 漏了就震荡 | **是** |
| P5 | dust 下限 | 残余量过小平不掉 | 是，校验 |
| A1 | `forgoProfit` 被 MODIFY 整份覆盖擦掉 | 已有缺陷 | 否（本改动用 `profitPct`，不受影响） |
| A2 | `autoFlag` 决定强平 TT/MT | 强平变慢 | 否 |
| A3 | `limitFlag` 死字段 | 无 | 否 |

---

## 5. 一句话

> ⚠️ 这段是五 flag 时期的结论，**字段名已变**（`errorFlag` → `stopFlag`），修订见 §3.9。

**五个 flag 里，只有 `closeFlag` 是目标改动真正要碰的（它就是 park 机制本身）；
`autoFlag` / `stopFlag` 一行都别写；`limitFlag` 是死的；`errorFlag` 是唯一会
"静默吃掉这次平仓"的那个 —— 而它恰恰在孤儿冻结和敞口失控时被置位。**

真正需要连带改的不是 flag，而是 `closeRemainVolume` 的**传播面**：
它会被 `BuildAlgoOrderJson` 和 `BuildNormalModify` 整份拷进**风控强平单**，
必须在两处 role 分支里归零。

---

## 6. 散点统一方案（"改一个 flag 要动 4 个地方"）

### 6.1 现状：10 个条件 × 7 个阶段，矩阵是不规则的

| 条件 | A 写开关 | B `CanOpen` | C 掐全部信号 | D 挡派发 | E 推给在跑单 | F 超时豁免 | G 强平方式 |
|---|---|---|---|---|---|---|---|
| `m_closeOnly` | ✅`:225` | ✅`:344` | | | 间接① | | |
| `m_globalPosExceeded` | ✅`:225` | ❌ | | | 间接① | | |
| `closeFlag` | ✅`:191-192` | ✅`:354` | | | ✅`:758` | | |
| `!autoFlag` | ✅`:194-197` | ❌ | | | ❌ | ✅`:556`+`:528` | ✅`:484/488` |
| `stopFlag` | ❌ | ✅`:349` | ✅`:249` | | ✅`:758` | ✅`:528` | |
| `limitFlag` | ❌ | ✅`:359` | | | ❌ | | |
| `errorFlag` | ❌ | ✅`:364` | | ✅`:372` | ❌ | | |
| 流动性 | ✅`:205-208` | ❌ | | | 间接① | | |
| 资金费 | ✅`:213-216` | ❌ | | | 间接① | | |
| 分位带宽 | ✅`:67-69` | ❌ | | | 间接① | | |

① "间接" = 靠 `BuildNormalModify` 整份拷 `op.*Switch` 生效，没有独立判断。

统计：五个 flag 一共被读 **21 次、跨 2 个文件、14 个闸门点**
（`SignalGenerator.cpp` 11 次 / 7 点，`PairTradingContext.cpp` 10 次 / 7 点；
另 `:575` 那次是 `CheckExposureAbnormal` 的重入保护，不算闸门）。

四个不规则之处，全都是"后来加条件时漏了某一路"：

1. `stopFlag` 不压开关，但 `closeFlag` 压 —— 于是"停止开仓"时，
   **新建的平仓单里 4 个开仓开关还是 true**，平仓途中能反手开仓；
2. `errorFlag` / `limitFlag` 只在 `CanOpen` 里挡，**在跑的单一个都没被压**；
3. `m_globalPosExceeded` 压开关但不进 `CanOpen`，日志里永远说不出"是全局闸门关的"；
4. `!autoFlag` 压开关但不进 `CanOpen`，日志原因显示成"没有信号"。

### 6.2 目标模型：三个 flag 就是一个三态（2026-10-29 定）

你说的这个模型是对的，而且它比"5 个 bool 各管一段"简单得多 ——
三个 flag 其实只是**一个枚举**，权限是两个派生判据：

| 状态 | 触发条件 | 能开仓 | 能平仓 |
|---|---|---|---|
| `kOpenAndClose`（正常） | 三个都没置 | ✅ | ✅ |
| `kCloseOnly`（只平不开） | `closeFlag` | ❌ | ✅ |
| `kStopped`（全停） | `stopFlag` 或 `errorFlag` | ❌ | ❌ |

优先级：`stopFlag` / `errorFlag` > `closeFlag`（两个都置 = 全停）。

```cpp
// PairInfo.h —— 纯 PairInfo 状态，不依赖任何单例，可以单独测
enum class PairGate { kOpenAndClose, kCloseOnly, kStopped };

inline PairGate GateOf(const PairInfo& pi) {
    if (pi.stopFlag || pi.errorFlag) return PairGate::kStopped;
    if (pi.closeFlag)                return PairGate::kCloseOnly;
    return PairGate::kOpenAndClose;
}
inline bool CanOpenByGate(PairGate g)  { return g == PairGate::kOpenAndClose; }
inline bool CanCloseByGate(PairGate g) { return g != PairGate::kStopped; }
```

`errorFlag` 与 `stopFlag` 权限档相同，但**字段不合并**：前者是系统写的（孤儿冻结 `:285` /
敞口失控 `:606`），后者是运维写的，快照列不同（27 / 30），语义也不同。

⚠️ **边界**：这三态管的是**策略信号**（`CanOpen` / `CanClose` / `CheckSignalForSatisfy`）。
**风控强平不走信号** —— `ProcessRisk` 直接建单，所以它不在三态里。见 6.5 Q1。

⚠️ **另一个事实**：`stopFlag` 在现网**恒为 false**。它唯一的写入点是 `ApplyCommand` 的
`PairCmd_STOP`（`PairInfoManager.cpp:999`），而 `ApplyCommand` 生产零调用（只有测试 G16）。
敞口失控时置的是 `errorFlag`（`PairTradingContext.cpp:606`），不是 `stopFlag`。
所以三态里的 `kStopped` 现在实际上只由 `errorFlag` 驱动。

全局那两个（`m_closeOnly` / `m_globalPosExceeded`）不在这三个 flag 里，它们是"全局只平不开"，
合成只有一行：`openAllowed = CanOpenByGate(GateOf(pi)) && !m_closeOnly && !m_globalPosExceeded;`
它们**不改变** `GateOf`，也不去写 `closeFlag` —— 理由见 `PairTradingContext.cpp:1226`。

`autoFlag` 从这个模型里**完全退出**：它只是"参数由谁算"的标签（自动 / 手动），
两种模式下都能开能平。这与祖先 `pair_info_manager.py` 一致：
`auto_flag == False` 的行不被 `calculate_pair_order_params` 覆盖，但**照常交易**。

### 6.3 落点：4 处改 + 2 处删

| # | 位置 | 改成 |
|---|---|---|
| 1 | `RecalcOrderParams:191-197` | 删掉 `&& pi.autoFlag`，四个开关统一 `canOpenLong && CanOpenByGate(g)` |
| 2 | `CanOpen:338-395` | 8 个 if 全删 → `return CanOpenByGate(g) && !m_closeOnly && !m_globalPosExceeded;`（reason 按 `GateOf` 给一句话） |
| 3 | `CheckSignalForSatisfy:249` | `if (!CanCloseByGate(g)) return result;`（替代 `stopFlag && !HasPosition()`） |
| 4 | `ProcessModify:758` | `if (!CanOpenByGate(g)) 压 4 个开关`（替代 `stopFlag \|\| closeFlag`） |
| 删 | `ProcessPairSignal:372-374` | `if (pi.errorFlag) return;` 整段删 —— #3 已经让全停档没有信号，`:376` 自然返回 |
| 删 | `PairInfo.h:317` + `CanOpen:359-360` | `limitFlag` 字段与唯一读者一起删（无写入点、不进快照） |

`ApplyNoOpenGate`（`:225`）保持原样，只管两个全局源 —— 与三态正交，不用改签名。

`autoFlag` 的三处非权限读点**本次不动**：`:556`（免超时撤单）、`:528`（`manualReady`）、
`:484/488`（强平走 TT 还是 MT）。它们不是权限，见 6.5 Q2。

### 6.4 与现状的 5 处行为差异（都要点头）

| # | 差异 | 为什么 |
|---|---|---|
| 1 | `autoFlag=false` 不再挡开仓 ⇒ **手动对子会开仓** | 这才是祖先语义。今天的写法让"手动"等于"永久禁开"，而且 `autoFlag` 进快照列 26 ⇒ 重启也开不了，只能改快照 |
| 2 | `stopFlag` 现在**连平仓一起挡**（今天只在空仓时挡） | 你的定义就是"不能平也不能开" |
| 3 | `errorFlag` 归到全停档 | 今天 `:372` 本来就是"开平都挡"，只是没进开关；归到枚举后行为一致，还少一处判断 |
| 4 | `limitFlag` 删除 | 无写入点、不进快照、唯一读者在 `CanOpen` |
| 5 | `ProcessPairSignal:372-374` 整段删 | 由 #3 的"没有信号"自然覆盖，等价 |

### 6.5 两个要你定的点

**Q1 风控强平要不要也被三态挡住？—— 结论：不挡（2026-10-29）**

先纠一个前提：敞口失控时置的是 **`errorFlag`**（`PairTradingContext.cpp:606`），不是 `stopFlag`；
`stopFlag` 在现网根本没有写入点（见 6.2 末尾）。所以这一问实际是
"`errorFlag` 的对子要不要挡 `ProcessRisk`"。

不挡，四条理由：

1. **死锁**。`errorFlag` 已经挡住了信号平仓（`ProcessPairSignal:372`），
   `ProcessRisk` 是**唯一**还能建平仓单的路径。再挡掉它，这个对子就彻底没有出口 ——
   而 `errorFlag` 的唯一清除路径（`ApplyCommand RESUME`）生产不可达，只能改快照重启。
2. **挡住的是保险丝**。`ProcessRisk` 覆盖的是 ADL 逼近强平、资金费异常、价差 24h 不回归
   （`:430-497`）。这些场景下不平的代价是交易所强平（额外强平费 + 更差的滑点）。
3. **敞口异常本身就该能减仓**。净敞口 > `exposureCancelTimes(4.0) × ttTargetVolume`
   说明仓位已经不对了，这时候最需要的动作就是平。
4. **"连保险丝也停"已有开关**：全局 `stopAllAlgo` 在 `ProcessRisk:445` 直接 return，
   粒度是全局、语义是急停 —— 那才是"什么都别动"的正确工具。

所以三态的正确表述是：**"策略不再自己开平"**，不是"这个对子冻结"。
风控强平是独立保护层，不受三态约束。

（顺带一个现存的自相矛盾，属于 §1.3 那条：`errorFlag` 挡住信号平仓之后，
"纯敞口异常 + 没有任何风控条件成立"的对子就是**完全无法平仓**的 ——
`CheckExposureAbnormal` 只置标记 + 撤单，不发平仓单。要不要让 `errorFlag` 落到
`kCloseOnly` 而不是 `kStopped`（能平不能开），是另一个决定，不在本次范围。）

**Q2 `autoFlag` 还要不要保留"免超时撤单"？**
今天 `:556` 让 `autoFlag=false` 的算法单永不超时撤单。如果"手动"只是"参数不由策略算"，
那它的单也该照常超时撤 —— 但祖先 `:1018-1020` 就是明确豁免的。
建议保留现状（本次不动），只把它记成"`autoFlag` 的第二个非权限语义"。

### 6.6 决定（2026-10-29）

本节是设计存档，**代码未动**；与"单对子紧急平仓"一起落，避免同一个函数改两次。

紧急平仓只用到 `closeFlag`（= `kCloseOnly`），它已经在这个枚举里 ——
所以模型落地不会给紧急平仓增加改动量，"平一半"只是在枚举之外多带一个 `closeRemainVolume`。

---

## 7. 落地复核（2026-10-29，commit `00d7b3f` / `d7913b4` / `f8af754`）

### 7.1 改对的

| # | 改动 | 位置 |
|---|---|---|
| 1 | 只留 `autoFlag` / `closeFlag` / `stopFlag`，`limitFlag` / `errorFlag` 删除，注释与模型一致 | `PairInfo.h:313-315` |
| 2 | `CanClose` 加 `stopFlag` ⇒ 全停档连平仓一起挡 | `SignalGenerator.cpp:389-392` |
| 3 | `CanOpen` 删掉 `limitFlag` / `errorFlag` 两条，保留 `stopFlag` / `closeFlag` | `SignalGenerator.cpp:345-357` |
| 4 | 快照 33 → 32 列，header / writer / parse **三处都是 32、列序一致**（逐列数过） | `PairInfoManager.cpp:95-110 / 298-335 / 435-443` |
| 5 | 孤儿冻结与敞口失控都改置 `stopFlag`，RESUME 仍是唯一清除路径 | `PairTradingContext.cpp:284` / `:552` |
| 6 | `ProcessModify` 仍按 `stopFlag \|\| closeFlag` 压 4 个开仓开关 | `PairTradingContext.cpp:690-695` |
| 7 | `algo_exec` **212 / 0 全绿** | — |

### 7.2 必须处理

**P0-1 `ProcessRisk` 强平方式硬编码成 `"MT"`**（`:451` / `:455`）—— ✅ **已决定：保留 `"MT"`**
原来是 `pi.autoFlag ? "TT" : "MT"`，默认 `autoFlag=true` ⇒ 走 TT（两腿吃单立刻成交）。
现在一律 MT（主动腿挂单等成交）。用户 2026-10-29 决定**默认 MT**，代码不动。
遗留：`algo_exec:1408` 那个 `pi.autoFlag = false; // 手动模式 -> algoMode = "MT"` 的用例
**现在是空断言**（无论 autoFlag 是什么都是 MT），TT 分支没有任何覆盖 —— 想守住"强平走 MT"
这个决定，应该把它改成直接断言 `algoMode`，否则将来谁改回去都不会红。

**P0-2 旧快照（33 列）在升级后第一次重启会被整份拒绝** —— ✅ **已定：不做兼容，升级前删文件**
- 现状：`LoadSnapshot` 表头列数不等于 `kSnapshotColumns` 就 `return -1`，`Init:101-105` 只
  `LOG_WARN` 一句"按无持仓启动"，**不中断**。
- `pairTotalVolume` 还能靠 `ReconcilePair`（`:1253`）从交易所纠正；但 `pairActiveTotalPrice` 会留在
  `-1` 哨兵 ⇒ `GetLockedSpread()`（`BaseAlgoOrder.cpp:361-364`）算成 `(-1)/(-1)-1 = 0`
  ⇒ 平仓阶梯的 `profitPct` 守卫用错基准。
- **决定（2026-10-29）**：不写兼容分支，升级时直接删掉旧快照文件，一律按 32 列读。
- ⚠️ **删文件的前提**（整份被拒会同时丢三样东西，而且只 warn 不报错）：
  1. `hasActiveAlgoOrder` —— 孤儿单告警/冻结靠它，丢了会在残留子单上再开一笔；
  2. 风控档位（`adlClose` / `spreadNoRegression` / `fundingAbnormal`）—— 重置成"没耐心"；
  3. 建仓基准 `pairActiveTotalPrice` —— 见上面的 `-1` 哨兵。
  ⇒ **先平干净 + 确认没有在途算法单，再删文件重启**。
- `LOG_ERROR` 已带提示："只认当前格式；33 列旧快照请先删除文件再启动"。

**P1-1 两套测试都编译不过** —— ✅ **已修：`market_to_algo` 493 / 0、`algo_exec` 212 / 0、`param_calc` 正常出报告**
原来 `market_to_algo` **15 个错误**、`param_calc` **3 个**（`suite.cpp:644 pi.manualFlag` 是 `f8af754` 留下的，
`limitFlag` / `errorFlag` 那 14 个是 `00d7b3f` 留下的）。修法不是简单删断言，而是按三态语义重写：
`B6` 的 blocker 列表缩到 `{stopFlag, closeFlag}`；`B9` / `E8` / `E10` / `E11` / `G5` / `G6` / `G16` / `J1`
全部改判 `stopFlag`；`A12` 的 `manualFlag` 改成 `autoFlag = false`（`isManual = !autoFlag`）。
⇒ **三态语义现在有覆盖了**（`CanClose` 挡平仓 = `B9`，敞口 one-shot = `E10`）。

编译通过后还暴露出 3 个**运行时**回归（都不是编译错误，是 `00d7b3f` / `9d80d0d` 顺手删掉的防御）：
1. `RequestCancelAlgoOrder` 的 `!m_algoOrderModifyCb` / `!hasActiveAlgoOrder` 两个守卫 → `E6` 抛
   `bad_function_call` 并让"无活跃单 → 不撤单"失效；**已恢复**。
2. `ProcessModify` 的 `!m_algoOrderModifyCb` → 同源问题；**已恢复**。
3. `SubmitAlgoOrder` 开头的 `!m_algoCommandCb`（`9d80d0d` 删的）→ `A15` 直接 abort；**已恢复**
   （必须放在 `BuildAlgoOrderJson` 之前，否则对子会被一张永远报不出去的单锁死）。
4. 启动闸门 / `stopAllAlgo` 被 `00d7b3f` 从 `ProcessPairSignal` 搬到 `OnSpread` ⇒ `B10` / `G14` /
   `K11` / `K13` 四条红。**已搬回 `ProcessPairSignal` 开头**：`OnSpread` 是唯一调用方，生产等价；
   而这些用例没法改走 `OnSpread`（它先 `UpdateRtSpread` 会用入参行情覆盖 `pi.rtSpread`）。

**P1-2 `CheckExposureAbnormal` 丢了 one-shot guard** —— ✅ **已修**
原来开头是 `if (pi.errorFlag) return;`，`00d7b3f` 之后没有对应判断 ⇒ 敞口仍异常时**每秒**重复
`LOG_ERROR` + 重复 `RequestCancelAlgoOrder`（强平单刚报出去就被撤，档位升不上去）。
修法：函数开头 `if (pi.stopFlag) return;`，顺带覆盖孤儿冻结（那种情况账本本就不可信）。
`E10` 重新钉住这条不变量。

**P1-3 `autoFlag` 仍然禁开**（`SignalGenerator.cpp:194-197` 的 `&& pi.autoFlag`）—— 🟡 **工作区已改，但带出一个新洞**
与"`autoFlag` 只是自动/手动标签，两种模式都能开平"矛盾。删掉这个条件就是"手动对子会开仓"，
是真实的行为变化 ⇒ 这一条要单独确认。

> **2026-10-29 晚工作区现状**（未提交）：`RecalcOrderParams:191-197` 把
> `&& !pi.closeFlag` 和 `&& pi.autoFlag` **两个都删了**。前者删掉带来一个洞：
> `pi.closeFlag` 从此**只**影响 `CanOpen:350` 与 `ProcessModify:717`，**不再进 `orderParams`**。
> 而 `BuildAlgoOrderJson:927` 是原样拷贝 `op.ttOLSwitch` 的 ⇒ `closeFlag` 期间新报出的
> **平仓单**会带着 4 个开仓开关 = true，要等下一次 `ProcessModify`（周期 `modifyTimespanSec=60`）
> 才被压掉 ⇒ 最长 60s 内这张平仓单可以在开仓带**反向开仓**，与"只平不开"相反。
> 改之前不会：那时 `canOpenLong` 带了 `!pi.closeFlag`，开关在建单前就是 false。
> 两个修法（二选一，都只动一处）：
> ① 在 `BuildAlgoOrderJson` 拷完 4 个开仓开关后加 `if (pi.stopFlag || pi.closeFlag) { 4 个 = false; }`
> （与 `ProcessModify:717` 同一判据，单点收口）；
> ② 保留 `&& !pi.closeFlag`，只删 `&& pi.autoFlag`（最小改动，回到原状 + P1-3 的目的）。

### 7.3 一致性问题（不阻塞，但会咬人）

| 位置 | 问题 |
|---|---|
| `PairInfoManager.cpp:993` | ⬜ `PairCmd_STOP` 仍写 `autoFlag = false` ⇒ 停止开仓顺带把对子变成"手动单"（免超时撤单） |
| `PairInfoManager.cpp:1002` | ⬜ `PairCmd_RESUME` 仍写 `autoFlag = true` ⇒ 覆盖运维设的手动/自动 |
| `PairTradingContext.cpp:465` | ✅ 已恢复 `RequestCancelAlgoOrder` 的 `!m_algoOrderModifyCb` / `!hasActiveAlgoOrder` 两个守卫（`00d7b3f` 删的；不恢复则 `E6` 抛 `bad_function_call`） |
| `PairTradingContext.cpp:712` | ✅ 已恢复 `ProcessModify` 的 `!m_algoOrderModifyCb` 检查（同源） |
| `PairTradingContext.cpp:736` | ✅ 已恢复 `SubmitAlgoOrder` 开头的 `!m_algoCommandCb`（`9d80d0d` 删的；不恢复则 `A15` abort，且回调为空时会 `SetActiveAlgoOrder` 白占对子） |
| `PairInfoManager.cpp:999` | ⬜ 注释断句："错误态也在这里复活：stopFlag 的唯一写入方是 CheckExposureAbnormal，" —— 实际还有 `HandleOrphanAlgoOrders:284` |
| `PairInfoManager.cpp:291` | ✅ 注释已改成 profitPct=29 / hasActiveAlgoOrder=30 / currentAlgoOrderId=31，并说明只认当前列数 |
| `PairTradingContext.h:144` / `:294` | ✅ 注释里的 `errorFlag` 已改成 `stopFlag` |
| `PairTradingContext.cpp:499` | ⬜ "风控强平单不豁免" 后面接 "两头都站不住："，前半句被删成了断句 |

### 7.4 修复记录（2026-10-29 晚）

| 项 | 动作 | 验证 |
|---|---|---|
| P0-1 | **不改代码**，保留 `ProcessRisk` 的 `"MT"`（用户决定） | — |
| P0-2 | **不写兼容**：`LoadSnapshot` 只认 32 列，旧文件升级前删掉；`LOG_ERROR` 里带上提示 | `market_to_algo` 全绿（无 33 列用例） |
| P1-1 | 两套测试按三态语义重写（不是删断言）；另恢复 4 处被顺手删掉的运行时防御 | `market_to_algo` **493 / 0**、`algo_exec` **212 / 0**、`param_calc` 正常 |
| P1-2 | `CheckExposureAbnormal` 开头加 `if (pi.stopFlag) return;` | `E10` 重新钉住"已标记 → 不重复撤单" |
| 注释 | `PairInfoManager.cpp:291`、`PairTradingContext.h:144/:294` 的 `errorFlag` 改成 `stopFlag` | 编译通过 |

用例数变化：`market_to_algo` 从 495 → **493**（`B6` 的 blocker 从 4 个减到 2 个，−2）。
（中途曾加过 `G15b` 验证 33 列兼容读，随"不做兼容"的决定一起删掉了。）

⬜ **仍未做**：P1-3（`autoFlag` 仍禁开）、§7.3 里标 ⬜ 的 3 条、以及 §7.2 P0-1 提到的
"`algo_exec:1408` 是空断言，TT 分支无覆盖"。
