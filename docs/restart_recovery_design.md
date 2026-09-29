# 挂掉重启恢复方案（设计稿）

设计对象：`utrade_hft` 策略层在**进程崩溃 / 被 kill / 正常重启**后，如何恢复运行状态。

涉及文件：`src/strategy/PairTradingStrategy.cpp`、`quant_library/algo/PairTradingContext.{h,cpp}`、`quant_library/algo/AlgoContext.{h,cpp}`、`quant_library/basic/PairInfoManager.{h,cpp}`、`quant_library/basic/PairInfo.h`、`quant_library/basic/BaseAlgoOrder.{h,cpp}`、`etc/config.json`。

祖先参照：`pair_trading_c_gateio/cc_pricespread_gb_ltp.py:393-397`、`utility/pair_info_manager.py:282-341`。

**本稿只做设计，未修改任何源码。**

---

## 〇、结论速览

| 问题 | 结论 |
|---|---|
| 现在能把 pairinfo 存到 CSV 吗？ | ✅ **能了**（本次已实现 `SaveSnapshot`/`LoadSnapshot`，33 列 + 原子写）。**修复前**是不能的：`csvStatePath` 指向一个**目录**，`SaveToCSV` 打开必然失败；且 `LoadFromCSV` 是个一行都读不回来的坏桩 |
| 重启后策略知道自己的持仓吗？ | ✅ **能了**（本次已实现：快照 + 启动对账 + `Reconciling` 闸门，§5.2）。**修复前**是不能的：`pairTotalVolume` 归零 → `HasPosition()` 恒 false → 平仓、四条风控、建仓基准全部失效；同时开仓侧**没有任何持仓门槛**，会在孤儿持仓之上再开一笔 |
| 该"全撤单"还是"接着做"？ | ⚠️ **两个都做不了**。`TradeClient` 只有 `query_account` / `add_new_order` / `cancel_order` / `query_order`，**没有"列在途单"、没有"全撤"**；算法单侧 `SaveToFile` / `LoadFromFile` 是空桩 |
| 推荐哪条路？ | ✅ **第三条：接续持仓、放弃算法单** —— 只恢复持仓账本与风控状态，算法单本来就不落盘。与祖先一致（`load_pair_info` 只恢复持仓列），工作量最小，且是另外两条的必经前置 |
| 交易所侧的旧子单怎么办？ | 今天撤不掉。短期只能**告警 + 人工确认**（✅ 已实现：`HandleOrphanAlgoOrders` 告警 + 置 `errorFlag` 冻结，§5.4）；要真撤，得先把在途单的 `strategyOrderId` 做成可读快照 |

一句话：**先把"持仓账本"这一件事做对（写快照 + 启动对账 + 冻结闸门），再谈撤单。**

> **已落地的源码改动**（本方案的前置 + 快照 + 启动闸门）：
> 1. `UpdateOnPosition:193` 的 key 分隔符 `","` → `"."`（`Position` 路从"整条死"恢复生效，§5.3.4）；
> 2. `ApplyCommand` 的 `PairCmd_RESUME` 一并清 `errorFlag`（补上原本不存在的清除路径，§5.5）；
> 3. `SaveSnapshot` / `LoadSnapshot` 取代 `SaveToCSV` / `LoadFromCSV`（33 列、原子写、NaN 往返，§5.1.5）；
> 4. 落盘周期 300s → 10s；`csvStatePath` `"./csv"` → `"./data/pair_info.csv"` + 自动建目录；
> 5. `PairTradingContext::Init` 的反向日志判断修正；
> 6. **启动状态机** `Reconciling → Trading` + 五处交易入口早退 + 启动对账 + 孤儿单告警冻结（§5.2）。
>
> **仍未做**：运行中的定期对账（**决定不做**，见 §5.3.5）、持仓字段收敛到单一来源（§5.6 第 9 项）、
> 孤儿单的中期动作（在途单持久化 + 启动撤单）。
> 全部改动都**未在部署机上编译验证**（本机缺 `cmake` / `fmt`）。
>
> ⚠️ **复查新发现（§6.1）：`errorFlag` 没有可达的清除路径。**
> `ApplyCommand` 零调用点，`AlgoContext::OnCommand`（`AlgoContext.cpp:233`）函数体全被注释掉
> → `PairCmd_RESUME` 发不出去 → 我这次加的清除分支**走不到**。
> 目前唯一解冻手段是**删快照 + 重启**（安全但未文档化）。
> 这同时意味着 `freezeOnOrphanAlgoOrder` 的默认值应先改成 `false`（§5.6 第 10b 项）。
>
> **接入点已定（§6.1.1）**：`OnCommand` 那条路确认**不再使用**；改为在 `OnTimer` 顶部
> （启动闸门之前）**轮询指令文件** —— 对齐祖先 `load_manual_pair_info`
> （`cc_pricespread_gb_ltp.py:1094-1098`）。文件格式与语义见 §6.1.1。

---

## 一、现状（修复前）：已有的 pairinfo → CSV 接口是断的

> **本节记录的是修复前的状态，用于解释为什么要重做。** 已于本次实现中全部修掉 ——
> `SaveToCSV`/`LoadFromCSV` 被 `SaveSnapshot`/`LoadSnapshot` 取代，见 §5.1 与 §5.6。

### 1.1 写入侧（修复前）

