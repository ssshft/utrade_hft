# 算法单源头链路审查（开仓 / 平仓 / 撤单）

审查对象：`utrade_hft` 策略层 `PairTradingStrategy` → `pt::PairTradingContext` → `pt::SignalGenerator` / `pt::RiskManager` → `AlgoContext` → `BaseAlgoOrder`。

审查目的：确认「由行情判断创建算法单」「撤算法单」「正常平仓算法单」「有风险时的撤单与平仓（价差不回归 / funding 异常 / ADL）」这四条源头链路是否真的能跑通。

**本次只做审查，未修改任何源码。**

---

## 〇、结论速览

| 链路 | 是否可用 | 卡在哪 |
|---|---|---|
| 行情 → 创建算法单（开仓） | ❌ **完全不可用** | 4 个独立阻塞点串联，见 §2.1–§2.4 |
| 行情 → 创建算法单（正常平仓） | ❌ 不可用 | 同上；`RecalcOrderParams` 还会在 `!autoFlag` 时关掉 TT 平仓开关 |
| 算法单终结（成交后收尾） | ❌ **不可用** | `OnTimer` 的终结判据用错了量纲，见 §2.5 |
| 撤算法单（外部指令） | ❌ **不可用** | `AlgoContext::OnCommand` 整个函数体被注释，见 §3.1 |
| 撤算法单（内部/风控触发） | ❌ 不存在 | 策略层与风控层没有任何撤算法单的出口，见 §3.2 |
| 撤子单（单腿） | ✅ 可用 | `BaseAlgoOrder::CancelOrderOnSpread` 的 else 分支（时间/价格/大单撤单） |
| 风控强平（价差不回归 / funding / ADL） | ⚠️ 逻辑在，但被判据 bug + §2 阻塞 | 见 §4 |

一句话：**子单层面的撤单是活的；算法单层面的「创建 → 终结 → 撤销」这条闭环是断的。** 用硬编码测试单能开能平，恰恰是因为它绕过了信号层与 `BuildAlgoOrderJson` 的全部校验。

---

## 一、调用链全景（标注 live / dead）

```
[行情线程]
PairTradingStrategy::on_dbpdata()                    PairTradingStrategy.cpp:113
├── algoContext.OnSpread(topic, pdata)               AlgoContext.cpp:1025   (live)
└── ptContext.OnSpread(topic, pdata)                 PairTradingContext.cpp:57
    ├── pim.UpdateRtSpread(pairKey, pdata)           :66   (live)
    └── ProcessPairSignal(*pi)                       :68   (live 但永远走不出第 84 行)
        ├── sg.CanOpen / sg.CanClose                 SignalGenerator.cpp:224 / 271
        ├── sg.CheckSignal(pi)                       SignalGenerator.cpp:158
        └── SubmitAlgoOrder(pi, mode, dir)           :163
            ├── BuildAlgoOrderJson(...)              :184
            ├── pim.SetActiveAlgoOrder(...)          :178
            └── m_algoCommandCb → AlgoContext::SubmitAlgoOrder   AlgoContext.cpp:80 (live)

[定时器]
PairTradingStrategy::on_timer()                      PairTradingStrategy.cpp:94
├── algoContext.OnTimer(utcTime)                     AlgoContext.cpp:1826   (live，终结判据见 §2.5)
├── ptContext.OnTimer(utcTime)                       PairTradingContext.cpp:490
│   ├── pim.RecalcVolumeParams(...)                  :495
│   ├── sg.RecalcOrderParams(*pi)                    :501
│   ├── ProcessRisk(*pi, nowUs)                      :509
│   │   └── rm.CheckRisk(pi, nowUs)                  RiskManager.cpp:231
│   │       ├── CheckTinyClose                       :73
│   │       ├── CheckADLRisk                         :87    ← §4.1
│   │       ├── CheckSpreadNoRegression              :130
│   │       └── CheckFundingAbnormal                 :190   ← §4.2
│   └── pim.SaveToCSV(...)                           :513
├── ScanFinishedAlgoOrders(utcTime)                  PairTradingStrategy.cpp:142  ← §5.1
└── algoContext.OnCommand("")                        :106   (dead，见 §5.8)

[外部指令]
PairTradingStrategy::on_command(json)                PairTradingStrategy.cpp:90
└── algoContext.OnCommand(json)                      AlgoContext.cpp:110
    └── 整个函数体被 /* */ 包住（112–859、865–1013）→ 空函数            ← §3.1

[停止]
PairTradingStrategy::pre_stop()                      PairTradingStrategy.cpp:72
└── SaveToCSV + WriteFileContent::Stop()，不撤任何单                    ← §3.4
```

---

## 二、P0：源头完全跑不起来

### 2.1 行情根本没订阅 → 源头没有输入

`etc/config.json`：

```json
"dbp": { "topics": [] },
"op": { "pairKeys": ["BINANCE.USDT_SWAP.BTC-USDT|GATEIO.USDT_SWAP.BTC-USDT", "..."] }
```

`PairTradingStrategy::pre_start()` 里唯一的订阅语句是注释掉的：

