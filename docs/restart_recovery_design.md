# 挂掉重启恢复方案（设计稿）

设计对象：`utrade_hft` 策略层在**进程崩溃 / 被 kill / 正常重启**后，如何恢复运行状态。

涉及文件：`src/strategy/PairTradingStrategy.cpp`、`quant_library/algo/PairTradingContext.{h,cpp}`、`quant_library/algo/AlgoContext.{h,cpp}`、`quant_library/basic/PairInfoManager.{h,cpp}`、`quant_library/basic/PairInfo.h`、`quant_library/basic/BaseAlgoOrder.{h,cpp}`、`etc/config.json`。

祖先参照：`pair_trading_c_gateio/cc_pricespread_gb_ltp.py:393-397`、`utility/pair_info_manager.py:282-341`。

**本稿只做设计，未修改任何源码。**

---

## 〇、结论速览

| 问题 | 结论 |
|---|---|
| 现在能把 pairinfo 存到 CSV 吗？ | ❌ **不能**。`etc/config.json:25` 的 `csvStatePath` 指向一个**目录**，`SaveToCSV` 打开必然失败；即便修好路径，`LoadFromCSV` 也是个坏桩，一行都读不回来 |
| 重启后策略知道自己的持仓吗？ | ❌ **不知道**。`pairTotalVolume` 归零 → `HasPosition()` 恒 false → 平仓、四条风控、建仓基准全部失效；同时开仓侧**没有任何持仓门槛**，会在孤儿持仓之上再开一笔 |
| 该"全撤单"还是"接着做"？ | ⚠️ **两个都做不了**。`TradeClient` 只有 `query_account` / `add_new_order` / `cancel_order` / `query_order`，**没有"列在途单"、没有"全撤"**；算法单侧 `SaveToFile` / `LoadFromFile` 是空桩 |
| 推荐哪条路？ | ✅ **第三条：接续持仓、放弃算法单** —— 只恢复持仓账本与风控状态，算法单本来就不落盘。与祖先一致（`load_pair_info` 只恢复持仓列），工作量最小，且是另外两条的必经前置 |
| 交易所侧的旧子单怎么办？ | 今天撤不掉。短期只能**告警 + 人工确认**；要真撤，得先把在途单的 `strategyOrderId` 做成可读快照 |

一句话：**先把"持仓账本"这一件事做对（写快照 + 启动对账 + 冻结闸门），再谈撤单。**

> **已落地的两处源码改动**（本方案的前置，不改变上面的结论）：
> 1. `UpdateOnPosition:193` 的 key 分隔符 `","` → `"."`（`Position` 路从"整条死"恢复生效，§5.3.4）；
> 2. `ApplyCommand` 的 `PairCmd_RESUME` 一并清 `errorFlag`（补上原本不存在的清除路径，§5.5）。
>
> 两者都**未在部署机上编译验证**（本机缺 `cmake` / `fmt`）。

---

## 一、现状：已有的 pairinfo → CSV 接口是断的

### 1.1 写入侧

`PairInfoManager::SaveToCSV`（`PairInfoManager.cpp:65-83`）只写 **5 列**：

```
pair_instrument_key,pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,pairPassiveTotalVolume,
```

调用点两处：

| 位置 | 时机 |
|---|---|
| `PairTradingStrategy::pre_stop`（`PairTradingStrategy.cpp:80-82`） | 优雅停机 |
| `PairTradingContext::OnTimer`（`PairTradingContext.cpp:1031-1036`） | 每 `csvSaveIntervalSec`，默认 **300s**（`PairTradingContext.h:66`） |

### 1.2 读取侧

`PairInfoManager::LoadFromCSV`（`PairInfoManager.cpp:85-107`）是个**坏桩**：

```cpp
// PairInfoManager.cpp:85
bool PairInfoManager::LoadFromCSV(const std::string& csvPath) {
    std::ifstream ifs(csvPath);
    if (!ifs) { LOG_WARN(""); return false; }

    std::string line;
    std::getline(ifs, line);          // 吃掉表头
    int loaded = 0;                   // 未使用

    while (std::getline(ifs, line)) {
        std::string pk = "";          // :97 硬编码空串，根本没解析 line
        auto it = m_pairInfoMap.find(pk);   // 恒 == end()
        if (it == m_pairInfoMap.end()) { continue; }   // 每行都 continue
        PairInfo& p = it->second;
        p.pairTotalVolume = 0;        // :105 就算进来了也是清零，语义反了
    }
}                                     // :107 缺 return，bool 函数走到这里是 UB
```

三个问题：空键导致**一行都没加载**；`pairTotalVolume = 0` 语义相反；**`bool` 函数缺 `return`**。

`PairTradingContext::Init` 的判断也是反的（`PairTradingContext.cpp:81-85`）：

```cpp
if (!cfg.csvStatePath.empty()) {
    if (pim.LoadFromCSV(cfg.csvStatePath)) {
        LOG_WARN("");                 // 成功反而打 WARN，失败反而静默
    }
}
```

### 1.3 配置

`etc/config.json:25`：

```json
"csvStatePath": "./csv"
```

`./csv` 是个**目录**，`std::ofstream ofs("./csv")` 必然失败 → `SaveToCSV` 直接 `LOG_ERROR` 返回 false。代码内的默认值是 `"data/pair_info.csv"`（`PairTradingContext.h:63`），配置把它覆盖掉了。

**结论：持仓状态今天既不存也不取。**

### 1.4 算法单侧