`PairInfoManager::SaveToCSV`（`PairInfoManager.cpp:65-83`）只写 **5 列**：

```
pair_instrument_key,pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,pairPassiveTotalVolume,
```

调用点两处：

| 位置 | 时机 |
|---|---|
| `PairTradingStrategy::pre_stop`（`PairTradingStrategy.cpp:80-82`） | 优雅停机 |
| `PairTradingContext::OnTimer`（`PairTradingContext.cpp:1031-1036`） | 每 `csvSaveIntervalSec`，默认 **300s**（`PairTradingContext.h:66`） |

### 1.2 读取侧（修复前）

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
| 2 | **建仓基准** ~~`openSmallSpreadBidBidUQ`、`openSmallSpreadAskAskDQ`~~ | ⚠️ **2026-09-29 起已无人读取**。原先的"建仓那一刻的 1h 分位数快照"（`CaptureOpenSpreadSnapshot`，只在 `wasFlat → HasPosition` 那一次取）有两个致命问题：`smallStats` 全工程没有生产者（`UpdateSmallStats` 零调用者）→ 恒为 NaN → `isnan` 哨兵永不触发；且轴错配（多头比 `spreadAskAsk`、空头比 `spreadBidBid`，正好是对方方向的平仓轴）。"价差不回归"已改为复用执行端平仓阈值（`SignalGenerator::CloseSpreadReached`）。这两列**暂留仅为保持列布局**，待格式统一迁移时删除 |
| 3 | **风控档位** | `adlClose` / `spreadNoRegression` / `fundingAbnormal` 各 6 个字段（`triggered`、`currentTier`、`startTime`、`tier1Times`、`tier2Times`、`tier3Times`） | 是"强平档位"的**跨轮次累积**状态（`AdvanceTier:43-60`、`OnAlgoFinished:318-332`、`GetCurrentTier:6-40`）。丢了 → 档位从 tier1 重来、`startTime` 归零让 `elapsed` 从 0 起算，等于每次重启都把风控的耐心重置一遍 |
| 4 | **风控计时起点** | `positionExceedThresholdStartTime`、`spreadNoRegressionStartTime` | 同上，是"持仓超时 / 价差不回归"的计时起点（`RiskManager.cpp:104/112-113/163/195-196/311-312`）。丢了 = 时钟重置，4 天门槛要重新等。**注意 `positionStartTime`（2026-09-29 新增，最短持有期门槛）刻意不进快照** —— 它是运行态字段，重启后重新起算，方向是"推迟风控"，落在安全侧 |
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
| `smallStats` + `UpdateSmallStats` + `CaptureOpenSpreadSnapshot` | **2026-09-29 起确认是死链**：`UpdateSmallStats`（`PairInfoManager.cpp:415`）零调用者 → `smallStats` 恒无效 → `CaptureOpenSpreadSnapshot` 的 `IsValid()` 守卫永远早退 → `openSmallSpread*` 恒 NaN。原先唯一读取方 `CheckSpreadNoRegression` 已改用 `SignalGenerator::CloseSpreadReached`。可整体删除（含快照 2 列） |
| `riskCloseOrderInFlight` | 重启后必然没有在途单，恒 false |
| `activeLiquidStatus`、`passiveLiquidStatus` | **有**写入，但由 `liquidPrice`/`markPrice` 算出（`UpdateSideLiquidStatus:243-261`）→ 随推送自愈，不用存 |

#### 5.1.4 存了也没用的（顺带发现）

`ApplyCommand` 的 `PairCmd_MODIFY` 能设 `maxVolume` / `ttTargetVolume` / `mtTargetVolume`
（`PairInfoManager.cpp:490-498`），但 `RecalcVolumeParams:386-389/409` 每 60s 覆盖一次
→ **手动改的量最多活 60s**。所以恢复它们没有意义。

写入时机：沿用现有两处（`pre_stop` + `OnTimer` 周期），把周期从 300s **收紧到 10s** —— 快照只有几十行，代价可忽略，换来崩溃时最多丢 10s 的账。

**必须同时修的两处**：

1. `etc/config.json:25` 的 `"./csv"` 改成文件路径（如 `"./data/pair_info.csv"`）；
2. 启动时 `mkdir -p` 父目录，否则第一次运行仍会 `LOG_ERROR`。

#### 5.1.5 实现（已落地）

| 项 | 实现 |
|---|---|
| API | `bool SaveSnapshot(path)` / `int LoadSnapshot(path)`（返回恢复的对子数，`<0` = 不可用）。取代 `SaveToCSV` / `LoadFromCSV` |
| 位置 | `PairInfoManager.cpp` 的匿名 namespace（helper）+ 两个成员函数；声明在 `PairInfoManager.h:29-36` |
| 原子写 | 写 `path + ".tmp"` → `std::rename` 原子替换；`writeOk` 检查后才 rename |
| 精度 | `ofs << std::setprecision(17)`，double 无损往返 |
| NaN 往返 | `openSmallSpread*` 为 NaN 时写出 `nan`，`std::stod`（走 `strtod`）读回 NaN ✓ |
| 容错 | 空串 → 保留默认值；坏值 → `LOG_WARN` + 回退默认；**列数不符 → 整行 skip**；表头列数不符 → 整个快照拒绝加载（挡住旧的 5 列 CSV） |
| 建目录 | `EnsureParentDir()` 逐级 `mkdir(..., 0755)`，忽略 `EEXIST` |
| 行序 | 按 `m_pairKeys`（config 顺序）写，保证输出稳定可比对 |
| 周期 | `csvSaveIntervalSec` **300 → 10**（`PairTradingContext.h:66`） |
| 配置 | `etc/config.json` 的 `csvStatePath`：`"./csv"` → `"./data/pair_info.csv"` |
| 启动日志 | `PairTradingContext::Init` 原来把成功/失败打反（成功 `WARN`、失败 `INFO`），已修正 |

