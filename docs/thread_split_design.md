# 算法单创建 / 执行分线程方案

> 目标：行情到达时执行得足够快；算法单创建是低频动作，不应该拖累执行路径。
> 本文档只做分析给方案，**不改任何代码**。

---

## 1. 现状：一个线程干所有事

### 1.1 线程清单

| 线程 | 入口 | 职责 |
|---|---|---|
| main | `src/main.cpp:80-83` | 只跑 `log_maintain()`（`fmtlog::poll()`，把日志刷盘） |
| **策略主线程** | `include/base/base_strategy.h:59-62` → `heavy_work()` (`:72-131`) | **行情处理 + 算法单执行 + 算法单创建 + 定时维护**，全部在这里 |
| Lark 推送 | `quant_library/basic/LarkRebot.cpp:17` 起线程，`:28-38` 消费 | 消费 `rLarkMsg` |
| 落库写入 | `quant_library/basic/WriteFileContent.h:146-163` 起线程并消费 | 消费 `contentQueue`，写文件/DB |

`heavy_work()` 的单轮循环（`base_strategy.h:80-130`）：

```
while (1) {
    rcmdQueue->pop(rcmd)            → on_ordertrade / on_balance / on_position / on_total_account
    dbpreader->FetchLast()          → 遍历所有订阅 topic，逐个回调 on_dbpdata(topic, pdata, jumpedNum)
    if (now - utcTime >= timerInterval * 1000) on_timer(now)
}
```

`FetchLast()`（`dbp/sig/dbp/dbpreader.h:48-66`）是**按 topic 循环**的：一轮里所有对子的行情都会回调一次。
所以任何一个对子在自己的 `on_dbpdata` 里慢 50µs，**排在它后面的所有对子的行情处理都被推迟 50µs**。
这就是"创建拖累执行"的真实作用机制。

> 附带一提：`PairTradingStrategy::on_command`（`src/strategy/PairTradingStrategy.cpp:82`）目前是**死代码**——
> `BaseStrategy` 里没有 `on_command` 这个虚函数（`base_strategy.h:159-177` 只有 on_timer / on_ordertrade /
> on_balance / on_position / on_total_account / on_dbpdata）。也就是说 JSON 控制通道（CANCEL/MODIFY/QUERY）
> 现在根本没接上。这点在做线程拆分时要想清楚：控制指令将来从哪个线程进来。

### 1.2 行情热路径（每 tick 都跑）

```
on_dbpdata (heavy_work 线程)
├── algoContext.OnSpread(topic, pdata)              AlgoContext.cpp:1052+
│   ├── SpreadManager::OnMarketSpread               // 更新 Bbo
│   ├── GetBbo(active) / GetBbo(passive)
│   ├── 遍历 alogOrderManager 全部算法单             AlgoContext.cpp:1059-1060
│   │   └── CancelOrderOnSpread(pdata)              BaseAlgoOrder.cpp:400+
│   │       ├── 遍历 orderMgr.GetAllOrders()
│   │       ├── LimitManager::PassCancelLimit()     → mAccountLimitBoard[]（会插入）
│   │       └── pairOrderMgr.SelectPairOrderByPairId()
│   ├── LimitManager::PassLimit()                   → mAccountLimitBoard[]（会插入）
│   ├── CreatePairOrder → GetTargetPairOrder        AlgoPairOrder.cpp:174/216+
│   ├── AccountManager::FundVerify()                → mAccount（unordered_map）
│   └── QuantTrade::CreateOrder()
└── ptContext.OnSpread(topic, pdata)                PairTradingContext.cpp:56+
    ├── pim.UpdateRtSpread(pairKey, pdata)          // 写 PairInfo.rtSpread
    └── ProcessPairSignal(pi)                       PairTradingContext.cpp:70+
        ├── sg.CanOpen(pi) / sg.CanClose(pi)
        └── sg.CheckSignal(pi)                      // 8 组比较，读 rtSpread + orderParams
```

**注意 `ptContext.OnSpread` 是每 tick 都跑的**（不是只有出信号才跑）。它才是热路径上持续存在的额外开销。

### 1.3 建单路径（低频，但一跑就是一整串）