- `BaseAlgoOrder::LoadFromFile` / `SaveToFile`（`BaseAlgoOrder.cpp:880-886`）是**空桩**（函数体为空）。
- `AlgoContext.cpp:1914` 里 `// pAlgoOrder->SaveToFile();` 被注释掉了。
- `AlgoContext.cpp:2360-2363` 留了一段"持久化"的注释设计：

  ```cpp
  // 持久化
  // algoOrder在每次创建和终结一个pairOrder时需要持久化
  // pairOrder在被创建和终结时需要持久化
  // quantOrder在被创建和终结时需要持久化
  ```

  这是**设计意图，未实现**。
- `AlgoContext::SubmitAlgoOrder(int64_t, cmd, modify)`（`AlgoContext.cpp:115-119`）按 id 在 `alogOrderManager` 里查单，查不到就 `LOG_ERROR` 直接 return。→ **重启后拿旧 `currentAlgoOrderId` 去撤单是个 no-op。**

### 1.5 祖先是怎么做的

祖先**确实有**重启恢复，路径是 `cc_pricespread_gb_ltp.py:393-397`：

```python
if self.continue_flag:  # 如果选择继续则会同步
    try:
        load_pair_info(self, self.reset_volume_flag)
    except:
        pass
```

`load_pair_info`（`utility/pair_info_manager.py:282-313`）的行为：

1. 定义一份**白名单列**：`status, pairTotalVolume, pairActiveTotalPrice, pairPassiveTotalPrice, float_pnl, total_pnl, active_liquid_status, passive_liquid_status, active_margin_status, passive_margin_status, limit_flag, error_flag, error_json, ttFloatPNL, ttTotalPNL, mtFloatPNL, mtTotalPNL, close_flag, stop_flag`；
2. `if not reset_volume_flag:` 再把 `maxVolume` 和 16 个 `*StartVolume` / `*EndVolume` 加进白名单（默认 `reset_volume_flag = True`，所以这条默认**不走**）；
3. 逐行按 `pair_instrument_key` 匹配，只覆盖白名单里的列；
4. **仅当该对子存档里 `auto_flag == False`**（手动单）时，额外同步 `auto_flag / profitSwitch / maxVolume / 8 个开关 / 32 个价差量参数`；
5. 收尾 `:313`：`maxVolume = max(maxVolume, abs(pairTotalVolume))`。

配套的 `load_manual_pair_info`（`:315-341`）读的是**运维手动命令文件** `./pair_command/<strategy>_command.csv`，应用完就删掉。

**三个关键观察：**

1. **算法单完全不落盘。** `algo_od_mgr.strategy_order_id` 是内存字典，启动即空 → 祖先**放弃**了崩溃前的算法单，也不去撤它。
2. **不撤任何单。** 启动路径里没有任何撤单动作。
3. **量价阶梯靠重算，不靠恢复。** `load_pair_info` 的调用点在 `update_pair_info_amount`（`cc_pricespread_gb_ltp.py:392`）**之后**，阶梯参数本来就由 `targetAmount` / `maxAmount` 现算。

也就是说，祖先对"全撤单 vs 接着做"的答案是：**都不。接续持仓，放弃算法单。**

---

## 二、为什么必须做：重启后的危害链

### 2.1 `pairTotalVolume` 是策略唯一的持仓账本

它的**唯一**写入方是 `PairInfoManager::UpdateOnAlgoOrder`（`PairInfoManager.cpp:290-302`）：

```cpp
pi->pairTotalVolume = volume;   // :296，数据来自算法单回调
```

重启后它 = 0。而 `HasPosition()`（`PairInfo.h:290-292`）= `std::abs(pairTotalVolume) > 1e-9` 是下面所有地方的**总闸**：

| 位置 | 后果 |
|---|---|
| `SignalGenerator::CanClose`（`SignalGenerator.cpp:280-286`，`:281` 判 `HasPosition`） | **平不掉** —— 想平仓也只会得到 `no position` |
| `RiskManager.cpp:83 / 98 / 162 / 222` | `CheckADLRisk` / `CheckSpreadNoRegression` / `CheckFundingAbnormal` / `CheckTinyClose` **四条风控全部早退**，孤儿持仓完全无人管 |
| `PairTradingContext.cpp:905 / 938 / 945 / 959 / 964` | 建仓分位快照、`wasFlat` 判定、`OnAlgoFinished(pi, !HasPosition())` 全部失真 |
| `PairInfoManager.cpp:409` | `maxVolume = max(maxVolume, abs(pairTotalVolume))` 的持仓地板失效 |

### 2.2 开仓侧没有任何持仓门槛

`SignalGenerator::CanOpen`（`SignalGenerator.cpp:231-278`）只看 `stopFlag` / `closeFlag` / `limitFlag` / `errorFlag` 和一条价差合理性：

```cpp
if (pi.limitFlag) { reason = "limitFlag = true"; return false; }   // :242
```

而 `limitFlag` **全仓库无人置位**（死字段）；`:252-269` 的 OI / 成交量检查是**注释掉的**。

→ **重启后策略会在孤儿持仓之上再开一笔**，敞口直接翻倍。这是比"平不掉"更严重的问题。

### 2.3 自愈的那一半

`activeRealPosition` / `passiveRealPosition` 会由**推送自愈**，现在有两条路都通：

| 路 | 驱动 | 来源 | 精度 |
|---|---|---|---|
| `UpdateOnPosition`（`PairInfoManager.cpp:192-234`） | `PairTradingStrategy::on_position`（`:137-140`） | `pubsub::Position` | **分腿**，带 `direction`/`avgPrice`/`liquidPrice`/`markPrice`/`adlQuantile` |
| `UpdateOnBalance`（`:268-284`） | `PairTradingStrategy::on_balance`（`:131-134`） | `pubsub::Balance` | **合成净额**（`total`，有符号） |

