# 祖先项目 `pair_trading_c_gateio` 实现方式梳理 —— 对照 `utrade_hft` 移植现状

> 目的：算法单创建是从 `pair_trading_c_gateio` 移植过来的。在补 `largeStats` 统计生产者之前，
> 先把祖先的完整链路、常量、字段语义梳理清楚，再逐项对照当前 C++ 移植缺了什么。
>
> 结论一句话：**C++ 移植把祖先的"期望价差层"（Stage 2/3/4）整层丢掉了**，
> 直接从分位数一步跳到 StartSpread。当前 `RecalcOrderParams` 只有祖先约 1/4 的逻辑。
>
> 本轮**未改动任何源码**，只做阅读与对照。

---

## 〇之前、总体逻辑一致性判定（先看这个）

**问题**：抛开 active/passive 这类可配置细节，只看"根据实时价差行情开仓平仓"这个总体逻辑，
两边是否一致？

**答案：骨架一致，闭环成立；但有 4 处会让"门槛松紧"和"触发时机"跑偏的实质差异。**

### 一致的骨架（8 项）

| 环节 | 祖先 | C++ |
|---|---|---|
| ① 用统计定区间 | 24h 窗口分位数 `quantile(0.92)/quantile(0.08)` | `largeStats` 的 8 个分位数（**生产者待补**） |
| ② 8 组信号 | `{tt,mt} × {OL,OS,CL,CS}` | 同 |
| ③ 目标价差 = 分位数 × 收缩系数 ± 成本 | `× spread_adj_pct` | `× spreadAdjPct` ✓ |
| ④ 触发 = 实时价差 vs 目标价差 | `_n_org < target` / `_n_org > target` | `rt.spreadXxx < StartSpread` / `> StartSpread` ✓ |
| ⑤ 方向 | OL/CS 走 DQ 配 `<`；OS/CL 走 UQ 配 `>` | 同 ✓ |
| ⑥ 开仓方向 | OL = 主动腿空 + 被动腿多（价差极低时进） | 同 ✓ |
| ⑦ 阶梯 | `StartVolume → EndVolume` 线性插值，越极端量越大 | 同 ✓ |
| ⑧ 平仓 = 持仓方向 + 价差回归 + 盈利要求；开平优先级 平仓 > 开仓 | — | 同 ✓ |

结论：**"统计定区间 → 分位数推目标 → 实时比目标 → 报单 → 阶梯执行 → 回归平仓"这个闭环，
C++ 是完整存在的，结构上没有走偏。** 前面提的 active/passive、`BidAsk`↔`AskBid` 都属于
"可配置/可镜像"的范畴，不影响闭环成立。

### 4 处实质差异（会让行为跑偏）

| # | 差异 | 后果 |
|---|---|---|
| **D1** | **触发侧用瞬时值，祖先用 Tema**。`CheckSignal:177-215` 全用 `rt.spreadXxx`；祖先的 `*_n_org` 用 `spreadXxxTema`（`targetSpreadType = NOW_MEAN`）。`rt.spreadXxxTema` 字段填了但**信号侧一次没读**（只有 `AlgoPairOrder` 拆单时读）。 | 信号会被单 tick 毛刺触发，报单频率和入场点都会更差 |
| **D2** | **OL/OS 的目标价差少取一路**。祖先是 `min(AskAskDQ路, BidBidUQ路)` / `max(...)`；C++ 各只取一路（`:87` 取 askAskDQ、`:106` 取 bidBidUQ）。 | 开仓门槛**偏松**——两路取 min/max 本意是取更保守的那个 |
| **D3** | **成本项的净组合方向相反**。祖先 long 侧净 **≈ −0.00065**（`+费率≈+0.00005`，再 `−openProfitPct 0.0004`、`−tt_add_percent 0.0003`）；C++ 净 **≈ +0.0016**（`totalCost` 把两腿 taker 费 + 滑点 + ttExtra 全加）。**单项符号结构其实是一致的**（long 侧 `+cost`、short 侧 `−cost`），差在量级和"该减的没减"。且 `openProfitPct` 在 `:44` 声明后从未使用。 | 门槛量级差 ~0.002，远大于价差本身的 1e-4 量级 → **C++ 的开仓门槛基本失去筛选作用** |
| **D4** | **funding 调整 + `adj_profit` 整层缺失**。前者影响"funding 要亏钱时应该要求更好的价差"；后者影响"持仓越满越愿意平仓"。 | 开仓时机少一层保护；平仓时机没有仓位感知，容易攒满仓不平 |