```
ProcessPairSignal → SubmitAlgoOrder                  PairTradingContext.cpp:163
├── BuildAlgoOrderJson                               PairTradingContext.cpp:183
│   ├── new AlgoPairOrder()
│   ├── 约 80 个字段赋值
│   └── SignalGenerator::Instance().GetConfig()      // 读全局配置
├── PairInfoManager::SetActiveAlgoOrder              // 写 PairInfo
└── m_algoCommandCb → AlgoContext::SubmitAlgoOrder   AlgoContext.cpp:80
    ├── BaseAlgoOrder::Init(smc)                     BaseAlgoOrder.cpp:98-118
    │   ├── smc->get_instrument_info() × 2           // 无锁只读，~50-100ns/次
    │   ├── InitPositionMgr()                        // 构造 PositionManager × 2
    │   └── StrategyConfig::GetInstance() × 2        // 读 minOrderAmount / tradesThreshold
    ├── alogOrderManager.InsertAlgoOrderByAlgoOrder()// unordered_map 插入
    ├── GeneratePubStr()                             // 字符串构造
    ├── rLarkMsg.Push()                              // moodycamel，跨线程安全
    ├── WriteAlgoOrder()                             Convert.h:148+
    │   └── fmt::format 约 55 个字段 + contentQueue.Push()   ★ 建单路径上最贵的一段纯 CPU
    ├── SpreadManager::IsPairInstrumentKeyExist / AddSpreadPara   // unordered_map 插入
    └── QuantDbp::Instance().Subscribe()             QuantDbp.h:25-31
        └── dbpreader->Subscribe()                   dbpreader.h:29-39
            ├── UpdateByHeader()                     // 可能重建 _pcols
            └── submap[nodeId] = 1                   ★ 与 FetchLast 同一个容器
```

### 1.4 结论：收益在哪

| 项 | 频率 | 单次量级 | 是否值得搬走 |
|---|---|---|---|
| `ptContext.OnSpread` 的信号判定 | **每 tick × 每对子** | 小 | **值得**（持续开销） |
| `BuildAlgoOrderJson` + `Init` | 低频 | 中 | 一般（只影响尖峰） |
| `WriteAlgoOrder` 的 55 字段 `fmt::format` | 低频 | **大** | 值得，但**可以就地优化掉** |
| `QuantDbp::Subscribe` | 低频 | 中 | **不能搬**（见 §2.1） |

所以：**"拆线程"不是唯一解，甚至不是性价比最高的解**。见 §5。

---

## 2. 硬约束：哪些状态不能跨线程碰

### 2.1 `DbpReader::submap` —— 最硬的一条 ⚠️

```cpp
void FetchLast(){                       // dbpreader.h:48
    auto iter = submap.begin();         // 迭代 submap
    while (iter != submap.end()) {
        if (iter->second != 1) { submap.erase(iter++); continue; }
        jumppednum = spreader.FetchLast(iter->first, &data);
        if (jumppednum > 0) {
            topic = spreader.GetColumnbyID(iter->first);
            _dbpcallback(topic, data, jumppednum);   // ★ 回调期间仍然持有 iter
        }
        ++iter;
    }
}
bool Subscribe(const std::string& topic){        // dbpreader.h:29
    UpdateByHeader();
    auto iter = _pcols->find(topic);
    if (iter != _pcols->end()){ submap[iter->second->__mnodeid] = 1; return true; }   // ★ 插同一个 map
    return false;
}
```

`FetchLast()` 在**持有 `submap` 迭代器、并且正在执行回调**的过程中，如果另一个线程调 `Subscribe()`
往 `submap` 里插入 → 触发 rehash → `iter` 失效 → **未定义行为**。

→ **结论：`QuantDbp::Subscribe()` 必须留在行情线程，只能通过"待订阅队列"在行情线程的安全点执行。**

### 2.2 其余共享状态