```cpp
if (op.HasMember("pairKeys")) {
    for (auto& pk: op["pairKeys"].GetArray()) {
        std::string pairKey = pk.GetString();
        m_ptCfg.pairKeys.emplace_back(pairKey);
        //dbpreader->Subscribe(pairKey);          // ← PairTradingStrategy.cpp:43
    }
}
```

`pt::PairTradingContext::Init()` 也只做 `pim.Init()` + `LoadFromCSV()`，不订阅。

**全工程唯一的 live 订阅点在 `AlgoContext::SubmitAlgoOrder`：**

```cpp
// AlgoContext.cpp:99
bool exist = SpreadManager::Instance().IsPairInstrumentKeyExist(pAlgoOrder->pairInstrumentKey);
if (!exist) {
    SpreadManager::Instance().AddSpreadPara(pAlgoOrder->pairInstrumentKey);
    QuantDbp::Instance().Subscribe(pAlgoOrder->pairInstrumentKey);
}
```

于是形成**循环依赖**：

```
要有行情 → 要 ProcessPairSignal → 要 SpreadManager 里有这个 pairKey
        → 要 QuantDbp::Subscribe → 要 AlgoContext::SubmitAlgoOrder
        → 要 PairTradingContext::SubmitAlgoOrder
        → 要 ProcessPairSignal / ProcessRisk（又回到起点）
```

结果：`on_dbpdata` 永远不会为这些 pairKey 触发，`UpdateRtSpread` 永远不执行，`pi.rtSpread.valid` 一直是未初始化的垃圾值。

> 另外，其余几处 `QuantDbp::Subscribe`（`AlgoContext.cpp:755-805`、`:1009`）都在被注释掉的代码块内，也是死的。

**修复方向（二选一）**

- 首选：在 `pre_start()` 里把 `dbpreader->Subscribe(pairKey)` 打开，让行情先于算法单到位（符合 `thread_split_design.md` 里「T_signal 只读快照」的设计意图，但订阅本身必须留在行情线程的安全点执行）。
- 或者：保留「建单时才订阅」，但必须额外提供一条不依赖行情的首次建单入口（例如恢复一个受控的 `OnCommand` 路径），否则冷启动无解。

---

### 2.2 统计量没有任何写入方 → `orderParams` 永远是全 0

```cpp
// SignalGenerator.cpp:21
void SignalGenerator::RecalcOrderParams(PairInfo& pi) const {
    auto& op = pi.orderParams;
    const auto& ls = pi.largeStats;

    if (!ls.IsValid()) {
        return;                    // ← 第 25-27 行：直接返回，什么都不算
    }
    ...
```

`largeStats.IsValid()` 要求 `valid && count > 0 && !std::isnan(bidBidUQ)`。而全工程 grep：

| 函数 | 定义位置 | 调用者 |
|---|---|---|
| `PairInfoManager::UpdateLargeStats` | `PairInfoManager.cpp:171` | **无** |
| `PairInfoManager::UpdateSmallStats` | `PairInfoManager.cpp:181` | **无** |
| `PairInfoManager::UpdateKlineStats` | `PairInfoManager.cpp:413` | **无** |

`CMakeLists.txt` 只编译 `src`、`src/strategy`、`src/tools`、`quant_library/{algo,basic,risk,signal}`，其中没有任何分位数/统计计算组件。也就是说：

- `pi.largeStats` / `pi.smallStats` 永远是默认值（`valid=false`, `count=0`）
- `RecalcOrderParams` 永远在第 27 行 return
- `pi.orderParams` 保持 `PairInfo.h:85-134` 的默认值：**8 个 Switch 全 false，16 个 Start/EndSpread 全 0.0**

`CheckSignal` 的 8 个分支全部以 `op.xxxSwitch &&` 开头，所以：

```cpp
// PairTradingContext.cpp:83-86
SignalResult sig = sg.CheckSignal(pi);
if (!sig.hasSignal) {
    return;                    // ← 永远从这里返回
}
```

**修复方向**：补上统计量生产者（或先用一个临时固定值把 `largeStats` 填上以便联调），并在 `UpdateLargeStats`/`UpdateSmallStats` 之后调用 `RecalcOrderParams`（`SignalGenerator.h:67` 的注释本来就是这么写的）。

---

### 2.3 `pi.autoFlag` 恒为 false → 开仓开关恒关

`PairInfo.h:225`：`bool autoFlag{false};`

`autoFlag` 被置 `true` 的唯一位置：

```cpp
// PairInfoManager.cpp:465
void PairInfoManager::ApplyCommand(const std::string& pairKey, PairCommandType cmd, ...) {
    switch (cmd) {
        ...
        case PairCmd_RESUME:
            pi->stopFlag = false;
            pi->closeFlag = false;
            pi->autoFlag = true;        // ← :482
            break;
```

而 **`ApplyCommand` 在整个工程里没有任何调用者**（`ResetAbnormalCloseState` 同样如此）。`PairTradingStrategy::on_command` 只往 `algoContext.OnCommand` 转发，而后者是空函数（§3.1）。

连锁反应：