列布局（33 列，字符串列放最后）：

```
pairInstrumentKey,
pairTotalVolume,pairActiveTotalPrice,pairPassiveTotalPrice,
openSmallSpreadBidBidUQ,openSmallSpreadAskAskDQ,
adlClose.{triggered,currentTier,startTime,tier1Times,tier2Times,tier3Times},
spreadNoRegression.{同上 6},
fundingAbnormal.{同上 6},
positionExceedThresholdStartTime,spreadNoRegressionStartTime,
autoFlag,stopFlag,closeFlag,profitPct,
errorFlag,
hasActiveAlgoOrder,
currentAlgoOrderId
```

**已验证**：用独立 harness 复刻同一套序列化逻辑做了 35 项往返断言（NaN 性、double 位精确、int64、列数 33、空字符串列），全部通过。
**未验证**：真实工程编译 —— 本机缺 `cmake` / `fmt`，需在部署机上 `make`。

### 5.2 启动状态机（✅ 已实现）

```
① Init ──────── PairInfo 建好 → LoadSnapshot 填账本 → 孤儿单告警 → 进入 Reconciling
   │
   ▼
② Reconciling ─ OnTimer 每 tick 尝试对账；未完成前**交易入口全部 return**
   │              ├─ 该腿的持仓信息已知？（推送 or 账户批次已完整到达）
   │              ├─ 一致      → 保留快照值（保住均价/基准/档位）
   │              └─ 不一致    → 以实时持仓为准
   ▼
③ Trading ───── 放行，OnTimer / OnSpread 正常跑
```

**为什么必须有这一步**：价差推送通常**早于**持仓推送，而 `CanOpen`（`SignalGenerator.cpp:231-278`）
**没有持仓门槛**、`CanClose`（`:280-285`）反而要求 `HasPosition()`。所以拿一本不可信的账开跑会：

| 账本 | 交易所 | 后果 |
|---|---|---|
| 0 | 有仓 | 开仓照开（在孤儿仓上再开一笔，敞口翻倍）、平仓被挡（平不掉） |
| ≠0 | 空仓 | `canClose = true` → 发 CLOSE_LONG → `activeDirection = DT_LONG`（`AlgoPairOrder.cpp:491`）→ **实际是反向裸开** |

#### 5.2.1 实现

| 项 | 实现 |
|---|---|
| 状态 | `PairTradingContext::StartupPhase{Reconciling, Trading}`（`PairTradingContext.h:142-152`），默认 `Reconciling` |
| 冻结范围 | `OnTimer` 顶部一处早退 → 覆盖 `ProcessRisk` / `CheckAlgoOrderTimeout` / `CheckExposureAbnormal` / `ProcessModify`（这四个**只**从 `OnTimer` 调用）；`ProcessPairSignal` 顶部单独一处早退 |
| 对账时机 | 只在 `OnTimer` 里试（tick 默认 1s）→ 放行延迟最多 1s。`OnSpread` 里的 `UpdateRtSpread` / `AccumulateSpreadSample` **不挡**，价差样本继续积累 |
| 放行条件 | 每个对子的**两条腿**都要"持仓信息已知"（见下），任一不满足则整体不放行 |
| 超时策略 | **一直等 + 每 30s 告警，绝不超时放行**（按"无持仓"放行 = 重复开仓） |
| 对账容差 | `max(1e-9, |activeRealPosition| × reconcileTolRatio)`，`reconcileTolRatio` 默认 `1e-6` |
| 新增字段 | `PairInfo::activePushArrived` / `passivePushArrived`（`UpdateOnPosition` 里置位，不进快照） |

#### 5.2.2 "持仓信息已知"的判据 —— 为什么要两个来源

只等 `pubsub::Position` 推送是**不够**的：交易所一般不会为"从未持有过"的腿推零仓
（`tb/src/oms/AccountManager.cpp:109-144` 只在**曾经有过**的仓位上合成零仓推送），
那样空仓的对子会永远等不到"推送到位"，启动闸门永远打不开。

所以用两个来源，满足其一即可：

1. **该腿的持仓推送到达过**（`activePushArrived` / `passivePushArrived`）；
2. **该腿所在账户的持仓批次已完整到达过**（`pubsub::Position::isLast == true`）
   —— 批次里没有这条腿，就说明它是空仓，此时 `activeRealPosition` 保持默认 0，对账照样成立。

> ⚠️ 批次必须**按 `accountId` 分开记**（`m_positionBatchDoneUs`）：`isLast` 是
> **单账户单次应答**的批次尾标记（适配器写成 `isLast = (i + 1 == pending.size())`），
> 用一个全局标记会把"主动腿账户的批次到了"误判成"被动腿也到了"。

#### 5.2.3 对账规则（`ReconcilePair`）

主判据：`|pairTotalVolume - activeRealPosition| <= tol` **且符号相同**
（量级相同但多空翻转必须算不一致）。被动腿不参与主判据。