| 对象 | 结构 | 现状 | 证据 |
|---|---|---|---|
| `SpreadManager` | `mSpread` / `mInstEntry` 两个 `unordered_map` | **无锁** | `SpreadManager.h:41-42` |
| `AlgoOrderManager` | `mAlgoOrder`（`unordered_map<int64_t, BaseAlgoOrder*>`） | **无锁**，热路径每 tick 遍历 | `AlgoOrderManager.h:17` |
| `PairInfoManager` | `m_pairInfoMap`（`unordered_map<string, PairInfo>`，按值存） | **无锁**；`Init` 之后不再插删，所以 `GetPairInfo` 返回的裸指针稳定，**但字段是竞态点** | `PairInfoManager.h:87`、`PairInfoManager.cpp:11-18` |
| `LimitManager` | `mAccountLimitBoard`，`GetAccountBoard` 用 `[]` **会插入** | **无锁** | `LimitManager.h:18`、`LimitManager.cpp:51-70` |
| `AccountManager` | `mAccount`（`unordered_map<int, QuantAccount>`） | **无锁** | `AccountManager.h:31` |
| `md::InstrumentInfo` 只读查询 | `atomic_load(shared_ptr)` + `unordered_map::find()` | **已线程安全**（文件里明确注释了设计意图） | `include/securitymanager.h:31-46, 192-207` |
| `rLarkMsg` / `contentQueue` | `moodycamel::ConcurrentQueue` | **已线程安全** | `ConcurrentQueue.h:9-32`、`Utility.h:23-26` |
| `fmtlog` | — | 已线程安全 | — |

### 2.3 `DbpData*` 的生命周期

`DbpData` 继承 `sp::RNB`（共享内存环形节点），`FetchLast` 返回的 `data` 是**指向 ring slot 的裸指针**
（`dbpreader.h:56`）。ring 被写满一圈后这个 slot 会被覆盖，所以**不能把这个指针跨线程传出去**。

`DbpData` 体积约 600 字节（16 个 spread + 8×5 档深度 + 4 个 funding + 2 个 tema + 10 个 int64）。
每 tick 每对子拷 600 字节 ≈ 100~200ns，可以接受；更好的做法是只拷信号层需要的那部分。

---

## 3. 方案

### 3.1 职责划分

| | T_exec（现有 `heavy_work`） | T_signal（新增） |
|---|---|---|
| **独占拥有** | `AlgoOrderManager`、`SpreadManager`、`DbpReader`（含 submap）、`LimitManager`、`AccountManager` | `PairInfo::rtSpread`、`PairInfo::orderParams` |
| **做** | `algoContext.OnSpread` / `OnTimer` / `OnOrder`、`ScanFinishedAlgoOrders`、**注册算法单**、**订阅价差**、限流 | `UpdateRtSpread`、`ProcessPairSignal`、`CheckSignal`、`CanOpen/CanClose`、**构造算法单对象**、`WriteAlgoOrder`、`GeneratePubStr`、`rLarkMsg.Push` |
| **不做** | 不再做信号判定 | 不碰任何上面"独占拥有"的东西 |

核心原则：**所有现存的非线程安全容器保持单线程访问**，跨线程只传"值"和"所有权明确的对象指针"。

### 3.2 数据流

```
T_exec: on_dbpdata(topic, pdata)
  ① drain 待注册队列  → AlgoContext::SubmitAlgoOrder(p)        // 只做：map insert + SpreadManager + QuantDbp::Subscribe
  ② algoContext.OnSpread(topic, pdata)                        // 热路径，不再被信号逻辑拖累
  ③ 拷一份信号层需要的行情快照 → 入 SPSC 队列（try_enqueue，满了丢最旧）

T_signal: while (running)
  ④ pop 快照 → pim.UpdateRtSpread(pairKey, snapshot) → ProcessPairSignal(pi)
  ⑤ 命中信号 → BuildAlgoOrderJson(...)                        // new 对象 + 填字段 + Init(smc) + WriteAlgoOrder + GeneratePubStr
  ⑥ 对象指针入待注册队列（moodycamel，跨线程安全）
```

### 3.3 队列与快照的具体设计

**待注册队列**（T_signal → T_exec）
```cpp
moodycamel::ConcurrentQueue<BaseAlgoOrder*> mPendingRegister;   // 直接复用已有的 ConcurrentQueue 封装
```
- 生产者 T_signal，消费者 T_exec，多生产者安全。
- T_exec 每轮 `on_dbpdata` **开头** drain（保证本轮 `OnSpread` 就能看到新单），另外 `on_timer` 也 drain 一次兜底。

**行情快照队列**（T_exec → T_signal）
- 队列元素不能用 `DbpData*`（见 §2.3），要拷值。
- 建议定义一个小 POD，只装信号层需要的字段：
  ```cpp
  struct SignalTick {
      char     pairKey[stra::INST_KEY_LEN];
      int64_t  generateTs;
      bool     spreadEffective;
      double   spreadBidAsk, spreadBidBid, spreadAskBid, spreadAskAsk;
      double   spreadBidAskTema, spreadBidBidTema, spreadAskBidTema, spreadAskAskTema;
      double   activeFundingRate, passiveFundingRate;
      double   activePriceTema, passivePriceTema;
  };   // 约 130 字节
  ```
  正好对应 `pt::RealTimeSpread`（`PairInfo.h:59-82`）里的字段，`UpdateRtSpread` 直接吃这个结构即可。
