# PairInfo 六个 flag 的审计

> 2026-10-21。只读审计，**没有改任何代码**。
> 声明位置：`quant_library/basic/PairInfo.h:239-244`。
> 你给的定义：`autoFlag` 可开可平；`stopFlag` 停止开仓；`closeFlag` 只平不开；`errorFlag` 错误不开仓。

---

## 0. 一张总表：谁写、谁读、实际效果

| flag | 写入点 | 读取点 | 实际效果 | 判定 |
|---|---|---|---|---|
| `autoFlag` | CSV 快照 `f[26]`；`ApplyCommand STOP→false` / `RESUME→true` | `RecalcOrderParams:147-150`、`ProcessRisk:402,406`、`UpdateSatisfyTime:455`、`CheckAlgoOrderTimeout:483` | **同时是"自动模式"、"开仓总开关"、"强平走 TT 还是 MT"** | ❌ 三重语义混用 |
| `stopFlag` | CSV `f[27]`；`STOP→true` / `RESUME→false` | `CheckSignalForSatisfy:189`、`CanOpen:247`、`ProcessModify:664` | 挡开仓；运行中的算法单也能被压掉开仓开关 | ⚠️ 语义对，但被 `autoFlag` 绑架 |
| `closeFlag` | CSV `f[28]`；`CLOSE→true` / `RESUME→false` | `RecalcOrderParams:144-145`、`CanOpen:252`、`ProcessModify:664` | 挡开仓、不挡平仓 = 只平不开 | ✅ 语义正确 |
| `limitFlag` | **无** | `CanOpen:257` | 闸门永远不成立 | ❌ 死闸门 |
| `errorFlag` | `PairTradingContext:213`（孤儿单）、`:536`（敞口失控）；`RESUME` 清除 | `CanOpen:262`、`ProcessPairSignal:298` | **开仓和平仓一起挡** | ⚠️ 与你说的"只挡开仓"不符 |
| `manualFlag` | **无** | `PairTradingContext:875` → `pAlgoOrder->isManual` | `isManual` 也只写不读 | ❌ 死字段 |

⚠️ 一个前提：`ApplyCommand`（`PairInfoManager.cpp:899-944`）**在生产里零调用点**。
所以除 `errorFlag` 外，其余四个 flag 的"运行时写入"全部不可达 ——
生产上它们的值只来自 **CSV 快照**（`PairInfoManager.cpp:343-349`），也就是"上次重启前的值"，
首次启动则全取 `PairInfo.h` 的默认值。

---

## 1. 逐条判：哪些是错的

### 1.1 ❌ `autoFlag` 被当成三样东西用，其中一处是错的

```cpp
// PairTradingContext.cpp:402 / 406  —— 风控强平
std::string mode = pi.autoFlag ? "TT" : "MT";
SubmitAlgoOrder(pi, mode, "CL", risk.forgoProfit);
```

**强平走 TT 还是 MT，由"是不是自动模式"决定。** 这两件事没有任何关系。
更糟的是它和 `ApplyCommand STOP` 联动：`STOP` 会 `autoFlag = false`（`PairInfoManager.cpp:908`），
于是**一个刚被"停止开仓"的对子，它的风控强平会从 TT 变成 MT** ——
从"两腿吃单立刻成交"变成"主动腿挂单等成交"。强平最需要的是立刻成交，
而"停止开仓"这个动作的副作用恰好把它改慢了。

`autoFlag` 另外两处用途本身是合理的：
- `RecalcOrderParams:147-150`：`autoFlag` 参与开仓开关 —— 等价于"自动模式才开仓"，可以接受；
- `CheckAlgoOrderTimeout:483`：`!autoFlag` 直接 return，**手动单不超时撤单** —— 这条有副作用，见 1.2。

### 1.2 ⚠️ `STOP` 会连带关掉"超时撤单"

```cpp
// PairInfoManager.cpp:906-909
case PairCmd_STOP:
    pi->stopFlag = true;
    pi->autoFlag = false;   // ← 这一行
```