```cpp
// SignalGenerator.cpp:127-140
op.ttOLSwitch = canOpenLong && pi.autoFlag;      // 恒 false
op.ttOSSwitch = canOpenShort && pi.autoFlag;     // 恒 false
op.mtOLSwitch = canOpenLong && pi.autoFlag;      // 恒 false
op.mtOSSwitch = canOpenShort && pi.autoFlag;     // 恒 false

op.ttCLSwitch = op.ttCSSwitch = true;
op.mtCLSwitch = op.mtCSSwitch = true;

if (!pi.autoFlag) {
    op.ttOLSwitch = op.ttOSSwitch = false;
    op.ttCLSwitch = op.ttCSSwitch = false;       // ← TT 平仓也被关掉
}
```

1. **开仓信号永远不可能产生**（4 个开仓开关恒 false）。
2. 即便有信号，`BuildAlgoOrderJson` 也会拒绝：
   ```cpp
   // PairTradingContext.cpp:220-225
   const bool sw = *pSw;
   if (!sw && !isClose && forgoProfit == 0.0) {
       return nullptr;                 // ← 开仓恒走这里
   }
   ```
3. **TT 平仓被连带关掉**，只剩 MT 平仓可用。
4. 风控强平的 `mode` 恒为 `"MT"`：
   ```cpp
   // PairTradingContext.cpp:154
   std::string mode = pi.autoFlag ? "TT" : "MT";   // 恒 "MT"
   ```

**修复方向**：明确 `autoFlag` 的语义与来源。若它是「策略启动后自动交易」的开关，应在 `PairInfoManager::Init` 或配置里置位；若它必须由人工 `RESUME` 指令打开，则必须先把 `OnCommand`/`ApplyCommand` 这条指令链路接通。

---

### 2.4 `targetVolume` 恒为 0 → `BuildAlgoOrderJson` 直接返回 nullptr

```cpp
// PairTradingContext.cpp:228-232
double targetVolume = isTT ? pi.ttTargetVolume : pi.mtTargetVolume;
if (std::isnan(targetVolume) || targetVolume <= 0.0) {
    LOG_WARN("invalid targetVolume:{} ...");
    return nullptr;
}
```

`ttTargetVolume` / `mtTargetVolume` 由 `PairInfoManager::RecalcVolumeParams` 计算，但该函数第一件事就是：

```cpp
// PairInfoManager.cpp:361-366
double ap = pi.activeMeanClose;
double pp = pi.passiveMeanClose;

if (std::isnan(ap) || ap <= 0 || std::isnan(pp) || pp <= 0) {
    continue;                       // ← 永远 continue
}
```

`activeMeanClose` 只能由 `UpdateKlineStats` 写入 —— 而它没有调用者（§2.2）。`PairInfo.h:212` 默认 `0.0`，于是 `RecalcVolumeParams` 对每个 pairKey 都 `continue`，`maxVolume / minVolume / ttTargetVolume / mtTargetVolume` 全部保持 0。

**修复方向**：同 §2.2，需要 K 线统计的写入方；同时 `activeMeanClose` 缺失时应当显式报错而不是静默 `continue`。

---

### 2.5 算法单永不终结 → `hasActiveAlgoOrder` 永久占位，堵死后续所有开平仓

这是最隐蔽也最致命的一条。

算法单的终结判据在 `AlgoContext::OnTimer`：

```cpp
// AlgoContext.cpp:1977
if (it->second->ttOLSwitch == false && it->second->ttOSSwitch == false &&
    it->second->mtOLSwitch == false && it->second->mtOSSwitch == false) {

    orderAmount = fabs(it->second->pairTotalVolume);            // :2007
    if (orderAmount < it->second->activeInfo.minSize && allPairOrders.size() == 0) {
        it->second->algoOrderStatus = stra::ALGO_OS_FILLED;     // :2011
        ...
    }
}
```

问题在于 `it->second->pairTotalVolume` 的语义。看 `BuildAlgoOrderJson` 的预置：

```cpp
// PairTradingContext.cpp:305
pAlgoOrder->pairTotalVolume = pi.pairTotalVolume;      // ← 用"当前持仓量"预置
```

再看 `BaseAlgoOrder::UpdateAlgoPairOrderByPairOrder` 的累加（`BaseAlgoOrder.cpp:239-297`，例如 `pairTotalVolume += activeVolume;`）。

所以 `order->pairTotalVolume` 是「**开单前的持仓量 + 本算法单已成交的增量**」，即**累计持仓量**，而不是「**剩余未成交量**」。

于是 `orderAmount < activeInfo.minSize` 这个判据实际在问「累计持仓量是否小于最小下单量」：

| 场景 | `order->pairTotalVolume` | `minSize` | 是否 FILLED |
|---|---|---|---|
| 开仓单，成交 0 | 0 | 0.001 | ✅ 会 FILLED |
| 开仓单，成交 50 | 50 | 0.001 | ❌ 不会 |
| 平仓单（预置 -50），成交 0 | -50 | 0.001 | ❌ 不会 |

**结论：成交越多，越不可能进入终结状态。** 一个正常成交的算法单会一直停在 `ALGO_OS_NEW`。

而 `ALGO_OS_FILLED` 是 `algoOrderStatus` 仅有的两个正常出口之一（另一个是 `ALGO_OS_ERRORCANCELED`，见 §3.5）。全工程 `algoOrderStatus =` 的赋值点：