两条路都拼**点号全键**、都是精确命中（`UpdateOnPosition` 的分隔符笔误已修，见 §5.3.4）。
重启后一到两个推送周期内，`activeRealPosition` 就会收敛到真实值；
`activeAvgPrice` / `*LiquidPrice` / `*MarkPrice` / `*AdlRank` / `*LiquidStatus` 由 `UpdateOnPosition` 填。

**但没有任何人读 `activeRealPosition` 来修 `pairTotalVolume`。**

`docs/algo_order_source_review.md:689-695`（§5.5）已经提过这个建议，本方案把它纳入。

### 2.4 结论

真实持仓在进程内**是拿得到的**（持仓推送 + 余额推送），只是没人用它。所以恢复方案的核心不是"去交易所捞状态"，而是**把已经到手的数据接上**。

---

## 三、能力边界：这决定了方案可行性

### 3.1 `TradeClient` 只有四个操作

`tb/include/base/BaseTrade.h:41-46`：

```cpp
virtual int query_account(...) = 0;
virtual int add_new_order(...)  = 0;
virtual int cancel_order(...)   = 0;
virtual int query_order(...)    = 0;
```

`QuantTrade`（`QuantTrade.h:24-50`）同样是四个：`CreateOrder` / `CancelOrder` / `QueryOrder` / `QueryAccount`，且**都要求先有一个已知的 order 对象**：

- `CancelOrder` 发的是 `order.strategyOrderId`；
- `QueryOrder` 要 `exchangeOrderId` **和** `strategyOrderId` 同时给。

**没有"列在途订单"，没有"全撤"。** → 撤一张单，必须先有它的 `strategyOrderId`；而重启后内存里没有任何 order 对象。

### 3.2 但原料是齐的

`QuantOrderRecord`（`DataStruct.h:581-610`）已经带齐了重建一次撤单所需的全部字段：

```
strategyName, strategyOrderId, systemOrderId, exchangeOrderId, instrumentKey,
orderType, direction, orderStatus, price, volume, updateTime,
pairId, algoPairId, isActiveOrder, reduceOnly
```

并且逐单 CSV（`_quantOrder.csv` / `_pairOrder.csv` / `_algoOrder.csv`，后缀见 `WriteFileContent.h:51-57`）**每 200ms flush 一次**（`kFlushIntervalUs = 200 * 1000`，`WriteFileContent.h:17`），`Stop()`（`:406-417`）还会 flush + close。

**但这些是 append-only 的事件日志，没有读取方**，也不是"当前态快照"。要做"启动全撤"，得先把它们变成一份可读的"当前在途"清单。

---

## 四、三条路线对比

| | ① 全撤单 | ② 接续算法单 | ③ 接续持仓、放弃算法单 |
|---|---|---|---|
| 需要什么 | 持久化每张在途单的 `strategyOrderId` + 一个"查单/撤单"重放器 | 算法单全量持久化（含 `orderMgr` / `posMgrMakerTaker` / `posMgrTakerTaker` / `pairOrderMgr` / 在途子单） | 只要持仓账本（`pairTotalVolume` + 均价 + 建仓基准 + 风控档位） |
| 今天能做吗 | ❌ 缺"列单"能力，只能靠落盘 id 反推 | ❌ `SaveToFile` / `LoadFromFile` 是空桩；`AlgoContext.cpp:115-119` 查不到就 `LOG_ERROR` 返回 | ✅ 只需补 `LoadSnapshot` + 一步对账 |
| 与祖先一致 | ❌ 祖先启动不撤任何单 | ❌ 祖先的 `strategy_order_id` 是内存字典，启动即空 | ✅ 祖先 `load_pair_info` 只恢复持仓列 |
| 主要风险 | 撤单窗口内行情已变，可能把刚成交的撤成废单；撤不干净会留半条腿 | 在途子单 / 订单管理器 / 持仓管理器一起丢 → 内部状态与交易所**永久背离** | 交易所侧旧子单可能还挂着，会与新一轮报单**叠单** |
| 工作量 | 大 | 很大 | **小** |

**推荐：③。** 理由是它与祖先对齐、风险最小，而且**不管最终走哪条，`pairTotalVolume` 的写快照 + 启动对账都必须先做对** —— 它是另外两条的前置。

---

## 五、推荐方案

### 5.1 快照文件（把 5 列 CSV 换成"当前态快照"）

不扩列，直接换语义：新增 `SaveSnapshot(path)` / `LoadSnapshot(path)`，**写临时文件 + `rename` 原子替换**，避免崩在写一半时留下半截文件。

#### 5.1.1 恢复的本质：只恢复"策略自己那本账"

账户真值（持仓量、均价、浮盈、强平价、标记价、ADL 排名、强平/保证金状态）**全部来自交易系统推送**，
重启后一到两个推送周期内自愈（`PairInfoManager::UpdateOnPosition` `:192-234`，入口
`PairTradingStrategy.cpp:137-140`）→ **一个都不存**。

真正要恢复的只有四类，它们的共同点是：**由算法单回调累积出来，且无法从交易所反推**。