另外，**统计生产者缺失**（`largeStats` 无人写）导致 ①②③ 目前全是死代码 —— 这是 D1~D4 之前的前置项。

### 修正说明

我在本文档第一版里把 D3 写成了"祖先费用项让条件变紧、C++ 让条件变松"，
**这个说法不准确**。核对后：两边"单项符号结构"是一致的（long 侧都是 `+cost`），
真正的差异是**净组合**（祖先靠 `openProfitPct` 和 `tt_add_percent` 把净值拉到负，
C++ 没有这两个负项，且 taker 费取值更大）。上表 D3 已按核对结果改写。

---

## 〇、项目性质与文件地图

`pair_trading_c_gateio` 名字里带 `c`，但**实际是 Python 项目**（pandas 向量化），不是 C++。

| 文件 | 行数 | 作用 |
|---|---|---|
| `cc_pricespread_gb_ltp.py` | 1128 | **主策略**。`on_timer()` 里完成全部参数计算 + 报单 + 撤单 + 改参 |
| `cc_pricespread_gb_ltp_rever.py` | 1109 | 变体（含 Tema 版 `*_spread_n_*` 的完整推导，注释最全，**建议以它为准读逻辑**） |
| `cc_pricespread_gb_ltp_02.py` / `_03.py` | ~1128 | 小改版本，逻辑同 `_ltp.py` |
| `utility/pair_info_manager.py` | 501 | `init_pair_info` / 分位数统计 / `calculate_pair_order_params`（**移植的核心源头**） |
| `utility/algo_order_manager.py` | 307 | `create_algo_pair_order`（算法单字段装配）、`create_modify_dict` |
| `utility/data_helper.py` | 164 | `update_spread_df`（**价差样本累积 = 统计生产者**） |
| `utility/algo_data_struct.py` | 497 | `AlgoPairOrder` 数据结构 |
| `strategy_processer_cc.py` | — | 线程编排（strategy / timer / dbp / db / aec / utrade / nano） |

线程模型：`timer_run()` 起一个定时线程跑 `on_timer()`；`dbp_run()` 收行情写 `temp_dbp_data_list`。
**统计和参数计算全部在 `on_timer()` 这一个函数里顺序完成**，没有跨线程共享问题。

---

## 一、祖先的完整链路（五段）

```
dbp 行情 tick
  └─ on_dbp_data()            # 每个 tick：8 个价差字段 × -1，追加到 temp_dbp_data_list
                              #            同时更新 last_spread（最新快照）

on_timer()（定时线程，全流程串行）
  ├─ Stage 1  分位数统计       # 每 60s：把 temp_dbp_data_list 合并进 spread_df（保留 24h）
  │                           #         对 24h 窗口求 8 个分位数 + count + active_depth_volume
  │                           #         → update_pair_info_by_spread_df()
  │
  ├─ Stage 2  期望锁定价差     # 每个 tick：分位数 × spread_adj_pct ± 费率 ± 滑点 ± funding
  │                           #         产出 mt_except_open_{long,short}_spread_{q_org,q_open,q,n_org,n_open,n}
  │                           #         + spread_span
  │
  ├─ Stage 3  开仓目标价差     # 每个 tick：Stage2 两路取 min/max，再叠加 openProfitPct、
  │                           #         funding 调整、min_spread_target 夹逼
  │                           #         产出 mt_except_open_{long,short}_spread
  │
  ├─ Stage 4  TT 由 MT 平移    # 每个 tick：± tt_add_percent
  │                           #         产出 tt_except_open_{long,short}_spread{,_q}
  │
  ├─ Stage 5  calculate_pair_order_params()   # 每个 tick：Start/EndSpread + Start/EndVolume + 4 组开关
  │
  └─ 报单/撤单/改参            # 优先级：手动 > 平仓 > 开仓；撤单 150s 超时 / just_close / 移仓
```