| 状态 | 位置 | 可达性 |
|---|---|---|
| `ALGO_OS_NEW` | `PairTradingContext.cpp:244`, `AlgoContext.cpp:86` | ✅ |
| `ALGO_OS_PARTFILLED` | **无任何赋值** | ❌ 死枚举 |
| `ALGO_OS_CANCELLING` | `AlgoContext.cpp:288` | ❌ 在注释块内 |
| `ALGO_OS_CANCELED` | `AlgoContext.cpp:2078` | ❌ 前置是 CANCELLING |
| `ALGO_OS_FILLED` | `AlgoContext.cpp:1993/2011` | ❌ 判据错误（本节） |
| `ALGO_OS_ERRORCANCELLING` | `AlgoContext.cpp:1904` | ⚠️ 见 §3.5 |
| `ALGO_OS_ERRORCANCELED` | `AlgoContext.cpp:2085` | ⚠️ 见 §3.5 |

`hasActiveAlgoOrder` 的清零路径只有一条：

```
ScanFinishedAlgoOrders (PairTradingStrategy.cpp:160 判定 terminal)
  → ptContext.OnAlgoOrderUpdate (PairTradingContext.cpp:460)
    → pim.ClearActiveAlgoOrder (PairTradingContext.cpp:467)
```

`OnAlgoOrderUpdate` 只有 `!order` 或 `order->algoOrderStatus ∈ {FILLED, CANCELED, ERRORCANCELED}` 时才会被调用。既然 FILLED 到不了、CANCELED 是死分支，**`hasActiveAlgoOrder` 一旦置 true 就再也回不到 false**。

后果被这两处 `return` 放大：

```cpp
// PairTradingContext.cpp:72    ProcessPairSignal 开头
if (pi.hasActiveAlgoOrder) { return; }      // 该对子再也不能开仓/平仓

// PairTradingContext.cpp:139   ProcessRisk 开头
if (pi.hasActiveAlgoOrder) { return; }      // 该对子再也不能风控强平
```

**即：第一个算法单发出去之后，这个对子就永久失效了。** 这正好解释了「测试算法单能开能平，但走不到持续运行」的现象。

**修复方向**：终结判据应当基于「本算法单的目标是否已达成」，而不是拿累计持仓量比 `minSize`。可选方案：

- 方案 A（最小改动）：比较「目标量」与「累计量」的差额。例如引入 `remainingVolume = |目标成交量| - |本单累计成交量|`，用 `remainingVolume < minSize` 作判据；
- 方案 B：由 `BaseAlgoOrder` 在 `PairOrderTrade`/`UpdateAlgoPairOrderByPairOrder` 中，当本单所有 pairOrder 都完成且累计量达到目标时，自行置 `ALGO_OS_FILLED`；
- 无论哪种，都应补一条**算法单级超时兜底**（见 §3.3），保证任何异常路径下 `hasActiveAlgoOrder` 都能被释放。

---

## 三、P1：撤算法单链路

### 3.1 `AlgoContext::OnCommand` 是个空函数

```cpp
// AlgoContext.cpp:110
void AlgoContext::OnCommand(string s) {
    // rLarkMsg.Push(s);
    /*
    ... 112–859 行（含 286-294 的 CANCEL 分支） ...
    */
    // 原 JSON 建单路径已由 AlgoContext::SubmitAlgoOrder(BaseAlgoOrder*) 取代。
    /*
    ... 865–1013 行（DOGE 硬编码测试单）...
    */
}                                  // ← 函数体里没有任何可执行语句
```

（用注释块扫描确认：`/*` 开于 112 行、闭于 859 行；第二个 `/*` 开于 865 行、闭于 1013 行。）

后果：

- `PairTradingStrategy::on_command(json)` → 静默丢弃。**NEW / CANCEL / MODIFY / QUERY 四类外部指令全部失效**；
- 唯一的 `ALGO_OS_CANCELLING` 赋值点在注释块内：
  ```cpp
  // AlgoContext.cpp:286（在注释块中）
  if (commandType == stra::CommandType_CANCEL) {
      pAlgoOrder->commandType = stra::CommandType_UCANCELLING;
      pAlgoOrder->algoOrderStatus = stra::ALGO_OS_CANCELLING;
      pAlgoOrder->cancelOrderTime = crypto::getCurrentTime();
      ...
  }
  ```
- 注释里也留了说明：「原 JSON 建单路径已由 `AlgoContext::SubmitAlgoOrder(BaseAlgoOrder*)` 取代」——即建单确实迁移完了，但**撤单/改单/查询还没有迁移**，属于迁移半成品。

**修复方向**：把 CANCEL / MODIFY / QUERY 三条分支从注释块里摘出来重新实现（建单部分可以保持由 `SubmitAlgoOrder(BaseAlgoOrder*)` 承担），这是恢复「撤算法单」能力的**唯一前提**。

---

### 3.2 策略层与风控层没有任何撤算法单的出口

在 `OnCommand` 失效的前提下：

- `PairTradingContext::ProcessRisk` 只做「**再发一张强平单**」，从不撤销在途算法单；
- `PairTradingContext::ProcessPairSignal` 遇到 `hasActiveAlgoOrder` 直接 return，也不会撤销；
- `BaseAlgoOrder::CancelOrderOnSpread` 的 CANCELLING 分支：
  ```cpp
  // BaseAlgoOrder.cpp:406
  if (algoOrderStatus == stra::ALGO_OS_CANCELLING) {   // ← 永不成立
      ... 撤销全部活动子单 ...
  } else {
      ... 逐子单的时间/价格/大单撤单 ...               // ← 只有这条是活的
  }
  ```