- 队列容量给足（比如 8192），满了就丢最旧——**行情快照允许丢，但绝不能阻塞 T_exec**。

**T_signal 需要的其它 `PairInfo` 字段**（`largeStats`/`smallStats`/`maxVolume`/`profitSwitch`/
`autoFlag`/各 flag/OI 字段/`pairTotalVolume`）由 T_exec 写，T_signal 只读。
这部分用**每对子一个双缓冲 + `std::atomic<uint32_t>` 序号（seqlock）**：

```cpp
struct PairSnapshot { SpreadStats largeStats, smallStats; double maxVolume; bool autoFlag, ...; };
struct PairShared {
    PairSnapshot buf[2];
    std::atomic<uint32_t> seq{0};     // 写：seq+1(奇) → 写 buf[seq&1] → seq+1(偶)
};                                     // 读：反复读 seq，偶数则读 buf[seq&1]，读完再验 seq
```
T_exec 在 `on_timer`（更新统计/重算参数）之后发布一次；T_signal 每个快照 tick 读一次。

### 3.4 必须加同步的 5 个点

| # | 位置 | 现状 | 改法 |
|---|---|---|---|
| 1 | `PairInfo::hasActiveAlgoOrder` + `currentAlgoOrderId` | `bool` + `char[128]`，T_exec（`OnAlgoOrderUpdate`→`ClearActiveAlgoOrder`）和 T_signal（`SetActiveAlgoOrder`）都写 | 改成 `std::atomic<bool>`；`currentAlgoOrderId` 只让 T_exec 写，T_signal 用 `std::atomic<int64_t>` 另存一份 |
| 2 | 重复建单（check-then-act） | T_signal 读 `hasActiveAlgoOrder` → 建单 → 写 `hasActiveAlgoOrder`，非原子 | ① `compare_exchange_strong` 抢占；② **T_exec 在 `SubmitAlgoOrder` 里再按 `pairInstrumentKey` 去重一次**（双保险，必须有） |
| 3 | `QuantDbp::Subscribe` | 见 §2.1 | 只允许 T_exec 调；T_signal 不订阅 |
| 4 | `SpreadManager::AddSpreadPara` | 无锁，热路径同时在读 | 只允许 T_exec 调（本来就在注册阶段） |
| 5 | `LimitManager::PassLimit` / `PassCancelLimit` | `mAccountLimitBoard[]` 会插入 | 只在 T_exec 调；T_signal 的风控强平请求交给 T_exec 时再判限流 |

### 3.5 线程启动 / 退出

- 在 `PairTradingStrategy::pre_start` 末尾起 T_signal（`std::thread` + 成员持有，**不要 detach**）。
- `PairTradingStrategy::pre_stop`（`PairTradingStrategy.cpp:71-76`）里置 `running=false` 并 `join`。
  现在的 `pre_stop` 只存了 CSV 就返回；**不加 join 的话，进程退出时 T_signal 可能还在访问已经析构的
  `PairInfoManager` 单例 → 崩溃**。`signal_handler`（`main.cpp:13-21`）只调 `pre_stop()` 然后 `exit()`，
  所以 join 必须放在 `pre_stop` 里。

---

## 4. 风险点

按严重度排序。

### R1（严重）`DbpReader::submap` 竞态 → 崩溃
见 §2.1。只要建单路径里还留着 `QuantDbp::Subscribe`，而它跑在 T_signal 上，就是必现 UB。
**这是本方案第一优先级要守住的边界。**

### R2（严重）`AlgoOrderManager` 遍历 vs 插入
T_exec 每 tick 遍历 `mAlgoOrder`（`AlgoContext.cpp:1059-1060`、`:1846`）。如果 T_signal 直接
`InsertAlgoOrderByAlgoOrder`，rehash 期间 T_exec 正在遍历 → UB。
必须走"待注册队列 + T_exec 单点插入"。