| 情形 | 处理 |
|---|---|
| 一致 | 保留快照值（保住记账均价、建仓分位数基准、风控档位） |
| 不一致 + 交易所**空仓** | 账本清零；`pairActiveTotalPrice`/`pairPassiveTotalPrice` 复位 `-1.0`；`openSmallSpread*` 复位 `NaN`；三个风控档位与两个计时起点复位 —— 与 `RiskManager::OnAlgoFinished(fullyFlat == true)` 保持同一套不变量 |
| 不一致 + 交易所**仍持仓** | 只把 `pairTotalVolume` 改成实时值；**保留**记账均价与风控档位（均价是建仓基准、部分平仓后不变；档位是跨轮次累积的风控耐心） |

> ⚠️ "仍持仓"那一支有个残余风险：若进程宕机期间该对子被**平掉又重开**，快照里的均价就是陈旧的。
> 这正是每次不一致都打 `LOG_ERROR` 的原因 —— 需要人工看一眼（对应 §6 待决策第 4 条）。

#### 5.2.4 孤儿单（§5.4 的短期动作，一起做了）

`HandleOrphanAlgoOrders()` 在 `Init` 里、`LoadSnapshot` 之后立刻跑：

- 若快照里 `hasActiveAlgoOrder == true` → `LOG_ERROR` 告警（带 `algoOrderId`）；
- 且 `freezeOnOrphanAlgoOrder`（默认 `true`）→ 置 `errorFlag = true`，复用"判死、停自动、留人工处理"
  语义，等运维 `PairCmd_RESUME` 复活（§5.5 已给它加了清除路径）。

⚠️ **必须在这里做**：`PairTradingStrategy::ScanFinishedAlgoOrders` 第一次跑就会因为
"算法单在 `AlgoContext` 里找不到"而把 `hasActiveAlgoOrder` 清掉，之后再也看不出痕迹。

**验证状态**：对账状态机与判据已用独立 harness 验过（31 项断言全过：一致 / 容差边界 / 符号翻转 /
空仓复位 / 首次启动重建 / 批次放行 / 陈旧批次不算数 / 多对子整体冻结 / 不超时放行）。
**整个工程仍未编译**（本机缺 `cmake` / `fmt`），需在部署机 `make`。

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

#### 5.3.5 为什么**没有**做"运行中的定期对账"

原计划里还有一条"在 `OnTimer` 里周期性地用 `activeRealPosition` 校正 `pairTotalVolume`"（自愈下面两个场景）：

- 算法单在策略看到终态之前就被 `AlgoContext` 删掉（`AlgoContext.cpp:1906-1915` 那一支）；
- 交易所侧被人工平仓。

**本次没有实现，因为它会是一次降级而不是修复**：

| | 新鲜度 | 来源 |
|---|---|---|
| `pairTotalVolume` | **更快** | 算法单回调，同线程直接调用（`PairTradingContext.cpp:944` → `UpdateOnAlgoOrder`） |
| `activeRealPosition` | 更慢 | pubsub 持仓推送，跨进程 |

也就是说**运行期间 `pairTotalVolume` 比 `activeRealPosition` 更新**。拿后者去覆盖前者，等于在每次成交后
用一拍之前的旧值把账本往回推 —— 不但没修好，还会引入抖动。启动时之所以反过来（以推送为准），
是因为那一刻**账本是陈旧的**（最多 10s 前 + 进程停机期间无人记账），而推送才是新的。

> 还要叠加一个语义边界：主动腿**成交到一半**时（主动腿成了、被动腿还挂着），
> `pairTotalVolume` 与 `activeRealPosition` 都在动但可能差一拍，属于合法的中间态。

**要真做自愈，触发条件得比"定期"更严**：`!hasActiveAlgoOrder`（无在途单）**且**该对子已静默 N 秒
**且**偏差持续存在（不是单次采样），才允许覆盖。这是独立任务。

### 5.4 交易所侧的旧子单（孤儿单）

`pre_stop()`（`PairTradingStrategy.cpp:79-91`）**不撤任何单**（`docs/algo_order_source_review.md` §3.4）。所以：

| 场景 | 结果 |
|---|---|
| 优雅停机（`pre_stop` 跑到） | 交易所侧可能仍有上一轮挂着的子单 → 重启后与新报单**叠单**，同一对子出现两套报价 |
| `SIGKILL` / 崩溃 | 同上，且连快照都是最多 10s 前的 |

今天**撤不掉**（见 §3.1）。分两步：

- **短期（✅ 已实现）**：`HandleOrphanAlgoOrders()` —— 启动时若快照里 `hasActiveAlgoOrder == true`，
  `LOG_ERROR` 告警（带 `algoOrderId`），并置 `errorFlag = true` 冻结该对子（`freezeOnOrphanAlgoOrder` 默认 true），
  等运维 `PairCmd_RESUME` 复活。见 §5.2.4。
  > 告警里的"飞书播报"部分**没做** —— 仓库里 `rLarkMsg.Push` 的调用点在别处（`docs/thread_split_design.md:162` 提到），
  > 这里只落了 `LOG_ERROR`。要接飞书需要补一次调用。
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