- `AlgoContext::OnTimer` 的 `ALGO_OS_CANCELLING → ALGO_OS_CANCELED`（2076–2083）也是死的。

**所以「撤算法单」目前只存在于死代码里。** 需要产品侧明确：风控触发时到底是「先撤掉在途算法单再发强平单」还是「等算法单自然结束再发强平单」——当前代码两者都没做，只是干等（而且因为 §2.5 会一直等下去）。

---

### 3.3 没有算法单级超时

`PairTradingConfig` 里声明了：

```cpp
// PairTradingContext.h:37
int64_t algoOrderTimeoutMs{30000};
```

grep 全工程，**除声明外没有任何引用**。`AlgoContext::OnTimer` 里唯一带时间的是：

```cpp
// AlgoContext.cpp:1879
if (eventTime - lastAlgoUpdateTime > 10 * 60 * second1) {
    it->second->Update();
}
```

这是「10 分钟全局刷新」，不是超时终结。配合 §2.5，一旦算法单卡住就永久卡住。

**修复方向**：在 `AlgoContext::OnTimer` 里补一条基于 `insertTime`/`updateTime` 的兜底：超时后撤掉全部子单并置 `ALGO_OS_ERRORCANCELED`（或 CANCELED），确保 `hasActiveAlgoOrder` 一定会被释放。

---

### 3.4 `pre_stop()` 不撤单

```cpp
// PairTradingStrategy.cpp:72
void PairTradingStrategy::pre_stop() {
    if (!m_ptCfg.csvStatePath.empty()) {
        pt::PairInfoManager::Instance().SaveToCSV(m_ptCfg.csvStatePath);
    }
    WriteFileContent::GetInstance().Stop();
    BaseStrategy::pre_stop();
}
```

进程退出/策略停止时，交易所侧的挂单不会被撤销。结合 §5.2（持仓不落盘恢复），重启后会出现「内部无持仓 + 交易所实际有持仓/挂单」的错位。

---

### 3.5 `ALGO_OS_ERRORCANCELLING` 只改状态、不撤子单，且会永久停留

```cpp
// AlgoContext.cpp:1898
if (eventTime - pdata->generateTs > 30 * second1 && mSpreadReportCount[it->second->pairInstrumentKey] > 60) {
    ...
    it->second->commandType = stra::CommandType_ERROR;
    it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELLING;   // ← 只改状态
    it->second->updateTime = eventTime;
}
```

落地条件却是：

```cpp
// AlgoContext.cpp:2084
} else if (allPairOrders.size() == 0 && it->second->algoOrderStatus == stra::ALGO_OS_ERRORCANCELLING) {
    it->second->algoOrderStatus = stra::ALGO_OS_ERRORCANCELED;
```

**矛盾点**：这段代码在「行情断了 30 秒」时触发，但既不撤子单、也不停 `OnSpread` 的报单逻辑，却要求 `allPairOrders.size() == 0` 才肯终结。只要还有在途子单，算法单就永远停在 `ALGO_OS_ERRORCANCELLING`。

更严重的是，`OnSpread` 的跳过列表**不包含** `ERRORCANCELLING`：

```cpp
// AlgoContext.cpp:1070
if (pAlgoOrder->algoOrderStatus == stra::ALGO_OS_FILLED ||
    pAlgoOrder->algoOrderStatus == stra::ALGO_OS_CANCELED ||
    pAlgoOrder->algoOrderStatus == stra::ALGO_OS_ERRORCANCELED) {
    continue;
}
pAlgoOrder->CancelOrderOnSpread(pdata);
if (pAlgoOrder->algoOrderStatus != stra::ALGO_OS_NEW &&
    pAlgoOrder->algoOrderStatus != stra::ALGO_OS_PARTFILLED) {
    continue;                      // ← ERRORCANCELLING 会从这里被挡住，不再报新单
}
```

第 1084 行恰好把 `ERRORCANCELLING` 挡在报单之外，所以不会继续报新单——但**已有的子单依然不会被撤**，因为 `CancelOrderOnSpread` 走的是 else 分支（普通撤单逻辑，依赖行情），而行情恰好已经断了。这是个典型的「行情断了要撤单，但撤单逻辑依赖行情」死锁。

**修复方向**：在设置 `ERRORCANCELLING` 的同时主动撤销全部子单（复用 `CancelOrderOnSpread` 的 CANCELLING 分支即可——那条分支正好不依赖行情，只按时间撤），或者让 `ERRORCANCELLING` 状态直接走无行情的撤单路径。

---

## 四、P1：风控强平链路

链路本身是通的（`OnTimer` 每 tick 都跑 `ProcessRisk`），但有 5 处判据问题。

### 4.1 ADL 的「持续 4 天」前置条件失效（赋值当比较）