关键点：**Stage 2~5 每个 tick 都重算**（因为依赖 `*Tema` 实时值），只有 Stage 1 是 60s 一次。

---

## 二、常量对照表（这是移植偏差最集中的地方）

| 参数 | Python 祖先 | C++ `utrade_hft` | 备注 |
|---|---|---|---|
| `quantile_up` / `quantileUp` | **0.92** | 0.9 | |
| `quantile_dn` / `quantileDn` | **0.08** | 0.1 | |
| `spread_adj_pct` / `spreadAdjPct` | **0.8** | 0.95 | 分位数向 0 收缩 |
| `min_spread_span` / `minSpreadSpan` | **0.0002** | 0.0003 | |
| `tt_add_percent` / `ttAddPercent` | 0.0003 | 0.0003 | ✓ |
| `openProfitPct` | **0.0004** | 0.0001 | |
| `min_profit_target` | **0.0004** | 无 | 祖先用来 gate 开仓 |
| `profitPct` | 0.00025 | `pi.profitPct` | 平仓期望利润 |
| `open_max_fundingrate` / `openMaxFundingRate` | 0.002 | 0.002 | ✓ |
| `activeMakerFeeRate` | **−0.00015**（GATEIO maker 返佣！） | 0.0002 | **符号相反** |
| `activeTakerFeeRate` | 0.0002 | 0.0006 | |
| `passiveMakerFeeRate` | **0.00002** | 0.0002 | |
| `passiveTakerFeeRate` | 0.0002 | 0.0006 | |
| `basic_slippage` / `basicSlippage` | **0.00025** | 0.0001 | |
| `slippage_pct_minmove` / `slippagePctMinMove` | **1** | 0.5 | |
| `min_spread_target` / `minSpreadTarget` | 0 | 0.0 | ✓ |
| 统计窗口 `spread_df_update_period` | **24 h** | largeStats 注释 24h / smallStats 1h | 祖先**只有一个窗口** |
| 统计刷新 `spread_df_update_timespan` | **60 s** | `spreadStatsUpdateIntervalSec` **3600** | |
| 样本数门槛 `spread_need_percent` | **0.5 → 需 ≥8640 个样本** | 无 | |
| 行情新鲜度 `lastGenerateTS` | **< 30 s** | 无 | |
| 算法单超时 `algo_order_cancel_time` | **150 s** | `algoOrderTimeoutMs` 30000（**未使用**） | |
| 改参周期 `modify_timespan` | **60 s** | `signalRecalcIntervalSec` 300 | |
| 最大算法单数 `max_algo_orders` | **12** | 无 | 平仓单占比上限 0.7 |
| 同 instrument 单数 `max_same_instrument_orders` | **1** | 无 | |
| K线周期/门槛 | 24h / 300s / 50% → ≥720 根 | `volumeRecalcIntervalSec` 60 | |

> `activeMakerFeeRate = -0.00015` 是最容易踩的坑：它是**返佣**，所以祖先公式里
> `+ activeMakerFeeRate` 实际是**减 0.00015**，让"主动腿做 maker"那一侧更划算。
> C++ 里 `activeMakerFeeRate = +0.0002` 会让同一项**反向**。

---

## 三、字段语义对照 —— active/passive 是**互换**的

这是本次阅读最重要的发现之一。