**问题 1（必要）：没有清除路径 —— ⚠️ 代码已加，但当前不可达。**
`ApplyCommand`（`PairInfoManager.cpp:703-750`）原来四个分支都不碰 `errorFlag`，
连 `PairCmd_RESUME` 也不清。置位后只能重启进程 —— 而快照若又把它恢复了，连重启都救不回来。

> ⚠️ **2026-09-27 复查结论：`ApplyCommand` 零调用点，所以下面的分支走不到。**
> `PairTradingStrategy::on_command`（`:102-104`）只转发给 `algoContext.OnCommand`，
> 而后者（`AlgoContext.cpp:233`）**整个函数体被 `/* */` 注释掉** —— 是空函数。
> → `PairCmd_RESUME` 发不出去 → **`errorFlag` 目前没有可达的清除路径**。
> 唯一出路是删快照 + 重启（安全，但未文档化）。详见 §6.1。

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

**问题 2（已澄清）：触发条件不会误报 —— 因为 `UpdateOnBalance` 那两条写入其实是空转。**

> **2026-09-27 更正**：上一版这里写"`balance.total` 已确认是有符号净持仓"，**这个前提是错的**。
> 查 tb 侧的生产方：两条腿的余额推送里 `total` 都是**账户权益**，不是持仓 ——
> Binance USDT 本位是 `B.a` 的 `wb`（walletBalance，`BinanceUFWsTrade.cpp:616-617`），
> Gateio US 是 futures account 的 `balance`（`GateioUSWsTrade.cpp:625-626`）。
> 而 `currency` 是**保证金资产**（两条腿都是 `"USDT"`），
> 于是 `symKey = "BINANCE.USDT_SWAP.USDT-USDT"` 与 `activeInstrumentKey = "…DOGE-USDT"`
> **匹配不上** → `PairInfoManager.cpp:510-518` 那两行写入**从来没执行过**。
>
> 所以"两个写入方抢同一字段"**当前不成立** —— 实际只有 `UpdateOnPosition` 一个写入方，
> `activeRealPosition` / `passiveRealPosition` 的口径是干净的。
> 但 `UpdateOnBalance` 是一颗**地雷**（`total` 是上万量级的权益，一旦匹配上会连环炸
> 对账 → `pairTotalVolume` → `errorFlag`），**修法是删掉这两行写入**，见 §6.2 ①。

**问题 3（设计）：要不要进快照？—— ✅ 已实现，保持"进"。**

| 选择 | 后果 |
|---|---|
| 进快照 | 重启后该对子仍判死，需运维复活。安全。**但注意 §6.1：清除路径当前不可达，实际要靠删快照 + 重启** |
| 不进快照 | 重启后自动复活，带着未解决的敞口继续交易。危险 |

→ 保持**进快照**（已在第 30 列实现）。它是"更保守"的那一侧，而保守方向的问题只是
"需要人工介入"，激进方向的问题是"带敞口裸奔"。

### 5.6 落地清单（按依赖顺序）

| # | 动作 | 文件 | 状态 |
|---|---|---|---|
| 0 | **前置**：修 `UpdateOnPosition` 的 key 分隔符（`","` → `"."`），否则 `Position` 路整条是死的（§5.3.4） | `PairInfoManager.cpp:193` | ✅ **已完成** |
| 1 | 加 `PairCmd_RESUME` 清 `errorFlag`（§5.5 问题 1），这是 `errorFlag` 进快照的前提 | `PairInfoManager.cpp:481-490` | ✅ **已完成**（未编译验证） |
| 2 | 把 `LoadFromCSV` 换成 `LoadSnapshot`：真解析 line、按 `pairInstrumentKey` 查表、写回、返回恢复数 | `PairInfoManager.cpp` | ✅ **已完成**（未编译验证） |
| 3 | 修 `Init` 里的反向判断（成功打 INFO、失败打 WARN） | `PairTradingContext.cpp:81-90` | ✅ **已完成** |
| 4 | 修配置路径 + 启动建目录 | `etc/config.json:25`、`EnsureParentDir` | ✅ **已完成** |
| 5 | 加 `SaveSnapshot`（原子写），在 `pre_stop` / `OnTimer` 调用，周期收紧到 10s | `PairInfoManager.cpp`、`PairTradingStrategy.cpp:80-82`、`PairTradingContext.cpp:1037-1043`、`PairTradingContext.h:66` | ✅ **已完成**（未编译验证） |
| 6 | 加启动状态机（Reconciling / Trading 两态闸门 + 五处入口早退） | `PairTradingContext.h:137-176`、`.cpp:93-106`、`:152-161`、`:1029-1040` | ✅ **已完成**（未编译验证） |
| 7 | 加 `pairTotalVolume` 启动对账 | `PairTradingContext.cpp:1113-1187`（`ReconcilePair` / `TryReconcile`） | ✅ **已完成**（未编译验证） |
| 7b | 运行中的**定期**对账 | — | ❌ **决定不做**，见 §5.3.5（会是用更旧的推送覆盖更新的账本） |
| 8 | 孤儿单启动告警 + 冻结 | `PairTradingContext.cpp:108-132`（`HandleOrphanAlgoOrders`） | ✅ **已完成**（飞书播报未接，只落 `LOG_ERROR`）；⚠️ 冻结默认值建议改 `false`，见 10b |
| 9 | **删掉** `UpdateOnBalance` 里对 `activeRealPosition`/`passiveRealPosition` 的写入（§6.2 ① —— 是删写入，不是让它匹配上） | `PairInfoManager.cpp:510-518` | ⬜ **建议修**（当前是空转，无害但危险） |
| 10 | **接通 `ApplyCommand` 的调用点**：在 `OnTimer` 顶部（启动闸门之前）轮询指令文件 —— 对齐祖先 `load_manual_pair_info`（§6.1.1）。**不用 `OnCommand`**（已废弃） | `PairTradingContext.cpp:1033`、`PairInfoManager` 新增 `LoadCommands`、`etc/config.json` 新增 `commandPath` | ⬜ **建议做** —— 没有它，`errorFlag` 无法清除 |
| 10b | 在 10 接通之前，把 `freezeOnOrphanAlgoOrder` 默认值改成 `false`（只告警不冻结，§6.2 ②） | `PairTradingContext.h:75` | ⬜ **建议修** |
| 11 | 把启动闸门的 `LOG_ERROR`（每 30s，`PairTradingContext.cpp:1188`）接飞书 —— 闸门不设超时，只能靠告警兜底（§6.2 ④） | `PairTradingContext.cpp:1188` | ⬜ |