### R3（严重）`PairInfo` 字段竞态
`m_pairInfoMap` 的**结构**在 `Init` 后不变（不会 rehash），所以 `PairInfo*` 指针稳定；
但 `pairTotalVolume` / `hasActiveAlgoOrder` / `avgPrice` / `largeStats` 等**字段**会被两边同时读写。
`double` 的读写本身在 x86-64 上是原子的，但**编译器优化 + 多字段不一致**才是问题
（例如 T_signal 读到 `pairTotalVolume` 是新的、`maxVolume` 是旧的）。
必须按 §3.3 的 seqlock / 双缓冲方案处理，不能"裸读裸写"。

### R4（高）信号时效性变化 → 业务风险
现在"看到信号 → 同一 tick 建单 → 下一 tick 开始报单"，链路是紧的。
拆线程后会引入 1 个 tick 以上延迟（T_signal 调度 + T_exec drain 时机）。
更麻烦的是：**T_signal 拿着一个已经过期的快照建单**——快照里的 `spreadBidBid` 可能已经走开了，
但 `BuildAlgoOrderJson` 算出来的触发价差是死的，于是订单会挂在一个错误的价差上。
→ 必须给快照加"有效期"（比如 `generateTs` 超过 100ms 就丢弃），并在 `BuildAlgoOrderJson` 入口再校验一次。
这个有效期定多少，是纯业务决策，需要和策略一起定。

### R5（高）重复建单
§3.4 的第 2 点。现在的 `if (pi.hasActiveAlgoOrder) return;` 是典型的 check-then-act，
单线程下没问题，拆开后两个 tick 的快照可能都判定"无活跃单"→ 建两张单 → 同一对子两个算法单同时在跑。
`AlgoContext::SubmitAlgoOrder` 目前**没有**按 `pairInstrumentKey` 去重（原来的 JSON 路径也没有），
拆线程前必须补上。

### R6（中）`on_timer` 的职责被劈开
`ptContext.OnTimer`（`PairTradingContext.cpp:324-353`）里做了三件事：
1. `RecalcVolumeParams` → 写 `pi.maxVolume` / `maxExposure`
2. `RecalcOrderParams` → 写 `pi.orderParams`（32 个字段，T_signal 要读）
3. `ProcessRisk` → 可能触发建单

如果 `orderParams` 归 T_signal 写，那 1/2 也得搬过去；但 `RecalcOrderParams` 读 `pi.largeStats/smallStats`，
这两个是 T_exec 在行情里更新的 → 又需要快照。
**建议第一版不要动 `OnTimer`**，让 `RecalcVolumeParams`/`RecalcOrderParams` 留在 T_exec，
`orderParams` 也仍由 T_exec 写，T_signal 通过 seqlock 读——这样改动面小得多。

### R7（中）`AccountManager` / `LimitManager` 的隐式插入
`FundVerify` 读 `mAccount`，`QueryAccount`（`on_timer`）写它；`PassLimit` 会 `mAccountLimitBoard[]` 插入。
只要坚持"这两个只在 T_exec 调"，就没问题；一旦有人图省事在 T_signal 里调了，就是偶发崩溃。

### R8（中）`sp::RNB` 快照一致性
即使拷贝，也要保证拷到的是**同一个 tick 的一致视图**（不能 `spreadBidBid` 是新的、`spreadAskAsk` 是旧的）。
`DbpData` 是共享内存里的节点，写方在写完后才更新 header（`spreader.FetchLast` 返回 `jumppednum > 0` 才回调），
所以只要在回调里**一次性 memcpy** 就天然一致；**千万不要逐字段读**。

### R9（中）退出顺序 / 生命周期
见 §3.5。另外 `BaseAlgoOrder` 的 `posMgrMakerTaker` / `orderMgr` / `pairOrderMgr` 是内嵌成员，
对象在 T_signal 构造、在 T_exec 析构（`~AlgoOrderManager`，`AlgoOrderManager.cpp:8-16`）——
跨线程 new/delete 是安全的，但**必须保证 T_signal 在 T_exec 停止后才退出**，否则正在构造的对象会被提前释放。

### R10（中）性能可能不升反降
- 建单是低频的，拆线程只在**最坏情况延迟**上有收益。
- T_signal 和 T_exec 如果没有绑核，会互相抢占 + 上下文切换，热路径的 **P99 可能变差**。
- 建议：T_signal 单独绑核（或至少 `nice` 降优先级），T_exec 绑核。
- 而且队列本身的入队/出队（moodycamel 大约 50~100ns/次）也是新增开销。