- **Python**：`activeAccountId = rad_ltp_gt_220_hf_trade`（GATEIO），`passiveAccountId = rad_ltp_bn_470_hf_trade`（BINANCE）
  → **active = GATEIO，passive = BINANCE**
- **C++ `utrade_hft/etc/config.json`**：`pairKeys = ["BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT"]`
  → **active = BINANCE，passive = GATEIO**

**两者 active/passive 正好相反。** 再加上：

- `dbsnap.h:303-306`：`spreadBidAsk = calcspread(activeBid, passiveAsk)`，命名规则 = **主动腿价位 + 被动腿价位**
- `dbsnap.h:232`：`SPCT_PRICEDIV1` → `(d1−d2)/d1`
- Python `on_dbp_data:423-430` 把 8 个价差字段**全部乘 −1**（注释："讲A-B/A变为B-A/A"）
- C++ `PairInfoManager::UpdateRtSpread:141-149` **原样拷贝，不取反**

令 A=GATEIO 价格、B=BINANCE 价格，则两边算出的**分子**对照如下：

| Python 字段 | 分子 | C++ 对应字段 | 分子 | 结论 |
|---|---|---|---|---|
| `spreadBidAsk` | B_ask − A_bid | `spreadAskBid` | B_ask − A_bid | **互换** |
| `spreadAskBid` | B_bid − A_ask | `spreadBidAsk` | B_bid − A_ask | **互换** |
| `spreadBidBid` | B_bid − A_bid | `spreadBidBid` | B_bid − A_bid | 同名对应 ✓ |
| `spreadAskAsk` | B_ask − A_ask | `spreadAskAsk` | B_ask − A_ask | 同名对应 ✓ |

**好消息**：两边经济含义一致（都是 "BINANCE − GATEIO"），符号不冲突，UQ/DQ 方向不用镜像。
**坏消息**：`BidAsk` 与 `AskBid` 在两边是**镜像**的，照抄 Python 字段名会错。
（分母一个是 A_bid 一个是 B_bid，量级近似，不影响方向。）

---

## 四、Stage 2~4 的完整公式（移植时缺的那一层）

以下用**祖先字段名**。`fees_long = +aMaker + pTaker + aMakerSlip + pTakerSlip`，
`fees_short = −aMaker − pTaker − aMakerSlip − pTakerSlip`。

### Stage 2 —— 期望锁定价差（`cc_pricespread_gb_ltp.py:690-769`）

```
# 分位数基准（_q_org），不含 funding
mt_except_open_long_spread_q_org   = spreadAskAskDQ * 0.8 + fees_long
mt_except_open_short_spread_q_org  = spreadBidBidUQ * 0.8 + fees_short

# 开仓用（_q_open），带 2× funding
mt_except_open_long_spread_q_open  = spreadAskAskDQ * 0.8 + fees_long  + 2*open_long_funding_profit
mt_except_open_short_spread_q_open = spreadBidBidUQ * 0.8 + fees_short − 2*open_short_funding_profit

# 平仓用（_q），带 0.5× funding
mt_except_open_long_spread_q       = spreadAskAskDQ * 0.8 + fees_long  + 0.5*open_long_funding_profit
mt_except_open_short_spread_q      = spreadBidBidUQ * 0.8 + fees_short − 0.5*open_short_funding_profit

# 当前可锁定价差（用 Tema 实时值）—— 注意 spread 类型与 target 是交叉的
mt_except_open_long_spread_n_org   = spreadAskAskTema + fees_long
mt_except_open_short_spread_n_org  = spreadBidBidTema + fees_short
mt_except_open_long_spread_n_open  = spreadAskAskTema + fees_long  + 2*open_long_funding_profit
mt_except_open_short_spread_n_open = spreadBidBidTema + fees_short − 2*open_short_funding_profit
mt_except_open_long_spread_n       = spreadAskAskTema + fees_long  + 0.5*open_long_funding_profit
mt_except_open_short_spread_n      = spreadBidBidTema + fees_short − 0.5*open_short_funding_profit

spread_span = spreadBidBidUQ − spreadAskAskDQ     # 与 C++ `:29` 公式一致 ✓
```