> **第 2-5 项的验证状态**：序列化往返已用独立 harness 验过（35 项断言全过，§5.1.5）；
> 但**整个工程还没编译过** —— 本机没有 `cmake`，也没有 `fmt`（`DataStruct.h:9` 依赖 `fmt/core.h`），
> 且 `build/CMakeFiles/utrade_hft.dir/flags.make` 里的 include 路径是部署机的 `/workspace/...`。
> 请在部署机上 `make` 一次再上线。

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
- **快照格式与落地**：`SaveSnapshot`/`LoadSnapshot` 已实现，33 列、原子写、NaN 往返（§5.1.5）。✅ 已改（未编译验证）
- **快照周期 = 10s**（原 300s，`PairTradingContext.h:66`）。✅ 已改
- **快照路径**：`etc/config.json` 的 `csvStatePath` 从 `"./csv"`（是个目录）改为 `"./data/pair_info.csv"`，并由 `EnsureParentDir` 自动建目录。✅ 已改
- **启动状态机**：`Reconciling → Trading` 两态闸门 + 五处入口早退 + 启动对账（§5.2）。✅ 已改（未编译验证）
- **等待上限 = 一直等 + 每 30s 告警，不超时放行**（原待决策第 2 条）。✅ 已定
- **对账容差** `reconcileTolRatio = 1e-6`（相对），另加符号必须相同。✅ 已定
- **孤儿单短期动作**：启动告警 + `errorFlag` 冻结（`freezeOnOrphanAlgoOrder` 默认 true），§5.4。✅ 已改
- **运行中的定期对账**：**决定不做**（会是用更旧的推送覆盖更新的账本，§5.3.5）。要做得用更严的触发条件，属独立任务。

### 6.1 ⚠️ 先看这一条：`errorFlag` 目前没有**可达**的清除路径

这是本次排查中新发现的、优先级最高的一条 —— 它同时改写了"孤儿单冻结"和"`errorFlag` 进快照"的结论。

```
置位：CheckExposureAbnormal（PairTradingContext.cpp:468）
      孤儿单冻结      （PairTradingContext.cpp:127）
清除：ApplyCommand 的 PairCmd_RESUME（PairInfoManager.cpp:722-725）  ← 唯一一处
恢复：LoadSnapshot（PairInfoManager.cpp:327）                          ← 只是从文件读回
```

而 **`ApplyCommand` 在整个工程里零调用点**（只有 `PairInfoManager.h:82` 的声明和 `.cpp:703` 的定义）：

```
PairTradingStrategy::on_command(json)   （PairTradingStrategy.cpp:102-104）
  └─> algoContext.OnCommand(json)        （AlgoContext.cpp:233）
        └─> 整个函数体被 /* */ 注释掉 —— 空函数
```

（`PairTradingStrategy.cpp:118` 那处 `OnCommand("")` 同理，且它自己就是死代码。）

**后果**：`PairCmd_RESUME` **发不出去** → 我这次加的清除分支**不可达** →
`errorFlag` 一旦置位（无论来自敞口异常还是孤儿单冻结），**唯一出路是删掉快照文件 + 重启**。

删快照这条路本身是安全的（快照没了 → `hasActiveAlgoOrder` 读不到 → 不冻结；
`pairTotalVolume` 归 0 → 启动对账会用实时持仓重建），但它是**未文档化、靠人猜**的操作。

### 6.1.1 `ApplyCommand` 的接入点：轮询指令文件（对齐祖先）

`OnCommand` 这条路已确认**不再使用**（`AlgoContext.cpp:233` 的函数体全被注释）。
查框架后可以确定：**没有任何现成的"策略指令"入站通道** ——
`BaseStrategy::heavy_work`（`base_strategy.h:80-111`）是唯一入口，它只按
`crypto::convert_rcmd_2_{ordertrade,balance,position,total_account}` 分派，
而 `RCommand` 的 `cmdTypeEnum`（`pubsub_protocol.h:8-23`）里**没有**策略指令类型；
`rLarkMsg` 是**纯出站**。所以要自己接一条。

**祖先的做法就是答案。** `cc_pricespread_gb_ltp.py:1094-1098`，在 `on_timer` 每个 tick 里：