| # | 分组 | 字段 | 为什么交易所给不了 |
|---|---|---|---|
| 1 | **持仓账本** | `pairTotalVolume`、`pairActiveTotalPrice`、`pairPassiveTotalPrice` | 唯一写入方是算法单回调（`UpdateOnAlgoOrder:296-298`，只写这 3 个）。`pairTotalVolume` **就是主动腿持仓**，平仓与四条风控全建立在它上面（见 §5.3.1）。注意它**没有**持久化路径以外的来源：算法单不落盘 |
| 2 | **建仓基准** | `openSmallSpreadBidBidUQ`、`openSmallSpreadAskAskDQ` | 是"建仓那一刻"的 1h 分位数快照（`CaptureOpenSpreadSnapshot:904-916`，只在 `wasFlat → HasPosition` 那一次取）。`smallStats` 是进程内滚动窗口，重启后从零积累，取不回建仓时的值 |
| 3 | **风控档位** | `adlClose` / `spreadNoRegression` / `fundingAbnormal` 各 6 个字段（`triggered`、`currentTier`、`startTime`、`tier1Times`、`tier2Times`、`tier3Times`） | 是"强平档位"的**跨轮次累积**状态（`AdvanceTier:43-60`、`OnAlgoFinished:318-332`、`GetCurrentTier:6-40`）。丢了 → 档位从 tier1 重来、`startTime` 归零让 `elapsed` 从 0 起算，等于每次重启都把风控的耐心重置一遍 |
| 4 | **风控计时起点** | `positionExceedThresholdStartTime`、`spreadNoRegressionStartTime` | 同上，是"持仓超时 / 价差不回归"的计时起点（`RiskManager.cpp:104/112-113/163/195-196/311-312`）。丢了 = 时钟重置，4 天门槛要重新等 |
| 5 | **运维意图** | `autoFlag`、`stopFlag`、`closeFlag`、`profitPct` | 由 `ApplyCommand`（`PairInfoManager.cpp:466-506`）写入，是运维指令，没有任何自愈路径 |
| 6 | **错误态** | `errorFlag` | 由 `CheckExposureAbnormal:416` 写入，语义是"该对子判死、留人工处理"。清除方 = `PairCmd_RESUME`（`PairInfoManager.cpp:481-490`，本次新增，见 §5.5）→ 不存的话，重启会让一个已判死的对子静默复活 |
| 7 | **上一轮残留标记** | `hasActiveAlgoOrder`、`currentAlgoOrderId` | **只用于启动告警**（见 5.4）。重启后 `alogOrderManager` 是空的，恢复 id 本身没有意义 |

字段数：1（行键）+ 3 + 2 + 18 + 2 + 4 + 1 + 2 = **33 列**。

两个序列化细节：

- `openSmallSpread*` 默认值是 **NaN**，必须能往返（写 `nan`、读回 `NAN`），否则 `isnan` 哨兵失效
  （`RiskManager.cpp:175/180` 用它判断"尚未快照"）；
- `pairActiveTotalPrice` / `pairPassiveTotalPrice` 默认是 **-1.0**（"从未建仓"），要能与真实的负价格区分。

#### 5.1.2 不存：能自己算出来的

| 字段 | 重算方 |
|---|---|
| `ttTargetVolume`、`mtTargetVolume`、`maxVolume`、`minVolume`、`maxExposure` | `RecalcVolumeParams`，每 `volumeRecalcIntervalSec`（默认 60s）一次（`PairTradingContext.cpp:978-981`、`PairInfoManager.cpp:386-389/409`） |
| `orderParams` **全部 40 个字段**（8 开关 + 32 价差/量） | `RecalcOrderParams` **每轮全量覆盖**（`SignalGenerator.cpp:123-150`），完全派生自 `largeStats`/`smallStats` + `autoFlag`/`closeFlag` + `profitPct`/`profitSwitch` + `*LiquidStatus` + funding rate |
| `largeStats`、`smallStats` | 24h / 1h 滚动窗口，重启后重新积累 |
| `positionValue` | `CalcPositionValue()` 派生（`PairInfo.h:331-333`） |
| `modifyTime` | 运行态时间戳 |

> ⚠️ 祖先的 `update_columns_1`（手动单的 8 开关 + 32 参数，`pair_info_manager.py:293-298`）
> **在 C++ 版不适用**：`ApplyCommand` 改不了这些位，而 `RecalcOrderParams` 每轮把它们全量覆盖。
> 所以只需要恢复 `autoFlag`/`stopFlag`/`closeFlag`/`profitPct` 这四个**输入**，不需要恢复派生结果。

#### 5.1.3 不存：在 C++ 版里是死字段

祖先白名单里有一大半字段在 C++ 版**从未被写过**，所以本方案的存列表比祖先短得多：

| 字段 | 状态 |
|---|---|
| `pairPassiveTotalVolume` | 0 写入。`UpdateOnAlgoOrder:296-298` 只写 `pairTotalVolume`/`pairActiveTotalPrice`/`pairPassiveTotalPrice`；`PairTradingContext.cpp:722` 读它（恒 0）赋给算法单；`SaveToCSV:80` 写出去的值也恒 0。`BaseAlgoOrder`/`AlgoRebalanceOrder` 里的同名成员是**另一个结构** |
| `floatPnl`、`totalPnl`、`ttFloatPnl`、`ttTotalPnl`、`mtFloatPnl`、`mtTotalPnl` | 0 写入（`PositionManager` 里的同名 `totalPnl` 是另一个结构） |
| `activeMarginStatus`、`passiveMarginStatus` | 0 写入；只在 `SignalGenerator.cpp:139` 被读 → 恒 0 |
| `manualFlag` | 0 写入；只在 `PairTradingContext.cpp:744` 被读 → `isManual` 恒 false |
| `limitFlag` | 0 写入（死字段） |
| `reBalance`、`stopMarginTrade` | 0 写入 |
| `profitSwitch` | 0 写入 → 恒 `true`（读于 `SignalGenerator.cpp:54/74/93/113`） |
| `AbnormalCloseState::lastPositionValue` | 0 写入 |
| k 线统计 8 个（`activeDailyAmount` / `activeOI` / `activeMeanClose` / …） | `UpdateKlineStats` 全仓库无调用者 |
| `riskCloseOrderInFlight` | 重启后必然没有在途单，恒 false |
| `activeLiquidStatus`、`passiveLiquidStatus` | **有**写入，但由 `liquidPrice`/`markPrice` 算出（`UpdateSideLiquidStatus:243-261`）→ 随推送自愈，不用存 |