`autoFlag = false` 之后，`CheckAlgoOrderTimeout:483-485` 直接 return。
⇒ **停止开仓的对子，它的算法单再也不会因为"150 秒没机会"被撤掉。**

方向是反的：停止之后应该更容易撤单，而不是永不撤。
（祖先 Python 里 `auto_flag == False` 表示"人工单"，人工单不该被机器撤，逻辑本身没错；
错的是把"停止开仓"也映射成了"人工单"。）

### 1.3 ⚠️ `errorFlag` 把平仓也挡了

```cpp
// PairTradingContext.cpp:296-300
// 敞口异常（CheckExposureAbnormal 打的 errorFlag）：……既不开也不平，留人工处理。
if (pi.errorFlag) {
    return;
}
```

你说的是"错误不开仓"。代码是"开平都挡"。
而 `errorFlag` 恰恰是被 `CheckExposureAbnormal:536` 在**敞口失控**时置位的 ——
**敞口刚失控，策略平仓路径就被堵死**，只剩 `ProcessRisk` 的强平能走（它不判 `errorFlag`）。
如果那一刻没有任何风控条件满足，这个仓位就会一直挂着，等人工。

`CanOpen:262` 里那道 `errorFlag` 检查是符合你描述的；问题只在 `ProcessPairSignal:298`。

### 1.4 ❌ `limitFlag` 是死闸门，`manualFlag` 是死字段

- `limitFlag`：全仓只有 `CanOpen:257` 一处读，**没有任何写入点** ⇒ 这道检查永远不成立。
  （"持仓上限已达"应该由 `RiskManager` 或 `RecalcVolumeParams` 在算出 `maxVolume` 时置位，现在没人置。）
- `manualFlag`：**没有任何写入点**，恒为 `false`；唯一读取是 `PairTradingContext:875`
  赋给 `pAlgoOrder->isManual`，而 `isManual` 全仓也只写不读（`BaseAlgoOrder.cpp:100`、`AlgoContext.cpp:377`）。

### 1.5 ✅ `closeFlag` 是对的，而且它是唯一一个"能改到运行中算法单"的

`closeFlag` 做了两件事，方向都对：
1. `RecalcOrderParams:144-145`：把开仓判据直接置假（`canOpenLong/Short`）；
2. `ProcessModify:664-669`：把四个开仓开关压成 `false` 推给**正在跑的算法单**。

平仓侧完全不受影响（`CanClose:295-301` 只判 `HasPosition`）。
这就是你要的"只平不开"，而且它在运行期是生效的 —— 前提是 `closeFlag` 能被置上，见 §2。

---

## 2. 运行期改参：机制已经写好了，入口断了

### 2.1 机制在哪

```cpp
// PairTradingContext.cpp:660-669
if (pi.stopFlag || pi.closeFlag) {
    mod.ttOLSwitch = false;
    mod.ttOSSwitch = false;
    mod.mtOLSwitch = false;
    mod.mtOSSwitch = false;
}
...
m_algoOrderModifyCb(pi.currentAlgoOrderId, stra::CommandType_MODIFY, &mod);
```

这段就是"行情有风险 → 运行中的算法单立刻只平不开"。**不需要新写。**

### 2.2 入口为什么走不到

| 环节 | 状态 |
|---|---|
| `PairTradingStrategy::on_command`（`PairTradingStrategy.cpp:529-531`） | 只往 `algoContext.OnCommand(json)` 转发 |
| `AlgoContext::OnCommand`（`AlgoContext.cpp:252`） | **整个函数体从 255 行起被 `/*` 注释掉**，只剩硬编码 DOGE 测试单的残骸 |
| `PairInfoManager::ApplyCommand`（`PairInfoManager.cpp:899`） | **零生产调用点**（你们自己的 `restart_recovery_design.md:38` 也这么写） |

⇒ **生产上没有任何一条路能在运行中把 `stopFlag` / `closeFlag` 置 true。**
唯一的办法是改 CSV 快照后重启 —— 而**运行中**改 CSV 是无效的：
`OnTimer` 每 10s 一次 `SaveSnapshot`（`PairTradingContext.cpp:1187-1192`）会把它写回去。