```cpp
// RiskManager.cpp:94
double posVal = pi.CalcPositionValue();
if (std::isnan(posVal) || posVal < m_cfg.adlPositionThresholdUsdt) {
    pi.positionExceedThresholdStartTime = 0;
    return r;
}

if (pi.positionExceedThresholdStartTime = 0) {          // ← :99 赋值，不是比较
    pi.positionExceedThresholdStartTime = nowUs;        //    永不执行
    return r;
}

int64_t holdDuration = nowUs - pi.positionExceedThresholdStartTime;   // = nowUs - 0
if (holdDuration < m_cfg.positionExceedDuration) {                    // 恒不成立
    return r;
}
```

因为 `positionExceedThresholdStartTime` 永远保持 0，`holdDuration = nowUs - 0` 是一个天文数字（微秒级 epoch），**恒大于 `positionExceedDuration`（4 天）**。

实际效果与设计意图**相反**：ADL 强平不再需要「持仓超阈值持续 4 天」，只要 `posVal >= 2000 USDT` 且 `adlRank >= 0.8` 就立刻强平。

**修复**：

```cpp
if (pi.positionExceedThresholdStartTime == 0) {
    pi.positionExceedThresholdStartTime = nowUs;
    return r;
}
```

（注意 §5.6：`CalcPositionValue()` 依赖 `activeMeanClose`，目前恒返回 0，所以这条路径暂时不会真的触发；修好 `activeMeanClose` 之后这个 bug 会立刻变成「过早强平」。）

### 4.2 funding 异常：多空算出同一个值

```cpp
// RiskManager.cpp:201
double netFundingCost = 0.0;
if (pi.IsLong()) {
    netFundingCost = aFR - (-pFR);      // = aFR + pFR
} else if (pi.IsShort()) {
    netFundingCost = (-aFR) - pFR;      // = -aFR - pFR
    netFundingCost = -netFundingCost;   // = aFR + pFR   ← 双重取负，抵消了
}
```

两个分支的结果完全相同（都是 `aFR + pFR`），空头方向的符号从未翻转。也就是说**空头持仓的资金费率成本是按多头口径算的**，该触发时不触发、不该触发时可能触发。

需要业务侧确认约定（`activeFundingRate` / `passiveFundingRate` 的正负号与多空腿的收付关系），然后重写这一段——建议改成显式表达式并加一行单测，例如 `netFundingCost = pi.IsLong() ? (aFR - pFR) : (pFR - aFR);`（**仅为示意，符号需按实际约定核对**）。

### 4.3 funding 阈值量纲可疑

```cpp
// RiskManager.cpp:211
double threshold = m_cfg.fundingAbnormalThreshold * m_cfg.fundingMaxAmount;   // 0.005 * 30.0 = 0.15
double fundingLoss = netFundingCost * posVal;
if (fundingLoss < threshold) { return r; }
```

`netFundingCost` 是费率差（量纲 1），`posVal` 是 USDT。门槛 0.15 USDT 相对于 `adlPositionThresholdUsdt = 2000` 的持仓门槛来说极小——费率差只要有 0.000075 就会越过。请确认这是否是有意为之；如果是「单期资金费用的绝对上限」，0.15 USDT 显然偏小。

### 4.4 档位升到 tier3 之后彻底放弃

```cpp
// RiskManager.cpp:6
std::pair<int, bool> RiskManager::GetCurrentTier(const AbnormalCloseState& state, int64_t nowUs) const {
    if (!state.triggered) return {0, false};
    int tier = state.currentTier;
    int64_t elapsed = nowUs - state.startTime;

    if (tier == 1 && elapsed > m_cfg.tier1WaitUs && state.tier1Times > 0) return {2, true};
    if (tier == 2 && elapsed > m_cfg.tier1WaitUs + m_cfg.tier2WaitUs && state.tier2Times > 0) return {3, true};

    if (tier == 1 && state.tier1Times == 0) return {1, true};
    if (tier == 2 && state.tier2Times == 0) return {2, true};
    if (tier == 3 && state.tier3Times == 0) return {3, true};

    return {tier, false};       // ← tier==3 且 tier3Times>0 时落在这里，永远 false
}
```

一旦 tier3 尝试过一次（`tier3Times` 变 1），之后 `GetCurrentTier` 恒返回 `{3, false}`，`needForceClose` 恒 false，**强平彻底停止**，直到持仓归零才通过 `OnAlgoFinished(fullyFlat=true)` 复位。如果 tier3 也没成交，就永久卡住。

请确认这是「最多尝试 3 次就交给人工」的设计，还是漏了「持续以 tier3 重试」的兜底。

### 4.5 零成交强平不推进档位（潜在发单风暴）

```cpp
// PairTradingContext.cpp:469
if (isFinished && std::abs(volumeFilled) > 1e-9) {
    pim.UpdateOnAlgoOrderFinished(...);
    ...
    RiskManager::Instance().OnAlgoFinished(*pi, isFullyFlat);     // ← 也被这个门槛挡住
    SignalGenerator::Instance().RecalcOrderParams(*pi);
}
```

`volumeFilled == 0` 时（例如挂单没成交就被时间撤掉），`OnAlgoFinished` 不会执行 → `tier1Times` 保持 0 → `GetCurrentTier` 继续返回 `{1, true}` → 算法单一旦终结，下一个 tick 立刻再发一张 tier1 强平单。