funding 调整（`:664-687`，这段 C++ **完全没有**）：

```
open_long_funding_profit  = aFR/aInterval*480 − pFR/pInterval*480
open_short_funding_profit = pFR/pInterval*480 − aFR/aInterval*480
两者再取 min(x, 0)   # funding 赚钱不计入期望收益，亏钱按 1 期计
# 按距离结算时间分档放大亏损预期（largeFundingIndex = |aFR|>0.0015 或 |pFR|>0.0015）
#   0~1h : 0.2 * funding_time_left * max_abs_funding_rate
#   1~2h : 0.2 * max_abs_funding_rate
#   >2h  : 0.3 * max_abs_funding_rate
# 再与 open_{long,short}_funding_profit 取 min  →  *_funding_profit_adj
```

### Stage 3 —— 开仓目标价差（`:774-782`）

```
# 关键：long/short 各自都是「两个来源取 min / max」
mt_except_open_long_spread  = min( mt_except_open_long_spread_q_open,     # AskAskDQ 路
                                   mt_except_open_short_spread_q_org      # BidBidUQ 路
                                     + 2*open_long_funding_profit_adj
                                     − openProfitPct )
mt_except_open_short_spread = max( mt_except_open_short_spread_q_open,    # BidBidUQ 路
                                   mt_except_open_long_spread_q_org       # AskAskDQ 路
                                     − 2*open_short_funding_profit_adj
                                     + openProfitPct )
# 再按 min_spread_target 夹逼（硬性要求 |target| ≥ 5e-5）
mt_except_open_long_spread  = min(mt_except_open_long_spread,  −(min_spread_target + 0.00005))
mt_except_open_short_spread = max(mt_except_open_short_spread, +(min_spread_target + 0.00005))
```

### Stage 4 —— TT 由 MT 平移（`:785-788`）

```
tt_except_open_long_spread   = mt_except_open_long_spread   − tt_add_percent
tt_except_open_short_spread  = mt_except_open_short_spread  + tt_add_percent
tt_except_open_long_spread_q = mt_except_open_long_spread_q − tt_add_percent
tt_except_open_short_spread_q= mt_except_open_short_spread_q+ tt_add_percent
```

注意：祖先的 TT **不是换一套 spread 类型**，而是把 MT 的目标价差平移 ±0.0003（要求更极端）。
源码注释自己也说"这里需要演绎一下, 现在的逻辑不一定对"。

### Stage 5 —— `calculate_pair_order_params`（`pair_info_manager.py:344+`）

```
ttOLStartSpread = tt_except_open_long_spread
ttOLEndSpread   = ttOLStartSpread − 0.000005
ttOLStartVolume = 0 ;  ttOLEndVolume = −maxVolume

ttCLStartSpread = tt_except_open_short_spread_q − adj_profit
ttCLEndSpread   = ttCLStartSpread + 0.000005
ttCLStartVolume = −maxVolume ;  ttCLEndVolume = 0

ttOSStartSpread = tt_except_open_short_spread
ttOSEndSpread   = ttOSStartSpread + 0.000005
ttOSStartVolume = 0 ;  ttOSEndVolume = +maxVolume

ttCSStartSpread = tt_except_open_long_spread_q + adj_profit
ttCSEndSpread   = ttCSStartSpread − 0.000005
ttCSStartVolume = +maxVolume ;  ttCSEndVolume = 0
（MT 同理，用 mt_ 前缀）
```

开关（`pair_info_manager.py:344+` 后半段）：