```python
# 定期读取手动指令文件,按照指令进行同步pair_info
load_manual_pair_info(self)
# 存储pair_info
self.pair_info.to_csv(f"./pair_info/{self.strategy_name}.csv", index=True)
```

`load_manual_pair_info`（`utility/pair_info_manager.py:315-341`）读的是
**`./pair_command/{strategy_name}_command.csv`** —— 运维写文件，策略轮询应用。

→ **建议接入点：`PairTradingContext::OnTimer` 顶部（`PairTradingContext.cpp:1033`），
启动闸门之前，周期轮询一个指令文件。**

| 祖先 | C++ 侧对应 |
|---|---|
| `on_timer` 里 `load_manual_pair_info(self)` | `OnTimer` 顶部 `pim.LoadCommands(cfg.commandPath)` |
| 紧随其后的 `pair_info.to_csv(...)` | 第 6 步 `pim.SaveSnapshot(cfg.csvStatePath)`（`:1106-1111`） |
| `./pair_command/{strategy_name}_command.csv` | `etc/config.json` 新增 `commandPath`（与 `csvStatePath` 同目录） |

**为什么必须放在启动闸门之前**：闸门**不设超时**（§6.2 ④）。若某个对子因为持仓订阅挂掉
而卡在 `Reconciling`，把命令轮询放在闸门之后就等于**连 `RESUME` 都发不进来** —— 死锁。
放在闸门之前，`RESUME` / `STOP` / `CLOSE` 始终可达。

**文件格式**（一行一条指令，直接映射到 `ApplyCommand(pairKey, cmd, params)`）：

```
pairInstrumentKey,command,param,value
BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT,RESUME,,
BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT,STOP,,
BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT,MODIFY,profit,0.001
```

- `pairInstrumentKey` = config 里那条 `A|B` 全串（`PairInfoManager.cpp:61` 的 `m_pairInfoMap` 键，
  也是 `SaveSnapshot` 的第 0 列），运维可以直接从快照文件复制。
- `command` ∈ `STOP` / `CLOSE` / `RESUME` / `MODIFY`（`PairInfo.h:16-22`）。
- ⚠️ `MODIFY` 目前**实际只有 `profit` 生效**：`maxVolume` / `ttTargetVolume` / `mtTargetVolume`
  会被 `RecalcVolumeParams` 在 60s 内覆盖（§5.1.3），所以别把它们写进指令文件当长期配置。

**一次性 vs 声明式 —— 建议一次性（应用后删文件）**

祖先那句 `os.remove(f"./pair_command/{ctx.strategy_name}.csv")`（`:341`）路径**少了 `_command`**，
与它检查/读取的 `{name}_command.csv` 不是同一个文件 → 实际是**声明式**：文件一直在，每 tick 重放。
（祖先的 `update_columns` 是 40 列宽表，声明式当"参数面板"用是合理的。）

C++ 侧建议改成**一次性**：

- 只有 4 个输入位（`autoFlag`/`stopFlag`/`closeFlag`/`profitPct`），没有宽表要维护；
- 声明式会让"文件永远赢" —— 运维改完忘删，之后连**快照恢复的 `autoFlag`** 都会被文件覆盖回去；
- 一次性天然去重，一条 `RESUME` 不会被每 tick 重放（虽幂等，但日志会刷屏）。

**防半截文件**：与 `SaveSnapshot` 同一套 —— 读失败（或列数不符）就**跳过本轮且不删文件**，
让运维有机会修正；只有成功应用后才 `std::remove`。

> 备选方案（不推荐）：走 `rcmdQueue`。需要新增 `cmdTypeEnum` 值 + `convert_rcmd_2_*` +
> 在 `BaseStrategy::heavy_work` 里加分支 —— 而 `base_strategy.h` 是**所有策略共享**的基类，
> 且 `on_*` 全是纯虚函数（加一个就要所有子类实现）；还要 tb 侧能发出这个新类型。
> 换来的是"推送无轮询延迟"，但轮询延迟本来就 ≤ `timerInterval`（1s），不值这个改动面。

### 6.2 剩余四条：不修会不会出问题

| # | 项 | 不修的后果 | 建议 |
|---|---|---|---|
| 1 | **持仓字段收敛到单一来源** | **当前无害**，但留着是地雷 —— 见下 | **修（删写入），优先级中** |
| 2 | **孤儿单冻结默认 `true`** | 误判一次 = 删快照 + 重启（因为 §6.1） | **改默认 `false`（只告警）** |
| 3 | **`errorFlag` 进快照** | 不进 = 已判死的对子静默复活（更危险） | **保持"进"**，已实现 |
| 4 | **放行延迟最多 1s** | 1s 内不报单、不做风控；强平也晚 1s | **不改**，但要保证告警有人看 |

**① 为什么 `UpdateOnBalance` 当前无害，却是地雷**

它拼 key 的方式是 `currency + "-" + baseAsset`，而 `baseAsset` 是**硬编码** `"USDT"`
（`PairTradingContext.h:255`，不从 config 读）。而两条腿的余额推送里 `currency` 都是**保证金资产**：

| 交易所 | `currency` 来源 | `total` 来源 |
|---|---|---|
| Binance USDT 本位 | `ACCOUNT_UPDATE` 的 `B.a`（asset）= `"USDT"`（`BinanceUFWsTrade.cpp:616`） | `B.wb` = **walletBalance**（`:617`） |
| Gateio US 本位 | futures account 的 `currency` = `"USDT"`（`GateioUSWsTrade.cpp:625`） | `balance` = **账户余额**（`:626`） |