#### 5.1.4 存了也没用的（顺带发现）

`ApplyCommand` 的 `PairCmd_MODIFY` 能设 `maxVolume` / `ttTargetVolume` / `mtTargetVolume`
（`PairInfoManager.cpp:490-498`），但 `RecalcVolumeParams:386-389/409` 每 60s 覆盖一次
→ **手动改的量最多活 60s**。所以恢复它们没有意义。

写入时机：沿用现有两处（`pre_stop` + `OnTimer` 周期），把周期从 300s **收紧到 5-10s** —— 快照只有几十行，代价可忽略，换来崩溃时最多丢 10s 的账。

**必须同时修的两处**：

1. `etc/config.json:25` 的 `"./csv"` 改成文件路径（如 `"./data/pair_info.csv"`）；
2. 启动时 `mkdir -p` 父目录，否则第一次运行仍会 `LOG_ERROR`。

### 5.2 启动状态机

```
① Init ──────── PairInfo 建好，账本全 0（当前就是这个状态）
   │
   ▼
② Reconcile ─── 读快照 → 等真实持仓推送到位 → 对账 pairTotalVolume
   │              ├─ 快照缺失      → 按实时分腿持仓重建 + WARN
   │              ├─ 两者一致      → 用快照（保住均价/基准/档位）
   │              └─ 两者不一致    → 以实时持仓为准 + WARN + 飞书
   ▼
③ Armed ─────── 对账完成前，交易逻辑全部冻结
   │
   ▼
④ Trading ───── 放开，OnTimer / OnSpread 正常跑
```

**① → ② 在 `PairTradingContext::Init`（`PairTradingContext.cpp:73-88`）里做**，位置在 `pim.Init(...)` 之后、`LoadSnapshot` 之后。

**③ Armed 是新加的闸门，今天完全没有。** 现在的代码：

- `PairTradingContext::OnSpread`（`:90-106`）收到第一条价差就 `ProcessPairSignal`；
- `PairTradingContext::OnTimer`（`:973-1037`）第一次触发就跑全套风控 + 撤单检查 + 改参。

而价差推送通常**早于**持仓推送 → 现在重启后的头几十毫秒，策略是拿着 `pairTotalVolume = 0` 在跑的。必须加：

```cpp
// 未完成对账时，以下入口全部直接 return
//   ProcessPairSignal / ProcessRisk / CheckAlgoOrderTimeout
//   / CheckExposureAbnormal / ProcessModify
```

**④ 的放行条件**：对账完成（即 `activeRealPosition` / `passiveRealPosition` 都已到位）。若长时间不到位，**不能**按"无持仓"放行 —— 那等于放它去重复开仓，只能一直等 + 告警。

### 5.3 `pairTotalVolume` 的恢复规则（本方案的核心）

#### 5.3.1 `pairTotalVolume` 就是主动腿持仓

**`pairTotalVolume` = 主动腿的持仓量**（带符号）。它不是"两条腿合成出来的净持仓" ——
整个执行逻辑都以**主动腿仓位的变化**为准：

| 用处 | 位置 | 量纲 |
|---|---|---|
| 期望仓位 = `pairTotalVolume + frozenActiveVolume + inflightActiveVolume` | `BaseAlgoOrder::GetExpectActiveVolume`（`:307-360`） | 主动腿 |
| 阶梯目标 `targetActiveVolume` 与 `expectActiveVolume` 比较 | `AlgoPairOrder::GetTargetPairOrder`（`:295` / `:309` / `:426` / `:490`） | 主动腿 |
| 信号闸门 `vol = pi.pairTotalVolume` 与 `ttOLStartVolume` / `ttOLEndVolume` 等比较 | `SignalGenerator::CheckSignalForSatisfy`（`:176` / `:184-222`） | 主动腿 |
| 多空判定 `IsLong()` = `< 0`、`IsShort()` = `> 0` | `PairInfo.h:294-300` | 主动腿 |

符号之所以是"负 = 多"，只是因为**多头仓位的主动腿本身是空腿**：
`OPEN_LONG` → `activeDirection = DT_SHORT`（`AlgoPairOrder.cpp:427`）
→ `pairTotalVolume -= activeVolume`（`BaseAlgoOrder.cpp:259`）→ 负。
（同一条映射在 `PairManager.cpp:1339-1340` 有注释：`OpenLong CloseShort activeDirection = Short`。）

#### 5.3.2 对账 = 与主动腿持仓直接比

既然 `pairTotalVolume` 与 `activeRealPosition` **是同一个量**（都是主动腿持仓），
对账就是**直接相等比较**，不需要任何换算：

```
主判据：|pairTotalVolume - activeRealPosition| <= tol
```

被动腿**不参与**这个比较 —— 它只用于"净敞口是否失控"这条独立检查：

```
辅助：|activeRealPosition * activeMultiple + passiveRealPosition * passiveMultiple| / activeMultiple <= tol_net
```

（换算方式与 `CheckExposureAbnormal:407-408` 一致，把被动腿折成主动腿单位。）

#### 5.3.3 恢复规则

1. **快照缺失**（首次启动 / 文件损坏）→ 用 `activeRealPosition` 重建 `pairTotalVolume`，`LOG_WARN`；
2. **主判据通过** → 用快照值（保住均价、建仓分位数基准、风控档位）；
3. **不一致** → **以系统推送为准**（已定）：`pairTotalVolume = activeRealPosition`；风控档位保留，`LOG_WARN` + 飞书播报；
4. **推送尚未到位** → 停在 Armed，不放行。

#### 5.3.4 两条推送路现在都活了（分隔符已修）