```
OS_true = mt_except_open_short_spread > 0
OL_true = mt_except_open_long_spread  < 0
tt/mt OL/OS Switch = OL_true/OS_true 且 close_flag == False
tt/mt CL/CS Switch = 始终 True
# 手动模式(auto_flag=False)：OL/OS/CL/CS 全部 False
# funding_limit: |aFR|>open_max_fundingrate 或 |pFR|>... → OL/OS = False
# restrict: active/passive liquid_status>0 或 margin_status>0 → OL/OS = False
```

**这几段与 C++ `RecalcOrderParams:124-154` 结构基本一致**（开关部分移植得不错）。

---

## 五、`adj_profit` —— 平仓盈利要求的动态调节（C++ 完全没有）

祖先 `:803-860`：

```
close_profit = 0
close_long_index  = pairTotalVolume <= −minVolume
close_short_index = pairTotalVolume >= +minVolume
close_profit[close_long]  = mt_except_open_short_spread_n_org − (pairPassiveTotalPrice/pairActiveTotalPrice − 1 + profitPct)
close_profit[close_short] = −mt_except_open_long_spread_n_org + (pairPassiveTotalPrice/pairActiveTotalPrice − 1 − profitPct)
close_profit = max(close_profit, 0)

# 按杠杆占用动态缩放
adj_pct = 0
if position_value_both_max/active_total_pnl > 0.95*leverage 或 passive 同: adj_pct = 0.2, min_profit_target×2, min_spread_span×2, openProfitPct×2
elif 同指标 > 0.9*leverage:                                     adj_pct = 0.1, 全部 ×1.5

adj_profit = min(close_profit * adj_pct, profitPct)   # 再 max(·, 0)
```

然后用于平仓触发：

```
except_close_long_flag  = (mt_except_open_short_spread_n_org > mt_except_open_short_spread_q − adj_profit)
                        & (pairTotalVolume <= −minVolume)
except_close_short_flag = (mt_except_open_long_spread_n_org  < mt_except_open_long_spread_q  + adj_profit)
                        & (pairTotalVolume >= +minVolume)
# 再由 profitSwitch + profitPct 二次过滤（:857-860）
```

C++ 目前是 `start ± (profitSwitch ? profitPct : 0)`（固定 `profitPct`），
**没有 `close_profit × adj_pct` 这一层**，也没有"持仓越满、平仓要求越松"的机制。

---

## 六、C++ 移植现状逐项对照

### ✅ 已经对的

| 项 | 位置 |
|---|---|
| `spread_span = bidBidUQ − askAskDQ` 公式 | `SignalGenerator.cpp:29` |
| 开关结构（OL/OS 看 target 正负 + closeFlag + autoFlag；CL/CS 恒 True） | `:124-140` |
| 流动性 / margin / funding 过大禁开仓 | `:143-154` |
| `EndSpread = StartSpread ∓ 5e-6` 的**绝对值**与 `Start/EndVolume` 配对 | `:49-121` |
| `CheckSignal` 的 8 个实时价差字段 | `:177-215` |
| MT 的 CL/CS 分位数来源 | `:96 / :116` |

> ⚠️ 更正：`EndSpread` 的 **±5e-6 方向 4 个全反了**（见下方 #20 与 `modification_plan.md` Step 0），
> 本文档第一版曾把它列为"已经对的"，那是错的。

### ❌ 缺失或偏差

