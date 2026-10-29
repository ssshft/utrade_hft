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