→ `symKey = "BINANCE.USDT_SWAP.USDT-USDT"`，而 `activeInstrumentKey = "BINANCE.USDT_SWAP.DOGE-USDT"`
→ `strstr` **匹配不上** → 这两行写入实际是**空转**（`PairInfoManager.cpp:510-518`）。

**地雷在于**：`balance.total` 是**账户权益**（正数、USDT 计价、量级上万），**不是持仓**。
一旦有人为了"让它生效"去调整拼接顺序（`symbol` 应为 `baseAsset + "-" + currency` 才可能匹配 DOGE-USDT），
`activeRealPosition` 会被写成几万 → 启动对账当场判为不一致 → `pairTotalVolume` 被改成几万 →
`CheckExposureAbnormal` 的净敞口立刻爆表 → `errorFlag`。**三处连环炸。**

所以第 9 项的修法**不是"让它匹配上"，而是删掉这两行写入**（`UpdateOnBalance` 只管余额类字段；
`PairInfo` 目前也没有余额字段，等于整段可以先只留告警或直接空掉）。

**② 为什么建议把 `freezeOnOrphanAlgoOrder` 改成 `false`**

误判窗口本身很窄：`pre_stop` 是优雅停机，快照记的就是停机瞬间的真值；若那时真有在途单，
交易所侧确实可能有活着的子单，冻结是**对的**。真正的误判只发生在
"单已终态、但 `ClearActiveAlgoOrder` 还没跑"（`ScanFinishedAlgoOrders` 每 200ms 一次）——
窗口 ≤200ms。

但**代价**因为 §6.1 从"发一条 RESUME"放大成"删快照 + 重启"。在命令链路接通前，
建议先只告警（`LOG_ERROR` 已经打全了 `pairKey` / `algoOrderId` / `pairTotalVolume`），
把"要不要冻结"交给人工判断。

**④ 真正的风险不是那 1s，而是"一直等"**

`TryReconcile` 只在 `OnTimer` 里试（tick 默认 1s），所以放行最坏晚 1s —— 这个无所谓。
要紧的是**闸门不设超时**：如果持仓推送和批次标记一直不到（比如持仓订阅挂了），
闸门**永远不开** → 该对子彻底停摆，**连风控强平也不执行**。

这是刻意的取舍（按"无持仓"放行的后果是在孤儿仓上重复开仓，更糟），但要保证
`PairTradingContext.cpp:1188` 那条每 30s 的 `LOG_ERROR` **有人看**（接飞书）。

### 6.3 仍需拍板（重排后）

1. **接通 `ApplyCommand` 的调用点**（§6.1.1）—— 建议在 `OnTimer` 顶部轮询指令文件（对齐祖先
   `load_manual_pair_info`），**不用**已废弃的 `OnCommand`。这是下面 2、3 的前提。
2. **`freezeOnOrphanAlgoOrder` 默认值**改成 `false`（只告警）？（§6.2 ②）
3. **删掉 `UpdateOnBalance` 里对 `activeRealPosition`/`passiveRealPosition` 的写入**（§6.2 ①）
   —— 注意：是**删写入**，不是让它匹配上。
4. **孤儿单中期动作**：在途单 id 持久化 + 启动撤单，什么时候做（§5.4）？
5. **`pairActiveTotalPrice` / `pairPassiveTotalPrice` 保不保？** 本次实现的选择是：
   **交易所空仓时复位成 `-1.0`；仍持仓时保留快照值**（§5.2.3）。认可吗？

**已解决（2026-09-29）**：`CheckSpreadNoRegression` 的"永远无条件强平"缺陷。原判据依赖
`smallStats`/`openSmallSpread*`（无生产者 + 轴错配）→ `isRegressed` 恒 false → 持仓满 24h
必然强平。已重写为复用执行端平仓阈值（`SignalGenerator::CloseSpreadReached`，与
`CheckSignalForSatisfy` 的 ttCL/mtCL/ttCS/mtCS 同源同轴同成本口径），并新增
`RiskConfig::minHoldDurationUs`（默认 1h）最短持有期门槛 —— 未满最短持有期一律不触发，
且**不启动**"连续未回归"计时。验证：`/tmp/riskcheck/spread_regress.cpp`（22 项断言全过，
编译真实 `RiskManager.cpp` + `SignalGenerator.cpp`）。

**顺带查出的两个新缺陷（未修）**：见 `docs/ancestor_pair_trading_gateio_review.md` 第七节后的批注
与 `.workbuddy-ai/memory/2026-09-29.md` —— ① `OnAlgoFinished` 对**任何**终态算法单都
`incrTimes`，普通单会污染已平息的风控档位；② `CheckADLRisk` / `CheckFundingAbnormal`
没有"条件消失就复位"分支。
   —— 残余风险是"宕机期间被平掉又重开"时均价陈旧，需要人工看一眼 `LOG_ERROR`。
6. **上线回归重点**：`Position` 路修好后，`CheckExposureAbnormal` / `SignalGenerator:139` /
   `ProcessModify:545` 三条**原本不可达**的路径同时变活（§5.3.4）。需要确认它们的阈值在真实
   行情下不会误触发 —— 尤其 `errorFlag` 的 `4 × ttTargetVolume`。