| # | 问题 | 位置 | 祖先做法 |
|---|---|---|---|
| 1 | **`openProfitPct` 声明了但从未使用** | `SignalGenerator.cpp:44` | Stage 3 里 `− openProfitPct` |
| 2 | **OL/OS 只取了 min/max 的一路** | `:87`（OL 只用 askAskDQ）、`:106`（OS 只用 bidBidUQ） | `min(AskAskDQ路, BidBidUQ路)` / `max(...)` **两路** |
| 3 | **funding 调整整层缺失**（`open_*_funding_profit_adj`） | 全文件 | Stage 2/3 的 `2×` / `0.5×` funding 项 |
| 4 | **`adj_profit` 整层缺失**（`close_profit × adj_pct`） | `:97 / :117` | Stage 5 + `:849-855` |
| 5 | **成本项"单项符号"一致，但"净组合"方向相反** | `:8` `− direction*totalCost`，净 **+0.0016** 量级 | `+费率(≈+0.00005) − openProfitPct(0.0004) ± tt_add_percent(0.0003)`，净 **≈ −0.00065** |
| 6 | `ttExtra` 走 `extraBuffer` 加进 `totalCost` → 让 TT 门槛**变松** | `:48/:57/:67/:77` | `tt = mt ± tt_add_percent` → 让 TT 门槛**更极端**（更难） |
| 6b | **触发侧用瞬时值，不用 Tema** | `CheckSignal:177-215` 全用 `rt.spreadXxx` | 祖先 `*_n_org` 用 `spreadXxxTema`（`TargetSpreadPrice_NOW_MEAN`） |
| 7 | `mtActiveFee = activeTakerFeeRate` | `:39` | MT 主动腿是 **maker**，应 `activeMakerFeeRate` |
| 8 | `mtCS` 传 `isTaker=true`，其他 MT 传 `false` | `:116` | `isTaker` 在 `CalcExpectSpread` 里其实没用到（未使用参数） |
| 9 | **`minSpreadTarget` 夹逼语义不同** | `:124-125` | 祖先夹逼的是 **target**，再 `& (target < −min_spread_target)` |
| 10 | **`spreadAdjPct` 常量 0.95 vs 0.8** | `:28` | 0.8 |
| 11 | **费率表整体对不上**（含 `activeMakerFeeRate` 符号） | `SignalGenerator.h:21-25` | 见第二节 |
| 12 | **统计生产者缺失**（`largeStats`/`smallStats` 无人写） | `PairInfoManager.cpp:171/181` 无调用者 | `update_spread_df` + `update_pair_info_by_spread_df` |
| 13 | 统计窗口/频率/样本门槛全部不同 | `PairTradingContext.h:41` | 24h 窗口、60s 刷新、≥8640 样本、行情 <30s |
| 14 | **`smallStats` 是 C++ 自己发明的**，祖先只有一个 24h 窗口 | `PairInfo.h:196-197` | 单窗口 |
| 15 | `openSmallSpreadBidBidUQ/AskAskDQ` 只在开仓时快照，之后无人读 | `PairTradingContext.cpp:472-479` | 祖先的 `*_q_open` 是 Stage 3 的 min/max 参与者 |
| 16 | `CalcMinMoveSlippage` 定义了但从未调用 | `:13-19` | `activeTakerSlippage = pct*minmove/priceTema + basic_slippage` |
| 17 | `SpreadStats.avgDepthVolume` 从未填充 | `PairInfo.h:50` | `active_depth_volume = (aBidVol+aAskVol)/2` 均值 |
| 18 | 算法单超时 150s → 30s 且未使用；改参 60s → 300s | `PairTradingContext.h:37/44` | 150s / 60s |
| 19 | `max_algo_orders=12` / `max_same_instrument_orders=1` 缺失 | 全项目 | 报单数量控制 |
| 20 | **`EndSpread` 的 ±5e-6 方向 4 个全反** | `:9` `startSpread − direction*5e-6` | 祖先：OL/CS 为 `start−5e-6`、OS/CL 为 `start+5e-6`；`AlgoPairOrder` 的阶梯插值要求与此一致 |

---

## 七、统计生产者应该怎么补（基于祖先）

祖先的做法非常直接，照搬即可：