### 2.3 即使接通，还有三个时效/范围问题

1. **最多 60 秒延迟**：`ProcessModify:651` 受 `modifyTimespanSec`（`etc/config.json:48` = `60`）节流。
   从"决定只平不开"到"算法单真的不再开仓"，最坏要等 60 秒。
2. **只影响新 pairOrder**：改参改的是算法单的四个开关，**已经在场的子单不会被撤**。
   要立刻停手，还得同时调 `RequestCancelAlgoOrder`。
3. **只在有算法单时生效**：`ProcessModify:644` 对 `!hasActiveAlgoOrder` 直接 return；
   且要求 `m_algoOrderModifyCb` 已注册。没有算法单时改 flag 只影响下一次建单，这是对的。

---

## 3. 建议（按"改动量 / 收益"排，都还没动代码）

### 3.1 先接通入口 —— 三选一

| 方案 | 怎么做 | 代价 |
|---|---|---|
| **A. 指令文件轮询**（推荐） | `PairTradingContext::OnTimer` 顶部每 N 秒读一个 `commands.json`，逐条转 `ApplyCommand` | 零新依赖、不动协议；延迟 = timer 周期；`restart_recovery_design.md §6.1` 已经提过这条 |
| B. 恢复 `OnCommand` | 把 `AlgoContext.cpp:252` 的函数体从注释里救回来，解析 json 分发到 `ApplyCommand` | 要定义命令格式；但顺带把 rebalance 那几条也救活了 |
| C. 共享内存命令队列 | 用现成的 shm 基础设施 | 最重，收益不比 A 高 |

### 3.2 安全侧改参不要等 60 秒

`ProcessModify` 的节流应该**区分方向**：
- **收紧**（把开仓开关从 true 压成 false、把 `closeFlag` 置上）：立即推，不等周期；
- **放宽**（恢复开仓、放大参数）：照旧等 `modifyTimespanSec`。

理由：节流是为了"别把参数抖来抖去"，而"关闸"不是抖动。

### 3.3 拆掉 `autoFlag` 的三重语义

建议拆成两个字段，语义各自单一：

| 新字段 | 含义 | 谁读 |
|---|---|---|
| `manualMode` | 人工单模式（豁免超时撤单） | `CheckAlgoOrderTimeout` |
| `forceCloseMode`（TT/MT） | 风控强平用哪种方式 | `ProcessRisk` |

并且 **`STOP` 不再动 `autoFlag`**：停止开仓就只置 `stopFlag`，
不要顺带把对子变成"人工单、永不超时撤单"。

### 3.4 `errorFlag` 的边界要明确

按你的定义（只挡开仓），应该改成：
- `CanOpen` 里继续挡（已经是了）；
- `ProcessPairSignal:298` 那个早退**去掉平仓侧**，即 `errorFlag` 只挡 `canOpen`，不挡 `canClose`。

否则"敞口失控 → 置 errorFlag → 禁止平仓"是个自相矛盾的闭环。
（`ProcessRisk` 的强平不受影响，这点目前是对的。）

### 3.5 死字段要么接上要么删

- `limitFlag`：接 `RiskManager`/`RecalcVolumeParams`（`maxVolume` 算出来为 0 或触顶时置位），否则删。
- `manualFlag`：如果确实要"人工单"这个概念，就从指令入口写；否则连同 `BaseAlgoOrder::isManual` 一起删。

---

## 4. 一句话

**六个 flag 里，`closeFlag` 是唯一一个语义正确、且能作用到运行中算法单的；
`autoFlag` 被当成三样东西用（其中"决定强平方式"是错的）；
`stopFlag` 语义对但被 `autoFlag` 绑架；`errorFlag` 把平仓也挡了；
`limitFlag` / `manualFlag` 根本没有写入点。**

而"运行中改成只平不开"这件事，**代码已经写好了（`ProcessModify:664-669`），
缺的只是一条能把 `stopFlag`/`closeFlag` 置上的入口** —— 那条入口现在是空的。