而 §2.5 的 bug 恰好让算法单**不会终结**，所以目前表现为「卡死」而不是「风暴」；但修好 §2.5 之后，这里会立刻变成**发单风暴**。两处必须一起修。

---

## 五、P2：其余问题

### 5.1 `ScanFinishedAlgoOrders` 的死三元表达式

```cpp
// PairTradingStrategy.cpp:153
if (!order) {
    double volFilled = order ? order->pairTotalVolume - pi->pairTotalVolume : 0.0;   // 恒 0.0
    bool fullyFlat = !pi->HasPosition();
    ptContext.OnAlgoOrderUpdate(pi->pairInstrumentKey, pi->currentAlgoOrderId, volFilled, 0.0, 0.0, true, fullyFlat);
}
```

`order` 恒为 null，三元恒取 `0.0`。而 `OnAlgoOrderUpdate` 的 body 又要求 `std::abs(volumeFilled) > 1e-9`，于是这个分支**只做了一件事**：`ClearActiveAlgoOrder`。

也就是说，当算法单对象在策略看到终结状态之前就被 `AlgoContext` 删掉时，`pi->pairTotalVolume` / 持仓均价 / 风控档位**全部不会更新**，内部持仓与真实持仓静默背离。建议至少补一条 `LOG_WARN`，并考虑用 `pi->activeRealPosition`/`passiveRealPosition` 做一次对账。

### 5.2 `PairInfoManager::LoadFromCSV` 是个坏桩

```cpp
// PairInfoManager.cpp:85
bool PairInfoManager::LoadFromCSV(const std::string& csvPath) {
    std::ifstream ifs(csvPath);
    if (!ifs) { LOG_WARN(""); return false; }

    std::string line;
    std::getline(ifs, line);
    int loaded = 0;

    while (std::getline(ifs, line)) {
        std::string pk = "";                       // ← :97 硬编码空串，没解析 line
        auto it = m_pairInfoMap.find(pk);          // ← 恒 == end()
        if (it == m_pairInfoMap.end()) { continue; }   // ← 每行都 continue
        PairInfo& p = it->second;
        p.pairTotalVolume = 0;                     // ← 就算进来了也是清零
    }
}                                                  // ← :107 缺 return，UB
```

三个问题：

1. 空键 → 永远查不到 → 全表 `continue`，一行都没加载；
2. `p.pairTotalVolume = 0` 语义反了（加载却清零）；
3. **`bool` 函数缺 `return`**，走到函数尾是 UB（`loaded` 也未使用）。

叠加 `etc/config.json` 里 `"csvStatePath": "./csv"` 是**目录**，`std::ofstream ofs("./csv")` 必然失败 → `SaveToCSV` 直接 `LOG_ERROR` 返回 false。**持仓状态既不存也不取。**

`PairTradingContext::Init` 的判断也反了：

```cpp
// PairTradingContext.cpp:48
if (pim.LoadFromCSV(cfg.csvStatePath)) {
    LOG_WARN("");          // ← 成功反而打 WARN
}
```

### 5.3 `PairInfo` 存在未初始化字段

```cpp
// PairInfo.h:202
double openSmallSpreadBidBidUQ;      // 无初值 → 不确定值
double openSmallSpreadAskAskDQ;      // 无初值 → 不确定值
```

```cpp
// PairInfo.h:59  struct RealTimeSpread
double spreadBidAsk;    // 全部无初值
double spreadBidBid;
...
bool valid;             // ← 连 valid 都没有初值
```

`PairInfo pi;`（`PairInfoManager.cpp:12`）是默认初始化，这些成员拿到的是**栈上残留值**。而使用处只用 `isnan` 兜底：

```cpp
// RiskManager.cpp:145
if (!std::isnan(pi.openSmallSpreadAskAskDQ)) {
    isRegressed = pi.rtSpread.spreadAskAsk > pi.openSmallSpreadAskAskDQ;
}
```

`isnan()` 拦不住垃圾值（一个非 NaN 的随机 double 会被当成真实的「开仓时分位数」）。同理 `CheckSignal` 第一行 `if (!pi.rtSpread.valid) return;` 也可能因为 `valid` 是随机真值而放行。

**修复**：给 `openSmallSpread*` 显式初始化为 `NAN`（与 `OnAlgoFinished` 里的复位值保持一致），给 `RealTimeSpread` 全部成员加初值并把 `valid` 置 `false`。

### 5.4 `ResetAbnormalCloseState` 空指针解引用

```cpp
// PairInfoManager.cpp:458
if (!st) {
    *st = AbnormalCloseState();      // ← 对空指针解引用
}
```

判断写反了，应为 `if (st)`。另外若 `type` 不是那三个枚举之一，`st` 就是 nullptr。目前该函数无调用者，是个定时炸弹。

### 5.5 `UpdateOnPosition` 不同步 `pairTotalVolume`

`UpdateOnPosition`（`PairInfoManager.cpp:192`）只写 `activeRealPosition` / `passiveRealPosition` / `avgPrice` / `adlRank` / `liquidPrice` 等，**从不回写 `pairTotalVolume`**。