```
# 数据侧（每个 dbp tick）
temp_dbp_data_list.append({
    generateTs,
    spreadBidAsk, spreadBidBid, spreadAskBid, spreadAskAsk,      # 4 个原始
    spreadBidAskTema, spreadBidBidTema, spreadAskBidTema, spreadAskAskTema,  # 4 个 Tema
    activeFundingRate, passiveFundingRate,
    activeBidVolume, activeAskVolume, passiveBidVolume, passiveAskVolume,
})

# 统计侧（每 60s，Stage 1）
spread_df = concat(spread_df[generateTs > now − 24h], temp_dbp_data_list)   # 24h 滚动窗口
temp_dbp_data_list = []

per pairKey:
    active_depth_volume = (activeBidVolume + activeAskVolume) / 2
    UQ = quantile(0.92) of [spreadBidAsk, spreadBidBid, spreadAskBid, spreadAskAsk]
    DQ = quantile(0.08) of 同上
    spread_count = count(generateTs)
    active_depth_volume = mean(active_depth_volume)
```

移植到 C++ 的建议（**待用户确认后再动手**）：

1. 在 `PairInfo`（或 `PairTradingContext`）里加一个 per-pairKey 的环形缓冲：
   `struct SpreadSample { int64_t ts; double sba, sbb, sab, saa; double abv, aav; };`
   `std::deque<SpreadSample>`，`OnSpread` 里 `push_back`，超 24h 的 `pop_front`。
2. `PairTradingContext::OnTimer` 里按 `spreadStatsUpdateIntervalSec` 触发：
   拷贝一份样本 → 排序 → 取分位数 → 组装 `SpreadStats` → `UpdateLargeStats(pairKey, st)`。
3. 分位数：`include/statistic.h` 的 `Statistic<T>::percentile(double p)` 是 **private 且 0~100 标度**。
   两个选择：(a) 改成 public 并传 `p*100`；(b) 在统计生产者里本地实现一份 nearest-rank。
   **建议 (b)**，不动公共头文件，避免影响其他模块。
4. 样本门槛：`count >= 24*3600/5*0.5 = 8640`，否则 `valid = false`。
   同时把 `rt.valid` 改为按 `now − lastGenerateTs < 30s` 判定（现在是无条件 `true`，见 `PairInfoManager.cpp:155`）。
5. 补完后把 `RecalcOrderParams` 挪到 `RecalcVolumeParams` 之后立即执行
   （`SignalGenerator.h:67` 的注释本来就是这么写的，现在 `PairTradingContext.cpp:495-506` 是分开两个周期）。
6. `smallStats` 是否保留？祖先没有。**建议先只做 largeStats（24h）**，
   `smallStats` 留空但把 `IsValid()` 依赖去掉，避免 `openSmallSpread*` 那条支路悬空。

---

## 八、建议落地顺序

1. **先补统计生产者**（第七节）——否则 `RecalcOrderParams` 第 25 行永远 return，前面所有修复都看不到效果。
2. **补 Stage 2/3/4 缺失层**（第六节 #1~#5、#9~#11）——这是"期望价差"的灵魂。
3. **对齐费率/常量表**（第二节），尤其 `activeMakerFeeRate` 的符号。
4. **补 `adj_profit` 与 `close_profit`**（第五节）。
5. 最后再处理算法单终态判定、撤单通路（见 `algo_order_source_review.md` 的三/四节）。

---

## 九、待确认

- 祖先 `active=GATEIO / passive=BINANCE`，C++ `active=BINANCE / passive=GATEIO`，
  是**有意调换**（因为资金/账户在不同交易所）还是移植时写反了？
  这决定了 `BidAsk`↔`AskBid` 要不要镜像。
- `spreadcalctype`：`dbp/etc/dbprocess.xml` 写 1、`dbp/etc/dbprocess.json` 写 0。
  用户已定"以 0 为准"。本节结论基于 0 推导，符号链条是自洽的。
- 祖先 TT 用「MT ± tt_add_percent」启发式，C++ 改用 BidAsk/AskBid 实际交叉价差。
  C++ 的做法在经济上更合理，但**语义已经和祖先不同**，需要确认是否有意为之。