**曾经的 bug**：`UpdateOnPosition:193` 拼 instrument key 用的是**逗号**，
而全仓库其它拼 key 的地方都用**点号**：

| 拼 key 的地方 | 代码 | 分隔符 |
|---|---|---|
| `Init` 里注册用的 key（`RegisterInstrument`，`PairInfoManager.cpp:25-28` / `:56-57`），取自 config 的 `BINANCE.USDT_SWAP.DOGE-USDT` | — | `.` |
| `AccountManager.cpp:56` | `... + "." + ... + "." + instId` | `.` |
| `PositionManager.cpp:413` | `... + "." + ... + "." + depth.instrument` | `.` |
| `UpdateOnBalance:270` | `fmt::format("{}.{}.{}", ...)` | `.` |
| ~~`UpdateOnPosition:193`~~ | ~~`... + "," + ... + "," + instId`~~ | **`,`** ← 已修为 `.` |

→ 原来拼出的是 `BINANCE,USDT_SWAP,DOGE-USDT`，而 `:195` 的 `FindPairsByInstrument`
查 `m_instrToPairs`（key 是点号格式）→ **恒查不到** → `:197` 直接 return，整个函数一行都不写。
**现已改为点号（`UpdateOnPosition:193`），`Position` 路恢复生效。**

**修复前后对照：**

| 字段 | 修复前 | 修复后 |
|---|---|---|
| `activeAvgPrice` / `passiveAvgPrice` | 恒 `-1.0` | ✅ 由 `UpdateOnPosition` 写入 |
| `activeLiquidPrice` / `passiveLiquidPrice` / `activeMarkPrice` / `passiveMarkPrice` / `activeAdlRank` / `passiveAdlRank` | 0 写入 | ✅ 写入 |
| `activeLiquidStatus` / `passiveLiquidStatus` | 恒 0 | ✅ 写入 |
| `activeRealPosition` / `passiveRealPosition` | 只有 `UpdateOnBalance` 一条路 | ✅ 两条路（建议以 `pubsub::Position` 为准，见下） |
| `activeFloatPnl` / `passiveFloatPnl` | 只有 `UpdateOnBalance` 一条路 | ✅ 两条路 |

（另一个 `FindPairsByInstrument` 调用方 `UpdateKlineStats:416` 本来就无调用者；
现在这条注册表终于被 `UpdateOnPosition` 真正用上了。）

> **顺带解除的三个封印**（原来都因字段恒为初值而永不生效）：
> 1. `CheckExposureAbnormal` 变得**可达**（`:392` 的 `activeAvgPrice > 0 && passiveAvgPrice > 0` 现在能过）→ `errorFlag` 才可能被置位；
> 2. `SignalGenerator.cpp:139`（流动性风险禁开仓）生效；
> 3. `ProcessModify:545`（`aggressiveClose`）生效。
>
> ⚠️ 这三个都是**新出现的活路径**，等于把之前"死代码"掩盖掉的 bug 一起放出来了 —— 上线前需要重点回归（尤其 `errorFlag` 的触发阈值，见 §5.5）。

**这条已确认是笔误并已修**：**同一个 `pubsub::Position` 推送**在 `AccountManager::OnPosition`
（`AccountManager.cpp:53-73`）里被正确消费 —— `:56` 用的就是**点号**：
`ExchangeTypeEnum2StrMap[...] + "." + InstTypeEnum2StrMap[...] + "." + position.instId`，
并且 `:68-73` 按 `position.direction` 正确分多空。`UpdateOnPosition` 曾是它的坏副本，
现在两边一致了。

> 备选方案（未采用）：真实持仓其实也落在 `AccountManager::mAccount[accountId].mPosition[dotKey]` 里，
> 但 `mAccount` 是 **private 且没有 accessor**（`AccountManager.h:29-32`），
> 要用得先加一个接口 —— 不如直接把 `:193` 的 `","` 改成 `"."`（一个字符）来得干净。

**修复后的三个变化：**

1. **对账有两个来源了**（都写 `activeRealPosition`/`passiveRealPosition`，"后到覆盖先到"）：

   | | `UpdateOnPosition` | `UpdateOnBalance` |
   |---|---|---|
   | 来源 | `pubsub::Position` | `pubsub::Balance` |
   | 量 | **分腿**（按 `direction` 定符号） | **合成净额** `total`（已确认有符号） |
   | 匹配 | 全键**精确**相等（`:214` / `:223`） | 全键 **`strstr` 子串**（`:274` / `:279`，键含 exchange+instType，实际等价精确） |
   | 额外字段 | `avgPrice` / `liquidPrice` / `markPrice` / `adlQuantile` | `unrealizedPnl`（写 `*FloatPnl`） |
   | 看 `accountId`？ | ❌ | ❌ |

   建议**以 `pubsub::Position` 为准**：分腿级、带 `direction`、额外信息更全；
   `total` 是账户合成净额，粒度更粗，且与分腿口径是否一致取决于交易系统怎么算。
   若要收敛到单一来源，让 `UpdateOnBalance` 只写余额类字段即可（§5.6 第 9 项）。
2. **`CheckExposureAbnormal` 恢复可达** → `:392` 的 `activeAvgPrice > 0 && passiveAvgPrice > 0` 现在能过 →
   **`errorFlag` 从此可能被置位**。所以 §5.5 的"清除路径"是**必须做**的（已做，见下）。
3. **`SignalGenerator:139`（流动性风险禁开仓）与 `ProcessModify:545`（`aggressiveClose`）恢复生效** ——
   依赖的 `*LiquidStatus` 现在有值了。

顺带把 §5.5 的定期对账一起做掉：在 `OnTimer` 里周期性地用 `activeRealPosition` 校正 `pairTotalVolume`。这样下面这两个场景也能自愈：