而 `pairTotalVolume` 只在 `UpdateOnAlgoOrderFinished`（`:314`）里 `+= volumeFilled`。所有风控门槛（`ProcessRisk` 的 `pi.HasPosition()`、`CheckTinyClose`、`CheckADLRisk`、`CheckFundingAbnormal`）都建立在这个**内部计数器**上。

一旦出现「算法单在策略看到终结前被删」（§5.1）、进程重启（§5.2）、或交易所侧手工平仓，内部计数就与真实持仓永久背离，风控会基于错误的持仓做决策。建议加一个定期对账：用 `activeRealPosition`/`passiveRealPosition` 校正 `pairTotalVolume`。

### 5.6 `RecalcOrderParams` 的调用时机与注释不符

`SignalGenerator.h:67` 写明应在「每次 `largeStats`/`smallStats` 更新后调用」，实际只有两处调用（`PairTradingContext.cpp:486` 算法单结束后、`:503` 每 `signalRecalcIntervalSec = 300s`）。

由于 `m_lastSignalRecalcUs` 初值为 0，第一个 `OnTimer` tick 就会触发一次（`nowUs - 0` 远大于 300s），所以启动时不会用陈旧参数。但 §2.2 的统计量生产者一旦补上，必须同步在这里加上「统计更新后重算」，否则参数最长会陈旧 5 分钟。

### 5.7 `on_timer` 里的 `OnCommand("")` 是死代码

```cpp
// PairTradingStrategy.cpp:104
if (utcTime - lastOnCommand > 10000000LL) {
    if (!createAlgo) {
        algoContext.OnCommand("");       // OnCommand 是空函数
        lastOnCommand = utcTime;
        createAlgo = true;
    }
}
```

`createAlgo` 只在这里被置位，所以最多执行一次，且因为 `OnCommand` 是空函数而毫无作用。属于旧路径的残留，建议连同 `createAlgo` / `lastOnCommand` 一起清掉。

---

## 六、建议的修复顺序

按「先让链路能跑起来，再修正确性」排序：

**第一阶段 — 打通闭环（缺一不可）**

1. §2.1 让行情订阅在算法单之前发生（打开 `PairTradingStrategy.cpp:43`，或在行情线程安全点统一订阅 `pairKeys`）；
2. §2.2 / §2.4 补上 `largeStats` / `smallStats` / `activeMeanClose` 的写入方，让 `orderParams` 与 `targetVolume` 真正被算出来；
3. §2.3 明确并接通 `autoFlag` 的来源；
4. §2.5 修正算法单终结判据（建议改为「剩余量 < minSize」或由 `BaseAlgoOrder` 自行判定完成）；
5. §3.3 补算法单级超时兜底，保证 `hasActiveAlgoOrder` 在任何异常下都能释放。

这五条做完，「行情 → 建单 → 成交 → 收尾 → 可再次建单」的循环才第一次真正闭合。建议先只开一个 pairKey、把 `maxAmount`/`targetAmount` 调到最小，跑通一次完整的「开仓 → 平仓」再扩量。

**第二阶段 — 恢复撤单能力**

6. §3.1 把 CANCEL / MODIFY / QUERY 从注释块里摘出来重新实现；
7. §3.2 明确风控触发时「撤在途算法单」与「发强平单」的先后关系并落地；
8. §3.5 让 `ERRORCANCELLING` 同时撤销子单，避免「行情断了撤不掉单」的死锁；
9. §3.4 在 `pre_stop()` 里撤销在途算法单。

**第三阶段 — 风控正确性**

10. §4.1 ADL 的 `=` → `==`；
11. §4.2 funding 多空符号；
12. §4.3 funding 阈值量纲确认；
13. §4.4 tier3 之后的重试策略确认；
14. §4.5 与 §2.5 一起修（零成交也要推进档位），否则会引入发单风暴。

**第四阶段 — 健壮性**

15. §5.2 `LoadFromCSV` 重写 + `csvStatePath` 改成文件路径 + 补 `return`；
16. §5.3 `PairInfo` 未初始化字段补初值（`openSmallSpread*` → `NAN`，`RealTimeSpread` 全字段初值 + `valid{false}`）；
17. §5.5 增加 `pairTotalVolume` 与真实持仓的对账；
18. §5.1 / §5.4 / §5.6 / §5.7 清理。

---

## 附：本次审查覆盖的文件

| 文件 | 关注点 |
|---|---|
| `src/strategy/PairTradingStrategy.cpp` / `include/strategy/PairTradingStrategy.h` | 入口、订阅、扫描、定时器 |
| `quant_library/algo/PairTradingContext.cpp` / `.h` | 信号处理、风控处理、建单、终结回调 |
| `quant_library/signal/SignalGenerator.cpp` / `.h` | 参数重算、信号判定、开平仓准入 |
| `quant_library/risk/RiskManager.cpp` / `.h` | 三类风险判定、档位升级 |
| `quant_library/algo/AlgoContext.cpp` / `.h` | 注册、行情处理、撤单、定时终结 |
| `quant_library/basic/BaseAlgoOrder.cpp` | `CancelOrderOnSpread`、持仓累计 |
| `quant_library/basic/PairInfoManager.cpp` / `PairInfo.h` | 状态管理、CSV、统计、指令 |
| `CMakeLists.txt`、`etc/config.json`、`etc/strategy.ini` | 编译范围与运行配置 |