### R11（中）调试复杂度上升
现在一条日志流能完整看到"信号 → 建单 → 报单 → 成交"。拆开后两个线程交错，时序不再线性。
必须给每个算法单打上 `signalTs`（信号产生时刻）/ `registerTs`（注册时刻）/ `firstOrderTs`（首次报单时刻），
否则出问题根本没法复盘。**这一条建议在拆线程之前就先做。**

### R12（低）`on_command` 通道归属未定
JSON 控制通道（CANCEL/MODIFY/QUERY）现在是死代码。将来接上时，它进来的线程如果是 main 或别的线程，
会直接踩 `AlgoOrderManager` / `PairInfo`。建议：**控制指令也统一走队列进 T_exec**。

---

## 5. 推荐路线

### 第 0 步：先量（必做）

在 `algoContext.OnSpread`、`ptContext.OnSpread`、`BuildAlgoOrderJson`+`SubmitAlgoOrder` 三处加
`crypto::rdtscp()` 打点，跑一天，看 P50 / P99 / max。

**如果建单的 P99 只有几十 µs 而热路径 P99 已经是几百 µs，那拆线程解决不了问题，应该先做第 1 步。**

### 第 1 步：让建单变便宜（低风险，可能就够了）

建单路径上真正贵的是**纯 CPU 的字符串格式化**，不是逻辑：

| 优化 | 做法 | 预期 |
|---|---|---|
| `WriteAlgoOrder` 的 55 字段 `fmt::format` | `Convert.h:148+` 改成把字段塞进 POD 丢给 `contentQueue`，让 `WriteFileContent` 线程去 format | 建单省掉最大一块 |
| `GeneratePubStr()` | 同上，`rLarkMsg` 改成传结构体或延迟构造 | 省一块 |
| `BaseAlgoOrder::Init(smc)` 的 2 次 `get_instrument_info` | `PairInfoManager::Init` 时就把 `InstrumentInfo` 缓存进 `PairInfo`，建单时直接赋值 | 省 2 次 hash 查找 |
| `QuantDbp::Subscribe` → `UpdateByHeader()` | 已订阅过的 key 直接插 `submap`，不做 `UpdateByHeader` | 省一次列更新 |

做完这四样，建单在热路径上大概只剩几次 hash 插入 + 队列 push（~1~3µs），
**拆线程的必要性会大幅下降**。

### 第 2 步：只把"信号判定"搬走（推荐的第一刀）

- T_exec 保留全部共享状态所有权，`on_dbpdata` 里只做：drain 注册队列 → `algoContext.OnSpread` → 入快照队列。
- T_signal 只做：读快照 → `UpdateRtSpread` → `ProcessPairSignal` → `CheckSignal`。
- **建单对象仍在 T_exec 构造**（即 T_signal 只输出"该建单"的意图 + 参数，不含对象）。

好处：把**每 tick 的持续开销**移出热路径，同时完全不碰 `DbpReader` / `AlgoOrderManager` 的跨线程问题
（因为建单还在 T_exec）。风险面最小，收益（对热路径延迟）最大。

### 第 3 步：再把对象构造搬走（如果第 0 步的量测显示建单尖峰确实可测）

- T_signal 构造完整对象（`new` + 填字段 + `Init` + `WriteAlgoOrder` + `GeneratePubStr`）。
- T_exec 只做：`alogOrderManager` 插入 + `SpreadManager::AddSpreadPara` + `QuantDbp::Subscribe`。
- 此时才需要 §3.4 的全部同步措施。

### 不建议的做法

- ❌ 只给 `AlgoContext` 加一把大锁然后两个线程都跑 `OnSpread`——热路径拿锁，等于把并发收益全吃掉，
  还可能因为持锁期间做 IO 导致更长的尾延迟。
- ❌ 让 T_signal 直接调 `QuantDbp::Subscribe`（R1，必崩）。
- ❌ 用 `DbpData*` 直接入队（§2.3，环形覆盖后读到脏数据）。

---

## 6. 需要你拍板的问题

1. **信号时效性**：T_signal 拿到的快照多久算过期？（R4，纯业务决策）
2. **`on_timer` 里的 `RecalcOrderParams` 是否也要搬**？搬了收益不大但改动面翻倍（R6）。
3. **控制指令通道**（CANCEL/MODIFY/QUERY）将来从哪个线程进来？（R12）
4. **是否可以绑核**？如果部署环境允许 `sched_setaffinity`，收益会明显好很多（R10）。