- 算法单在策略看到终态之前就被 `AlgoContext` 删掉（`AlgoContext.cpp:1906-1915` 那一支）；
- 交易所侧被人工平仓。

> 注意：定期对账有一个语义边界 —— 主动腿**成交到一半**时（主动腿成了、被动腿还挂着），
> `pairTotalVolume` 与 `activeRealPosition` 都在动但可能差一拍，属于合法的中间态。
> 对账最好在**没有在途算法单**时做，否则会把中间态当成偏差纠偏。

### 5.4 交易所侧的旧子单（孤儿单）

`pre_stop()`（`PairTradingStrategy.cpp:79-91`）**不撤任何单**（`docs/algo_order_source_review.md` §3.4）。所以：

| 场景 | 结果 |
|---|---|
| 优雅停机（`pre_stop` 跑到） | 交易所侧可能仍有上一轮挂着的子单 → 重启后与新报单**叠单**，同一对子出现两套报价 |
| `SIGKILL` / 崩溃 | 同上，且连快照都是最多 10s 前的 |

今天**撤不掉**（见 §3.1）。分两步：

- **短期（随本方案一起做）**：加启动告警 —— 若快照里 `hasActiveAlgoOrder == true`，飞书播报"上一轮有算法单未终结，交易所侧可能残留子单，请人工确认"。
- **中期（独立任务）**：把逐单 CSV 从"事件流"改成"当前在途清单"，启动时对每张在途单走一次 `QuantTrade::QueryOrder` → 若仍 `OS_NEW` / `OS_PARTFILLED` 就 `CancelOrder`。这条路能落地，但依赖 §3.2 说的 id 持久化。

### 5.5 `errorFlag`：实测语义与修法

唯一写入方是 `CheckExposureAbnormal`（`PairTradingContext.cpp:416`），触发条件是
`netExposure > exposureCancelTimes(=4.0) × ttTargetVolume`（`:407-412`）。

**实测语义**（逐个入口查过）：

| 入口 | 是否被 `errorFlag` 挡住 |
|---|---|
| `SignalGenerator::CanOpen:247` | ✅ 挡 → 不开仓 |
| `ProcessPairSignal:172` | ✅ 挡 → **也不平仓** |
| `CheckExposureAbnormal:384` | ✅ 挡 → 不重复触发 |
| `ProcessRisk`（`:226-246`）/ `RiskManager::CheckRisk`（`:262-302`） | ❌ **不挡** → 风控强平仍然有效 |
| `ProcessModify`（`:518-567`） | ❌ 不挡 |
| `CheckAlgoOrderTimeout`（`:340-378`） | ❌ 不挡（会撤单） |

→ 它**不是完全冻结**，而是"禁止策略自己开平，只留风控强平这一条出口"，
与祖先 `status = ERROR`（留人工处理）的语义一致。

> ⚠️ **前提（已解除）：`errorFlag` 曾经永远不会被置位。**
> `CheckExposureAbnormal:392` 要求 `activeAvgPrice > 0 && passiveAvgPrice > 0`，
> 而这两个字段的**唯一写入方**曾在**死掉的** `UpdateOnPosition:216` / `:225` 里（见 §5.3.4）
> → 恒为 `-1.0` → 这个检查每次都在 `:392` 提前 return。
> **`UpdateOnPosition` 的分隔符已修，这条路活了 → `errorFlag` 从此可以被置位。**
> 所以下面三条现在是**真实生效**的路径，不再是纸面分析。

**问题 1（必要）：没有清除路径 —— ✅ 已实现。**
`ApplyCommand`（`PairInfoManager.cpp:466-506`）原来四个分支都不碰 `errorFlag`，
连 `PairCmd_RESUME` 也不清。置位后只能重启进程 —— 而快照若又把它恢复了，连重启都救不回来。

> **改动**（`PairInfoManager.cpp:481-490`，`PairCmd_RESUME` 分支）：
>
> ```cpp
> case PairCmd_RESUME:
>     // 错误态也在这里复活：errorFlag 的唯一写入方是 CheckExposureAbnormal，
>     // 语义是"该对子判死、停自动、留人工处理"。RESUME = "恢复自动"，
>     // 正是它的对偶操作，否则置位后除了改快照/重启没有第二条路。
>     // 风险自限：敞口若没真正解决，下一轮 CheckExposureAbnormal 会立刻再次置位。
>     if (pi->errorFlag) {
>         LOG_WARN("ApplyCommand RESUME: pairKey:{} clear errorFlag (was set by CheckExposureAbnormal)", pairKey);
>         pi->errorFlag = false;
>     }
>     pi->stopFlag = false;
>     pi->closeFlag = false;
>     pi->autoFlag = true;
>     break;
> ```
>
> 语义：`RESUME` = "恢复自动"，而 `errorFlag` 正是"自动交易已停"的标记，两者是一回事。
> 风险自限：若敞口问题没解决，下一轮 `CheckExposureAbnormal` 会立刻再次置位 ——
> 这是**正确**行为（它反映真实敞口），不会失控。
> 所以清除路径是**运维显式动作**，没有做自动清除。
>
> **状态**：源码已改，但**未编译验证** —— 本机无 `fmt`、构建树配在部署机 `/workspace`，
> 需在部署机上 `make` 确认。

**问题 2（重要）：触发条件在 `Position` 路修好后可能误报 —— 已部分澄清。**
`netExposure` 的判据依赖两条腿**符号相反**。`balance.total` **已确认是有符号净持仓**
（你确认的），所以 `UpdateOnBalance` 这条路不会把两条腿写成同号，
原先担心的"净敞口退化成总持仓（≈2V）"**不成立**。
剩下的风险是**两个写入方抢同一字段**（后到覆盖先到）：
`UpdateOnPosition` 写的是分腿真实持仓，`UpdateOnBalance` 写的是**合成净额**，
若 `total` 的口径（是否含被动腿折算、是否含未成交）与分腿口径有细微差异，
`errorFlag` 的阈值判断就会在两次推送之间**抖动**。
→ 建议收敛到单一来源（以 `pubsub::Position` 为准），见 §5.3.4 变化 1。

**问题 3（设计）：要不要进快照？**

| 选择 | 后果 |
|---|---|
| 进快照 | 重启后该对子仍判死，需运维 `RESUME` 复活。安全，且**清除路径已就绪**（问题 1） |
| 不进快照 | 重启后自动复活，带着未解决的敞口继续交易。危险 |

→ 建议**进快照**；"问题 1"这个前置**已经完成**，不再阻塞。

### 5.6 落地清单（按依赖顺序）

| # | 动作 | 文件 | 状态 |
|---|---|---|---|
| 0 | **前置**：修 `UpdateOnPosition` 的 key 分隔符（`","` → `"."`），否则 `Position` 路整条是死的（§5.3.4） | `PairInfoManager.cpp:193` | ✅ **已完成** |
| 1 | 加 `PairCmd_RESUME` 清 `errorFlag`（§5.5 问题 1），这是 `errorFlag` 进快照的前提 | `PairInfoManager.cpp:481-490` | ✅ **已完成**（未编译验证） |
| 2 | 修 `LoadFromCSV`（或换成 `LoadSnapshot`）：解析 `line`、按 `pairInstrumentKey` 查表、写回、补 `return` | `PairInfoManager.cpp:85-107` | ⬜ |
| 3 | 修 `Init` 里的反向判断（成功打 INFO、失败打 WARN） | `PairTradingContext.cpp:81-85` | ⬜ |
| 4 | 修配置路径 + 启动建目录 | `etc/config.json:25` | ⬜ |
| 5 | 加 `SaveSnapshot`（原子写），在 `pre_stop` / `OnTimer` 调用，周期收紧到 5-10s | `PairInfoManager.cpp`、`PairTradingStrategy.cpp:80`、`PairTradingContext.cpp:1031` | ⬜ |
| 6 | 加启动状态机（Reconcile / Armed / Trading 三个闸门） | `PairTradingContext.cpp:73-88`、`:90-106`、`:973-1037` | ⬜ |
| 7 | 加 `pairTotalVolume` 对账（启动一次 + `OnTimer` 周期一次） | `PairTradingContext.cpp:973-1037` | ⬜ |
| 8 | 加孤儿单启动告警 | `PairTradingContext.cpp` | ⬜ |
| 9 | （可选）把持仓字段收敛到单一来源：让 `UpdateOnBalance` 只写余额类字段，不再写 `activeRealPosition`/`passiveRealPosition`（§5.3.4 变化 1） | `PairInfoManager.cpp:268-283` | ⬜ |

---

## 六、待决策

**已定：**

- **存列表 = 33 列**（§5.1.1），只覆盖"持仓账本 / 建仓基准 / 风控档位与计时 / 运维意图 / 错误态 /
  上一轮残留标记"六组；账户真值全部等推送；派生字段与死字段一律不存。
  祖先的 `update_columns_1`（手动单 8 开关 + 32 参数）在 C++ 版**不适用**，已从存列表移除。
- **`pairTotalVolume` = 主动腿持仓**（§5.3.1）；对账就是与 `activeRealPosition` **直接相等比较**（§5.3.2）。
- **对账不一致时以系统推送为准**（§5.3.3 规则 3）。
- **`UpdateOnPosition` 的 key 分隔符**已修（`","` → `"."`），`Position` 路恢复生效（§5.3.4）。✅ 已改
- **`balance.total` 是有符号净持仓**（已确认）→ `UpdateOnBalance` 不会把两条腿写成同号（§5.5 问题 2）。
- **`errorFlag` 的清除路径**：`PairCmd_RESUME` 已一并清 `errorFlag`（§5.5 问题 1）。✅ 已改（未编译验证）

仍需拍板：

1. **持仓字段要不要收敛到单一来源？** 现在 `UpdateOnPosition`（`pubsub::Position`，分腿、带 `direction`、
   带 `avgPrice`/`liquidPrice`/`markPrice`/`adlQuantile`）与 `UpdateOnBalance`（`pubsub::Balance`，
   只给合成净额 `total`）**都写** `activeRealPosition`/`passiveRealPosition`，后到覆盖先到。
   建议**以 `pubsub::Position` 为准**，让 `UpdateOnBalance` 只写余额类字段（§5.3.4 变化 1、§5.6 第 9 项）。
2. **快照周期取多少？** 建议 5-10s（现在 300s）。
3. **`Armed` 的等待上限？** 建议**一直等 + 告警**，不设超时放行（按无持仓放行的后果是重复开仓）。
4. **孤儿单**：本期只告警，还是现在就把在途单 id 持久化 + 启动撤单一起做（§5.4）？
5. **`pairActiveTotalPrice` / `pairPassiveTotalPrice` 保不保？** 对账不一致时以推送为准，
   会丢掉这两个策略自己的记账价。它们除了对账还有别的用途吗？
6. **`errorFlag` 进快照**（§5.5 问题 3）—— 建议进；清除路径已就绪，不再阻塞。
7. **上线回归重点**：`Position` 路修好后，`CheckExposureAbnormal` / `SignalGenerator:139` /
   `ProcessModify:545` 三条**原本不可达**的路径同时变活（§5.3.4）。需要确认它们的阈值在真实
   行情下不会误触发 —— 尤其 `errorFlag` 的 `4 × ttTargetVolume`。
