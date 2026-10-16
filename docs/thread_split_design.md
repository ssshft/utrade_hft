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

> ✅ **已于 2026-10-16 做完，结果与修正后的路线见 §7。**
> 结论摘要：每 tick 的判定逻辑只要 **5 ns**，那 7.9~8.9 µs 全是 **9 条调试 `std::cout`**；
> `Build` 是 **885 µs/对子**但每 60s 才一次。**先删那 9 条打印（9 行），再考虑拆线程。**

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

---

## 7. 实测结果（2026-10-16，§5「第 0 步：先量」已做）

> 触发：用户提出"算法单创建单独一个线程跑吧，因为要算价差分位数、这个比较耗时；
> 而且算法单创建也不需要实时价差推送触发，只要定时 5s 的数据"。
> 本节把 §5 要求的量测补上，并据此**修正**推荐路线。

### 7.1 怎么量的

本机编不了完整工程（缺 fmt / simdjson / boost / redis），但 `tests/market_to_algo/run.sh`
有一棵现成的**「stub 头 + 真实源码软链」**编译树，能编真实
`SignalGenerator.cpp` / `SpreadStatsBuilder.cpp` / `PairInfoManager.cpp`。
用它 + 三个一次性基准（都放 `/tmp`，**不入仓库**）量：

| 基准 | 量什么 | 怎么编 |
|---|---|---|
| `bench_build.cpp` | `SpreadStatsBuilder::Build` | 链 `SpreadStatsBuilder.cpp` |
| `bench_sig.cpp` | `CanOpen`/`CanClose`/`CheckSignalForSatisfy` | 链 `SignalGenerator.cpp` |
| `bench_rt.cpp` | `GetPairInfo` / `UpdateRtSpread` | 链 `PairInfoManager.cpp` |
| `bench_cout.cpp` | 热路径上那 9 条 `std::cout`（逐字复刻） | 单独编，无依赖 |

⚠️ 两个口径要分开报：**stdout 指向 `/dev/null`**（最便宜的 sink）与**指向管道**（更接近
生产：管道给日志收集）。差 1~3 µs，说明这 9 条语句的成本**主要在 `write()` 系统调用本身**。

### 7.2 每 tick × 每对子（`on_dbpdata` 全路径）

| 项 | 频率 | 实测 |
|---|---|---|
| `UpdateRtSpread`（含一次 `GetPairInfo` 字符串查找） | 每 tick | **0.013 µs** |
| `AccumulateSpreadSample`（5s 降频后绝大多数 tick 直接 return） | 每 tick | ~0.005 µs |
| `CanOpen` + `CanClose` | 每 tick | **0.005 µs** |
| `CheckSignalForSatisfy` 的**判定逻辑** | 每 tick | ~0.005 µs |
| 🔴 **9 条 `std::cout << … << std::endl`** | 每 tick | **7.9 µs**（→/dev/null）／**8.9 µs**（→管道） |
| **合计** | | **≈ 7.9 ~ 8.9 µs** |

那 9 条是：

| 位置 | 条数 |
|---|---|
| `src/strategy/PairTradingStrategy.cpp:559`（`on_dbpdata` 开头的调试打印） | 1 |
| `quant_library/signal/SignalGenerator.cpp:208-223`（`CheckSignalForSatisfy` 里逐条打 8 个开关/价差/阈值） | 8 |

### 7.3 每 60s × 每对子（`OnTimer` 里的统计刷新）

`SpreadStatsBuilder::Build`（`PairTradingContext.cpp:1091`）随窗口样本数增长：

| 采样间隔 | 24h 窗口样本数 | 内存 | `Build` 实测 |
|---|---|---|---|
| **5s（当前生产配置 `spreadSampleIntervalMs=5000`）** | **17 280** | 0.53 MB | **885 µs** |
| 1s | 86 400 | 2.64 MB | **10.1 ms** |
| 200 ms（`SpreadStatsBuilder.h:33` 注释里假设的最坏情况） | 432 000 | 13.2 MB | **60.6 ms** |

`Build` 内部做 4 次「拷贝 + `std::sort`」（sba/sbb/sab/saa 各一次）+ 2 遍计数/求和，
另加 1 条 `std::cout`（`SpreadStatsBuilder.cpp:111`）。**排序是绝对大头。**

### 7.4 三个与直觉不符的结论

**① 分位数计算不在每 tick 路径上。**
`Build` 在 `OnTimer` 里，按 `spreadStatsUpdateIntervalSec`（默认 **60s**）触发，
不在 `OnSpread` 里。所以它**不拖累每一个 tick**。
它真正的危害形态是：`on_timer` 与 `on_dbpdata` **在同一个线程**（`base_strategy.h:99-155` 的
`heavy_work`），所以 Build 那一拍会**阻塞行情** `0.885 ms × 对子数`。

**② 按总 CPU 算，那 9 条调试打印比整个分位数计算贵 20 倍以上。**

| | 60s 内的总开销（1 对子，假设行情 50 Hz） |
|---|---|
| 9 条 `std::cout` | 8 µs × 50 × 60 = **24 ms** |
| `Build`（5s 采样） | 0.885 ms × 1 = **0.885 ms** |

**③ "只需要定时 5s 的数据" —— 这本来就是现状。**
`AccumulateSpreadSample`（`PairTradingContext.cpp:172-177`）已经按 `spreadSampleIntervalMs`
降频，而生产配置就是 **5000 ms**。也就是说每 5s 只有 1 条样本真正进窗口，
其余 tick 只做一次减法+比较就返回（≈5 ns）。
⇒ 用户设想的"5s 快照"**不需要新增机制**，只要把"够 5s 的那一条"转发给另一个线程即可。

### 7.5 修正后的推荐路线

§5 原来的排序（先优化建单、再拆线程）方向是对的，但**权重需要改**：

| 步骤 | 做什么 | 预期收益 | 风险 | 建议 |
|---|---|---|---|---|
| **第 0 步** | **删掉/降级那 9 条 `std::cout`**（9 行） | 每 tick **−7.9~8.9 µs**（≈ 100%）；`Build` 也省一块 | **≈0** | **立刻做** |
| **第 1 步** | 把 **`Build`** 移出行情线程（**`RecalcOrderParams` 不搬**，见 §8） | 消掉每 60s 的 `0.885 ms × N` 行情停顿 | 中（见 §7.6） | **这才是"单独线程"的真正理由** |
| 第 2 步 | 把 `ProcessPairSignal`（判定+建单）也搬走 | 第 0 步之后判定只要 **5 ns**，搬它收益≈0 | **高**（§3.4 的 5 个同步点） | **不必做** |

**第 0 步为什么优先级最高**：它是唯一一处「改 9 行、拿走热路径上 99.9% 的可观测开销」的地方。
注意这 9 条都是**调试残留**（`std::endl` 还会强制 flush ⇒ 每 tick 9 次 `write()` 系统调用），
不是有意设计的输出。

**第 1 步怎么做（最小形态）**：
- 行情线程在 `AccumulateSpreadSample` 判定"这一条够 5s"时，把这条 32 字节的 `SpreadSample`
  `try_enqueue` 给 T_signal（**满就丢最旧**，绝不能阻塞行情）。
- T_signal 独占 `m_spreadWindows`：收样本 → `Add` → 每 `spreadStatsUpdateIntervalSec`
  做 `Prune + Build`，把算出的 `SpreadStats` 发布回来。
- 这样跨线程数据量 = **每对子每 5s 一条 32 B**（对比 §3.3 原方案"每 tick 一个 ~130 B 快照"，
  量级小 3 个数量级），而且**完全绕开 §2.1 的 `submap` 竞态**（T_signal 不碰 `DbpReader`）。
- ⚠️ ~~但 `RecalcOrderParams` 写 `pi.orderParams`，而 `CheckSignalForSatisfy`（行情线程）
  每 tick 读它 ⇒ 这 32 个字段需要同步（§3.3 的 seqlock，或**双缓冲 + 原子指针换**）。~~
  **已作废，见 §8：`RecalcOrderParams` 实测只有 6.2 ns，不搬。它留在行情线程，
  `orderParams` 就仍然是"只有行情线程写" ⇒ 一个锁、一个 seqlock、一个原子变量都不需要。**

### 7.6 第 1 步的硬约束（不变，同 §2 / §3.4）

- `QuantDbp::Subscribe` / `SpreadManager::AddSpreadPara` **只能行情线程**（`submap` 迭代器失效 → UB，R1）。
- `AlgoOrderManager` 插入**只能行情线程**（每 tick 遍历，R2）。
- `DbpData*` **不能跨线程**（共享内存环形覆盖，§2.3）⇒ 第 1 步传的是拷出来的 `SpreadSample`，天然安全。
- `pre_stop` 必须 `join`（R9）。
- `algoContext.OnSpread`（执行侧撤子单）**必须留在行情线程** —— 它是实时的，且 §3.1 已定它归 T_exec。

### 7.7 需要你拍板

1. **那 9 条 `std::cout` 是删掉，还是改成限流日志？** 如果它们还在用于调参/对账，
   建议 `LOG_DEBUG` + 每 N 条采样一次（仓库里已有 `should_log_limited` 这类限流工具）。
2. **第 2 步（把判定+建单也搬走）做不做？** 量测结论是**不值得**（判定 5 ns），
   但如果你要的是"建单那一拍绝对不进行情线程"的架构洁癖，那就得吃下 §3.4 的 5 个同步点。
3. **对子数会涨到多少？** 这是决定第 1 步紧迫性的唯一变量：
   1 对子时 `Build` 的 0.885 ms 停顿可忽略；20 对子就是 **18 ms**，必须搬。

### 7.8 附带发现（与本议题无关，但顺手报一下）

- `tests/market_to_algo/run.sh` **现在编不过**（exit=1，3 个 error）——stub 树与生产代码漂移：
  ① `ext/log_engine.h` 的 `log_sink` 只收 `const char*`，而生产有 `LOG_ERROR(std::string)`；
  ② 桩里没有 `LarkRebot`（`PairTradingContext.cpp:133` 用）；
  ③ 桩 `AlgoPairOrder` 缺 `tradingTypeOrder` 字段。
  本次量测是在 `/tmp` 的**临时副本**上补桩跑通的，**没有改仓库里任何文件**。
  这个套件本身需要单独修一次，否则它会一直"绿着骗人"（实际是编不过）。
- `SignalGenerator.cpp:3-7` 与 `SpreadStatsBuilder.cpp:5-8` 的注释都写着
  "为了 `std::cout` 调试打印才显式 include `<iostream>`" —— 删打印时**别把 include 一起删**，
  否则换个构建树会报 `'no member named cout in namespace std'`（注释里说 tests 踩过）。
  更稳的做法是连注释一起改掉。

## 8. 同步方案定稿（2026-10-16）—— 回答"要不要加锁 / 能不能用无锁队列"

### 8.1 先量 `RecalcOrderParams`：6.2 ns

`/tmp/ptsuite_build/bench_recalc.cpp`（一次性，不入仓库），链真实 `SignalGenerator.cpp`，
`clang++ -O2`，300 000 次取均值：

| 操作 | 实测 |
|---|---|
| `RecalcOrderParams`（统计有效，**全路径**：16 个量字段 + 8 组起止价差 + 8 个开关 + 流动性/资金费率门控） | **6.2 ns** |
| `RecalcOrderParams`（统计无效，写 16 个量字段后早退） | 2.7 ns |
| `OrderParams` 整体 `memcpy` | 2.3 ns |
| `SpreadStats` 整体 `memcpy` | 2.3 ns |

`sizeof(OrderParams)=320`、`sizeof(SpreadStats)=96`、`sizeof(SpreadSample)=32`。

⇒ `Build`(885 000 ns) ÷ `RecalcOrderParams`(6.2 ns) ≈ **143 000 倍**。
`RecalcOrderParams` 只占 `Build` 的 **0.0007%**。

**结论：把 `RecalcOrderParams` 一起搬走没有任何收益，反而凭空制造一个跨线程写者。**

### 8.2 定稿：只搬 `Build`，`pi.orderParams` 一个字段都不动 ⇒ 零锁

这是对 §7.5 那条 ⚠️ 的修正。职责划分：

| | T_market（行情线程） | T_signal（新增） |
|---|---|---|
| **独占拥有** | `PairInfo` **全部字段**（含 `orderParams` / `largeStats`）、`AlgoOrderManager`、`DbpReader`(含 `submap`)、`SpreadManager`、`LimitManager`、`AccountManager`、`m_lastSampleTs[]` | `m_builders[]`（每对子一个 `SpreadStatsBuilder`，内含 24h `deque<SpreadSample>`） |
| **每 tick** | `UpdateRtSpread` → 5s 采样门控 → `ProcessPairSignal`（判定+建单） | — |
| **每 5s** | 把 `{pairIdx, SpreadSample}` 入队 | 出队 → `builder.Add(s)` |
| **每 60s** | 消费新统计 → `UpdateLargeStats` → `RecalcOrderParams`（6.2 ns） | `Prune + Build`（885 µs）→ 发布 `SpreadStats` |

**关键点：`pi.orderParams` 和 `pi.largeStats` 仍然只由行情线程写。**
T_signal **不写 `PairInfo` 的任何字段** ⇒ 所有读者
（`CheckSignalForSatisfy`、`CloseSpreadReached`、`SubmitAlgoOrder`、`BuildNormalModify`、
`BuildCloseModify`、`RiskManager::ProcessRisk` 的 `IsValid()`）全部还是同线程读，
**一行都不用改，也不需要 seqlock / 双缓冲 / 原子变量。**

§3.3 和 §3.4 里为 `orderParams` 设计的那些同步手段，在这一版里**全部删掉**。

### 8.3 跨线程面 = 两条 SPSC 无锁队列

| 方向 | 载荷 | 频率 | 带宽（每对子） | 队列类型 |
|---|---|---|---|---|
| T_market → T_signal | `{uint32_t pairIdx; SpreadSample s;}` = 40 B | 每 5 s | **8 B/s** | SPSC 环形队列，`try_push` 满则丢**最新**（boost 语义，见 §9.6） |
| T_signal → T_market | `{uint32_t pairIdx; SpreadStats st;}` = 104 B | 每 60 s | **1.7 B/s** | 同上（或 per-pair 双缓冲槽 + 原子序号） |

合计 ≈ **10 B/s per pair**；20 个对子 = 200 B/s。
对比 §3.3 原方案（每 tick ~130 B × 50 Hz = 6500 B/s/对子）：**小 650 倍**。

**为什么无锁队列在这里是安全的（不需要任何 mutex）**：

1. 两个方向都是**严格单生产者单消费者（SPSC）** —— 各自只有一个线程写、一个线程读。
   这是无锁队列唯一需要的前提，满足。
2. 载荷是 **trivially copyable 的 POD**：没有指针、没有堆内存、没有析构、没有异常。
   （`DbpData*` 在入队**之前**就被转成了 32 B 的值 `SpreadSample`，指针不跨线程 —— 守住了 §2.3。）
3. 容量固定、`Init` 时一次分配 ⇒ 运行期**零内存分配**，不会在热路径上撞 `malloc` 锁。
4. 生产者必须非阻塞 ⇒ `try_push`（满则丢最旧），**绝不能用会阻塞的 `push`**。

实现就是一个 `alignas(64) std::atomic<size_t> head, tail` + 定长数组（2 的幂 + 掩码），
约 30 行。`head` 用 `release` 存、`tail` 用 `acquire` 读即可（x86 上其实是免费的）。
两个原子变量**各自 64 B 对齐**，避免 head/tail 互相 ping-pong cache line。

> ⚠️ 别复用仓库里现成的 `SPMCSubscriber`：它是**多生产者**语义、且构造时
> `next_idx = write_idx + 1`（重启期间的推送不补发），语义和这里要的 SPSC 流不一样。

### 8.4 60s 节奏归谁（两种都行）

- **A（推荐）T_signal 自持节奏**：T_signal 自己每 `spreadStatsUpdateIntervalSec` 做
  `Prune + Build` 并发布；行情线程的 `OnTimer` 第 2 步改成"**检查有没有新统计**"
  （per-pair `atomic<uint32_t> statsSeq` 变了才消费）。反应延迟 ≤ `timerInterval`(1s)，
  对 60s 周期无意义。少一种消息类型。
- **B 行情线程发"请算"请求**：`OnTimer` 到 60s 时塞一条 `REQ_BUILD{pairIdx}`，
  T_signal 算完发布，行情线程**下一 tick** 消费。"谁在什么时候算"更可控，但多一种消息类型。

两种都不影响 §8.2 的所有权结论。

### 8.5 顺带消失 / 顺带要改的两处

1. **`unordered_map` 的 rehash 风险直接消失。**
   现在 `m_spreadWindows` 是 `unordered_map<string, SpreadWindow>`，
   `AccumulateSpreadSample` 里 `find` + `emplace`。一旦拆线程，行情线程的 `emplace`
   （可能触发 rehash）会和 T_signal 的 `find` 撞车 —— 和 §2.1 的 `submap` 是同一类 bug。
   **改法：两边都改成 `Init` 时按 `pim.GetAllPairKeys()` 定长的 `std::vector`，
   下标就是 pairIdx**（`m_lastSampleTs[]` 归行情线程、`m_builders[]` 归 T_signal）。
   对子清单在 `pre_start` 之后不再变化 ⇒ 运行期**永不扩容、永不 rehash**。
   顺带把每 5s 一次的 `std::string` 哈希查找也省掉。

2. **`RecalcOrderParams` 的 4 个调用点一个都不用动**（因为它在行情线程原地不动）：
   `Init`（`PairTradingContext.cpp:99`）、`OnTimer` 第 1 步（`:1069`）、
   `OnTimer` 第 2 步（`:1093`）、`OnAlgoOrderUpdate` 终态（`:1040`）。

### 8.6 仍然必须留在行情线程的（含新增的三条）

同 §7.6：`QuantDbp::Subscribe`、`AlgoOrderManager` 插入、`DbpData*` 不跨线程、
`pre_stop` join、`algoContext.OnSpread`。

**另有三样必须留**，因为它们写的是 `RecalcOrderParams` 要读的输入字段，
搬走就会引入新的同步（而留在原地则完全免费）：

- `RecalcVolumeParams` —— 写 `pi.maxVolume`
- `UpdateLiquidStatus` / `ApplyCommand` —— 写 `*LiquidStatus` / `autoFlag` / `closeFlag`
- `UpdateRtSpread` —— 写 `rtSpread.*FundingRate`（**每 tick 都写**）

这正是"只搬 `Build`"最漂亮的地方：`RecalcOrderParams` 的**全部输入**都留在同一个线程，
所以它不需要快照、不需要原子读、不需要内存屏障。

### 8.7 一句话回答

> **要同步的只有"数据流"，没有"共享内存"。**
> 数据流是两条 SPSC 小 POD 队列（8 B/s + 1.7 B/s per pair），**无锁队列就够，不需要任何锁**。
> 原来担心的 `orderParams` 竞态**不存在**了 —— 因为实测 `RecalcOrderParams` 只要 6.2 ns，
> 让它留在行情线程即可，`orderParams` 依然是单线程独占。

### 8.8 顺手发现（与拆线程无关）

`m_lastSpreadStatsUpdateUs` 初始化为 `0`（`PairTradingContext.h:246`），`Init` 里**不播种**。
于是启动闸门一放行，第一次 `OnTimer` 就立刻满足 `nowUs - 0 > 60e6` ⇒ **马上做一次 Build**，
而此时窗口里只有 `Reconciling` 期间攒下的几条样本：

- 若样本数 `< spreadStatsMinSamples(5)` ⇒ `Build` 返回 `valid=false` ⇒
  `UpdateLargeStats` 把**刚从磁盘恢复的 `largeStats` 刷成无效** ⇒
  `RiskManager::ProcessRisk` 的"价差未回归"检查（`RiskManager.cpp:196`）**静默停摆**，
  直到下一轮（60s 后）窗口攒够。
  （阈值本身没坏：`RecalcOrderParams` 在 `!ls.IsValid()` 时早退，保留 Init 时按恢复值算出的
  `orderParams`。坏的是风控那一处。）
- 若样本数 `≥ 5` ⇒ 用几条样本的分位数覆盖掉刚恢复的 24h 分位数，
  "重启免预热"实际只买到不到 60s。

修法很轻：`Init` 里把 `m_lastSpreadStatsUpdateUs` 播种成 `m_startupTimeUs`（或让
`UpdateLargeStats` 拒绝无效统计、只在 `stats.IsValid()` 时才覆盖）。
**这条与本次线程拆分无关，单独立项。**

## 9. 队列实现选型（2026-10-16）—— 用 boost，不自己写

### 9.1 结论

**用 `boost::lockfree::spsc_queue`，不要新写一个。** 而且它已经在作用域里了，
连 include 都不用加。

### 9.2 证据：它已经在生产里跑，且就在要改的那个文件的作用域内

1. **已经在跑**：`include/shm_global.h:19,21`
   ```cpp
   typedef pubsub::SPSCQueue<pubsub::RCommand, 4096>  RcmdInnerQueue;
   typedef pubsub::SPSCQueue<pubsub::TCommand, 4096>  TcmdInnerQueue;
   ```
   而 `pubsub::SPSCQueue<T,size>`（`include/concurrent_queue.h:71-99`）就是
   `boost::lockfree::spsc_queue<T, boost::lockfree::capacity<size>>` 的薄封装。
   `RCommand` 是 **544 B**（我们 40 B 的 13 倍），4097 槽 ≈ **2.2 MB**，跑得好好的。
   ⇒ 载荷尺寸对我们完全不是问题。

2. **已经在作用域里**：`utrade_hft/include/base/base_strategy.h`
   - `:7` `#include "shm_global.h"` → 带进 `concurrent_queue.h` → 带进
     `<boost/lockfree/spsc_queue.hpp>`
   - `:12` `#include "concurrent_queue.h"`
   - `:17` `using namespace pubsub;`
   ⇒ 在 `heavy_work()` 所在的这个文件里，`SPSCQueue` 已经是可见的名字。

3. **不需要新增链接项**：`boost/lockfree` 是 header-only。`utrade_hft/CMakeLists.txt`
   的 `link_libraries` 里已经有 `boost_system`；`tb/build/.../CXX.includecache`
   里 `boost/lockfree/spsc_queue.hpp` 解析成功（`TB.dir`），而 tb 也没链 `boost_atomic`
   ⇒ 在这套 boost 上它是 header-only。

4. **`-I../include` 已在** `include_directories` 里（`SYSTEM_INC_DIR`），头文件找得到。

### 9.3 性能上没有自己写的理由（实测）

手写环（2 的幂 + 掩码 + release/acquire，索引各 64 B 对齐），40 B 消息，
`/tmp/ptsuite_build/bench_ring.cpp`，`clang++ -O2`：

| 操作 | 实测 |
|---|---|
| `pop` 空队列（消费者忙轮询的常态） | **0.25 ns** |
| `push` + `pop` 往返 | 1.78 ns（单次 **0.89 ns**） |
| `push` 满队列（失败路径） | **0.26 ns** |
| 跨线程：生产者满速 + 消费者忙轮询（cache line 乒乓最坏情形） | **5.73 ns/push** |
| `sizeof(Ring<SampleMsg,1024>)` | 41 088 B（≈40 KB） |

`boost::lockfree::spsc_queue` 是**同一个设计**（两个单调递增索引 + 取模 + release/acquire），
同一个数量级。**手写的最多和它一样快，不会更快** —— 瓶颈是原子操作本身，不是代码行数。

摊到本用例：每对子每 5 s 一条 ⇒ 5.73 ns ÷ 5 s = **1.1e-7 %**。等于零。

### 9.4 更重要的理由：这个仓库的队列已经太多了

`include/` 下现有的队列实现：

| 文件 | 类型 |
|---|---|
| `concurrent_queue.h` | boost `queue`/`spsc_queue` 封装 + `WFMPMC` 封装（3 个类） |
| `MPMCQueue.h` | rigtorp MPMC（运行期容量、cache line 对齐） |
| `readerwritercircularbuffer.h` | moodycamel SPSC（**阻塞式** API） |
| `atomic_queue/` | max0x7ba atomic_queue |
| `WFMPMC/` | 自研 wait-free MPMC |
| `ShmSpscQueue.h` / `ShmSpmcQueue.h` | 跨进程共享内存 SPSC / SPMC |
| `shmqueue/` | 跨进程 `ShmQueue` |
| `notify_ring.h` / `readerwritercircularbuffer.h` | 通知环 / 循环缓冲 |

外加 `db/include/base/MdSlotRing.h`（db 自己又写了一个定长槽索引环）。

而仓库自己的审查结论（`docs/三队列总体审查.md` §4）是「**三份实现各自只做对了三分之一**」；
`docs/架构判断与队列实现审查.md` 里还挂着一串未修的 P0/P1
（`ShmSpmcQueue::publish()` 39 次 locked RMW、读者空转把发布者拖慢 14 倍…）。

**在一个"队列过多、且质量参差"的代码库里新增第八套，是明确的负债方向。**

### 9.5 但**别用** `pubsub::SPSCQueue` 这个封装，直接用 boost 原生

它有 3 个坑：

1. **`push()` 把返回值丢了**（`concurrent_queue.h:82-84`）：
   ```cpp
   void push(const Topic &topic){ pQueue->push(topic); }
   ```
   满队列时**静默丢弃**，调用方无法计数、无法告警。我们要能数丢包。
2. **默认容量 60000**：`capacity<N>` 版本把缓冲区**内联在对象里**，
   `new Boost_Queue()` 分配 `60001 × sizeof(T)`。对 40 B 消息是 **2.4 MB** 的堆块。
   必须显式传容量。
3. **`get_left()` 是假的**（`:93-95`，永远 `return 0`）—— 没有任何诊断能力。
   boost 原生有 `read_available()` / `write_available()`。

### 9.6 建议写法

```cpp
// base_strategy.h 已经带进了 <boost/lockfree/spsc_queue.hpp>，无需新增 include
struct SampleMsg { uint32_t pairIdx; pt::SpreadSample s; };   // 40 B
struct StatsMsg  { uint32_t pairIdx; pt::SpreadStats  st; };  // 104 B

// 编译期容量：缓冲区内联，构造后运行期零分配
boost::lockfree::spsc_queue<SampleMsg, boost::lockfree::capacity<1024>> m_sampleQ; // ≈41 KB
boost::lockfree::spsc_queue<StatsMsg,  boost::lockfree::capacity<64>>   m_statsQ;  // ≈7 KB
```

要点：
- **用 `capacity<N>` 编译期容量**，不要用运行期容量版本（那个要 `new[]` + allocator）。
  容量取 2 的幂（boost 内部按容量取模/掩码）。
- **生产者用 `push()` 并累加丢弃计数**。
  ⚠️ **修正（2026-10-16 实施时查证）**：`boost::lockfree::spsc_queue` **没有 `try_push`**。
  它的签名是 `bool push(T const&)`，返回 `false` 表示队列满、这一条被丢弃。
  （对照 boost 1.85 文档的成员表：`push` / `pop` / `read_available` / `write_available` /
  `consume_one` / `consume_all` / `front` / `reset` —— 没有任何 `try_*`。
  仓库里 `include/concurrent_queue.h` 的 `pubsub::SPSCQueue` 也是只调 `push`/`pop`。）
  所以正确写法是 `if (!q.push(msg)) { ++dropped; }`。
- ⚠️ **语义修正**：boost 满时丢的是**最新**那条，不是 §7.5/§8.3 里我写的"丢最旧"。
  要"丢最旧"得 `pop` 一次再 `push`，那不是原子的、也不值得。
  容量 1024 × 5 s = **85 分钟**的积压余量，实际永远触发不到 —— **就用丢最新**。
  （`db/include/base/MdSlotRing.h` 的注释里也是这么对齐语义的：
  "满则丢【最新】那条 —— 与 `boost::lockfree::spsc_queue` 语义一致"。）
- **消费者用 `pop`**，空队列返回 `false`，成本 0.25 ns。
- `T` 必须 trivially copyable + trivially destructible —— `SpreadSample`(32 B) /
  `SpreadStats`(96 B) 都满足，boost 有 `static_assert` 兜底。
- **不需要为 cache line 对齐自己写**：boost 的两个索引没有显式 `alignas(64)`，
  但我们 10 B/s 的流量下乒乓根本不存在（跨线程满速实测也才 5.73 ns/push）。
  真要一个对齐版，仓库里的 `include/MPMCQueue.h`（rigtorp，用
  `hardware_destructive_interference_size` 对齐、运行期容量、`try_push`/`try_pop`
  返回 bool）比手写更划算 —— 但它是 MPMC 设计，单次开销更大，且全仓库没人用过。

### 9.7 两个诚实的保留

- **本机没有 boost**（`brew --prefix boost` 只返回路径，实际未安装），
  所以 §9.3 的数字是**手写环**的；boost 那一侧是**静态判断**
  （同设计 + 已在生产跑 + 已在作用域内）。要在服务器上跑一次才算数。
- `pubsub::SPSCQueue` 这个封装**从来没被直接实例化过**吗？不是 ——
  `RcmdInnerQueue` / `TcmdInnerQueue` 就是它，已在生产。但它 `push()` 丢返回值这条
  决定了我们**不该复用它**（§9.5）。



## 10. 实施记录（2026-10-16）—— 已改完并验证

口径：**只搬 `Build`，`RecalcOrderParams` 留在行情线程**（§8），所以**零锁**；
队列用 `boost::lockfree::spsc_queue`（§9），不自己写。改动尽量小。

### 10.1 改了哪些文件

**生产代码（5 个）**

| 文件 | 改动 |
|---|---|
| `quant_library/algo/PairTradingContext.h` | 新增 `#include <atomic>/<cstring>/<thread>/<unordered_map>` + `<boost/lockfree/spsc_queue.hpp>`；新增 `SpreadSampleMsg` / `SpreadStatsMsg` 两个 POD 与两条队列 typedef（附 ~30 行设计注释）；新增公开 `StopSignalThread()`；新增私有 `StartSignalThread()` / `signal_work()` / `RunSignalOnce()` 与 `m_signalThread` / `m_signalRun` / `m_sampleQ` / `m_statsQ` / `m_lastSampleTs`；`m_spreadWindows` 的值类型由 `SpreadWindow{builder,lastSampleTs}` **收窄为 `SpreadStatsBuilder`**；**删除** `m_lastSpreadStatsUpdateUs` |
| `quant_library/algo/PairTradingContext.cpp` | 顶部流程注释同步；加 `<chrono>` / `<cstring>` / `<thread>`；析构开头调 `StopSignalThread()`（兜底）；`Init()` 末尾调 `StartSignalThread()`；新增 3 个线程函数（含 `RunSignalOnce`）；`AccumulateSpreadSample` 重写为「5s 降频门控 + `push` 一条 160 B POD」；`OnTimer` 第 2 步由「自己 Prune+Build」换成「消费 `m_statsQ` → `UpdateLargeStats` → `RecalcOrderParams` → 条件 `SaveSpreadStats`」 |
| `src/strategy/PairTradingStrategy.cpp` | `pre_stop()` 第一行加 `ptContext.StopSignalThread()`；`on_dbpdata` 里那条逐 tick 的 `std::cout` 删除 |
| `quant_library/signal/SignalGenerator.cpp` | `CheckSignalForSatisfy` 里 8 条 `std::cout` 删除；随之删掉 `<iostream>`（本文件已无任何 iostream 用法） |
| `quant_library/signal/SpreadStatsBuilder.cpp` | `Build` 末尾 1 条 `std::cout` 删除；随之删掉 `<iostream>` |

**测试与工具（7 个）**：见 §10.3 / §10.6。

### 10.2 生产代码的最终形态

两条队列（`PairTradingContext.h`）：

```cpp
struct SpreadSampleMsg { char pairKey[stra::INST_KEY_LEN]{""}; SpreadSample s; };  // 160 B
struct SpreadStatsMsg  { char pairKey[stra::INST_KEY_LEN]{""}; SpreadStats  st; }; // 224 B

using SpreadSampleQueue = boost::lockfree::spsc_queue<SpreadSampleMsg, boost::lockfree::capacity<256>>;
using SpreadStatsQueue  = boost::lockfree::spsc_queue<SpreadStatsMsg,  boost::lockfree::capacity<32>>;
```

实测（`sizeof`，在测试树上直接量的）：

| 类型 | sizeof |
|---|---|
| `SpreadSample` | 32 B（`int64_t ts` + 6 × `float`） |
| `SpreadStats` | 96 B |
| `SpreadSampleMsg` | **160 B**（`char[128]` + 32 B） |
| `SpreadStatsMsg` | **224 B**（`char[128]` + 96 B） |
| `SpreadSampleQueue` | **40 976 B**（256 × 160 + 16） |
| `SpreadStatsQueue` | **7 184 B**（32 × 224 + 16） |

⇒ `PairTradingContext` 内联多了 **≈48 KB**。对象是 `new` 出来的（`main.cpp`），
测试里 `Fx` 是栈对象、一个约 48 KB，远在 8 MB 栈内，无所谓。

> 容量比 §9.6 草稿小（256/32 而不是 1024/64）：**载荷带的是 `pairKey` 字符串而不是
> `pairIdx`**，所以单条从 40 B 涨到 160 B。256 槽 ≈ 40 KB，按 5 s/对子算在 20 对子下
> 仍有 64 s 积压余量 —— 而正常情况（无积压）下这个数字毫无意义。**选 `pairKey` 而不是
> `pairIdx` 是为了不改 `PairInfoManager` 的接口**（§8.2 的"最小改动"优先）。

T_signal 的循环（`signal_work()`）与它的**一轮**（`RunSignalOnce()`）：

```cpp
void PairTradingContext::signal_work() {
    // 计时器从本线程启动时刻起算，不是 0（否则第一轮立刻 Build，见 §8.8）
    int64_t lastStatsUpdateUs = crypto::getCurrentTime();
    while (m_signalRun.load(std::memory_order_relaxed)) {
        RunSignalOnce(crypto::getCurrentTime(), lastStatsUpdateUs);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void PairTradingContext::RunSignalOnce(int64_t nowUs, int64_t& lastStatsUpdateUs) {
    // 1. 排空样本 → 喂进各自的 24h 窗口
    SpreadSampleMsg sm;
    while (m_sampleQ.pop(sm)) {
        auto it = m_spreadWindows.find(sm.pairKey);
        if (it == m_spreadWindows.end()) {
            SpreadStatsBuilder builder;
            builder.SetWindowUs(static_cast<int64_t>(m_cfg.spreadStatsWindowSec) * 1000000LL);
            it = m_spreadWindows.emplace(sm.pairKey, std::move(builder)).first;
        }
        it->second.Add(sm.s);
    }
    // 2. 到点就 Prune + Build + 发布
    if (nowUs - lastStatsUpdateUs <=
            static_cast<int64_t>(m_cfg.spreadStatsUpdateIntervalSec) * 1000000LL) return;
    for (auto& kv : m_spreadWindows) {
        kv.second.Prune(nowUs);
        const SpreadStats stats = kv.second.Build(m_cfg.quantileUp, m_cfg.quantileDn,
                                                 static_cast<size_t>(m_cfg.spreadStatsMinSamples));
        SpreadStatsMsg out{};
        std::strncpy(out.pairKey, kv.first.c_str(), stra::INST_KEY_LEN - 1);
        out.st = stats;
        m_statsQ.push(out);                 // 满则丢本条（boost 语义：丢最新）
    }
    lastStatsUpdateUs = nowUs;
}
```

> 为什么是 `sleep_for(2 s)` 而不是 `spin::backoff_sleep(step)`：那条阶梯最后停在 20 µs，
> 在这条通道上等于每秒醒 5 万次，纯属空转。样本自带 `ts`、窗口是 24h，晚 2s 消费**不改变
> 任何结果**，所以 2s 是免费的。唯一的代价是 `StopSignalThread()` 的 `join()` 最多等 2s ——
> 只发生在退出路径上。

行情线程侧（`OnTimer` 第 2 步）：

```cpp
SpreadStatsMsg msg;
bool updated = false;
while (m_statsQ.pop(msg)) {
    PairInfo* pi = pim.GetPairInfo(msg.pairKey);
    if (!pi) continue;
    pim.UpdateLargeStats(msg.pairKey, msg.st);
    sg.RecalcOrderParams(*pi);          // 6.2 ns，留在本线程 => 零同步（§8.2）
    updated = true;
}
if (updated && !m_cfg.spreadStatsStatePath.empty()) pim.SaveSpreadStats(m_cfg.spreadStatsStatePath);
```

> 丢包观测（`m_sampleDropped` / `m_statsDropped` / `m_lastDropWarned` + 那条"只在变化时
> 打一条 WARN"的第 2c 步）在第二轮简化里**整块删掉了**：`push` 的返回值不再接、
> 计数器不再存在。理由 —— 两个队列的容量（256 / 32）相对流量（5s/对子、60s/对子）
> 有 60 倍以上余量，容量打满只可能是 T_signal 被饿死，而那属于"线程根本没起来"
> 这一档故障，应该在别处（线程存活检查）暴露，不该靠一个恒为 0 的计数器。

**零锁的落实**：`pi.orderParams` / `pi.largeStats` 的**唯一写者仍然是行情线程**
（`RunSignalOnce` 只写 `m_spreadWindows` 与 `m_statsQ`）。`m_spreadWindows` 归 T_signal
独占、`m_lastSampleTs` 归行情线程独占 —— 两个容器各自单线程，**顺带消掉了 §8.5 的 rehash
隐患**（原来一个 `unordered_map` 两边碰，`emplace` 触发 rehash 会和另一侧的 `find` 撞）。

`m_lastSpreadStatsUpdateUs` 删除后，§8.8 那个 bug（`Init` 没给它播种、第一轮 `OnTimer`
立刻用几条样本 Build 出一个假分位数覆盖掉刚恢复的 24h 分位数）**由构造消失**：
计时器现在活在 `signal_work()` 的局部变量里，从 `crypto::getCurrentTime()` 起算。

### 10.3 测试怎么跟上：`PumpSignal`

分位数搬到异步线程后，D4 / G17 / G19 / G20 那种「喂 5 条样本 → `OnTimer` → 统计就该建立」
的断言没法"等"另一个线程。解法是把 T_signal 的**一轮**做成生产代码里的可调用函数
（`RunSignalOnce`），测试显式驱动：

```cpp
static void PumpSignal(PairTradingContext& ctx, int64_t nowUs) {
    ctx.StopSignalThread();                 // 先停：否则两个线程同时碰 m_spreadWindows / m_statsQ
    int64_t lastStatsUpdateUs = 0;          // 0 -> nowUs - 0 > 60e6 必然成立
    ctx.RunSignalOnce(nowUs, lastStatsUpdateUs);
}
```

> `RunSignalOnce` 原来有个 `bool force` 形参专门给测试绕门控用，第二轮简化时删掉了。
> 现在靠 `lastStatsUpdateUs = 0` 达到同样效果：门控是
> `nowUs - lastStatsUpdateUs > spreadStatsUpdateIntervalSec * 1e6`，传 0 时左边就是
> 整个 Unix 时间戳（≈1.8e15 µs），恒成立。生产路径传的是 `signal_work()` 自己的真实
> 计时器，所以仍然每 60s 才建一次统计 —— 测试与生产**共用同一个函数、同一段逻辑**。

**为什么这次改动不可能影响其它用例**：全仓库只有 4 个用例会调 `OnSpread`
（D4 / G17 / G19 / G20），其余用例的 `m_spreadWindows` 恒为空 —— 而旧代码的
`for (auto& kv : m_spreadWindows)` 在空表上什么也不做。所以"搬走 Build"对它们
是恒等变换。（顺带说明：旧代码里那 4 个用例能过，靠的是 `m_lastSpreadStatsUpdateUs`
初值为 0 这个 bug —— 第一轮 `OnTimer` 无条件 Build。）

### 10.4 没被覆盖的东西（诚实的局限）

- `PumpSignal` 测的是**一轮 T_signal 的逻辑**，不是"线程真的跑起来了"。线程的启动、
  退避（`sleep_for(2s)`）、停止/`join` 路径**没有确定性用例**。要覆盖它得写
  "推样本 → 轮询 `pi.largeStats.IsValid()` 直到超时"的用例，那必然带时间依赖 ——
  这是异步设计的固有代价，本次没做。
- macOS 上编的是 `stubs/noboost/.../spsc_queue.hpp` 这个**等价桩**，不是真 boost。
  真 boost 那条路径（服务器）只经过"编译 + 类型检查"，没跑过。
- 真 boost 的 `spsc_queue` 是否满足 `static_assert`（trivially copyable）——
  `SpreadSampleMsg` 含 `char[128]` + 6 float，满足；但没有在真 boost 上验过。

### 10.5 验证结论

`tests/market_to_algo`：**356 PASS / 0 FAIL**（含 D4 / G17 / G19 / G20）。
`./run.sh -v`（`-Wall -Wextra`）下**新增代码零 warning**，只剩桩里两条既有的
（`time_util.h` 的 `sprintf` 弃用、`StraException.h` 的 `const` 返回限定符）。

⚠️ **这只是"编译 + 行为"级别的验证，不是性能验证。** §7 那些数字是在
macOS/clang++ -O2 的 /tmp 独立基准里测的；`utrade_hft` 全工程在服务器上编译，
`-march=native -O3 -flto` 下的实际收益要在服务器上确认。

### 10.6 顺手修的东西（都是"套件本来就坏了"，与本次改动无关）

`tests/market_to_algo/run.sh` 在这次改动之前**直接 `exit 1`** —— 三处桩漂移：

| 漂移 | 修法 |
|---|---|
| `stubs/ext/log_engine.h` 的 `log_sink(const char*, A&&...)` 接不了 `std::string`（`LOG_ERROR(msg)` 的孤儿单告警） | 第一个参数也改成万能引用 |
| 缺 `stubs/basic/LarkRebot.h`（真实 `BaseAlgoOrder.h:12` 会 include 它） | 补一个接口与真实头一致的桩 |
| `stubs/basic/BaseAlgoOrder.h` 漏了 `tradingTypeOrder`（真实头 `:25`） | 按真实头位置补上 |

另外三件：

- **`F5` 用例过期**：它断言 10 USDT 触发碎单强平、注释写 "< 25"，但
  `RiskManager.h:48` 与 `etc/config.json` 都已经是 **`tinyCloseThresholdUsdt = 5`**。
  改成 `-0.04`（4 USDT）。`docs/实盘接入与参数配置.md:408` 那张表还写着 25，也是旧的。
- **`run.sh` 加 boost 探测**：`spsc_queue.hpp` 是 header-only，系统里有真 boost 就用真的，
  没有才 `-I stubs/noboost`（避免 `-I` 先于系统路径命中、把服务器上真的 boost 挡掉）。
- **`suite.cpp` 的 `#define private public` 清单**补 `<atomic>` / `<thread>` / `<mutex>`，
  并把 `<boost/lockfree/spsc_queue.hpp>` 放在宏**生效之前**包含 —— boost 头里 `private:`
  极多，和 std 头一样不能在宏生效期间第一次解析。

### 10.7 待办

- **服务器上跑一次 `./run.sh`**（用真 boost + libstdc++），确认 §10.4 的三条局限。
- **`BaseAlgoOrder.cpp:444/459/467` 与 `AlgoPairOrder.cpp:707/709/714` 还有 6 条 `std::cout`**
  没删。其中 `BaseAlgoOrder.cpp:444` 在 `CancelOrderOnSpread` 的循环里，**每个在跑的算法单
  每 tick 一次**，和刚删的那 9 条同性质（`std::endl` 强制 flush = 一次 `write()`）。
  没顺手删是因为这两个文件不在本套件的编译集里、本机验不了。建议单独一轮清掉。
- `docs/实盘接入与参数配置.md:408` 的 `tinyCloseThresholdUsdt = 25` 要改成 5。

### 10.8 第二轮：简化（用户手改后对齐）

第一轮交付后用户自己动手把实现削薄了一层，方向一致（"最小化改动"），改动如下：

| 改动 | 影响 |
|---|---|
| `signal_work()` 不再调 `spin::backoff_sleep(step)`，改 `std::this_thread::sleep_for(2s)` | `#include "SpinBackoff.h"` 变成死引用，**已删**；补上显式的 `<chrono>`（`std::chrono::seconds` 原本靠 `<thread>` 传递包含）。代价：`join()` 最多等 2s，只在退出路径 |
| `RunSignalOnce(nowUs, lastStatsUpdateUs, bool force)` → **去掉 `force`** | 测试改靠 `lastStatsUpdateUs = 0` 让门控必然成立（`PumpSignal`）。少一个只有测试会用的形参，生产/测试彻底同构 |
| 删掉 `m_sampleDropped` / `m_statsDropped` / `m_lastDropWarned` 与 `OnTimer` 第 2c 步 | `push` 返回值不再接、计数器不再存在。理由见 §10.2 的引用块 |
| `AccumulateSpreadSample` 去掉 `static_cast<float>` 与 `push` 返回值判断 | 显式窄化改隐式（`double` → `float`，同一处源码本来就在做这件事）。`-Wall -Wextra` 下无 warning（`-Wconversion` 没开，且该转换是有意的） |
| `OnTimer` 第 2 步的外层 `{ }` 作用域去掉 | `msg` / `updated` 直接落在 `OnTimer` 作用域里，纯风格 |

⚠️ 手改过程中 `RunSignalOnce` 的**头文件声明被误删**（`.cpp` 里还有定义、还在用已删的
`m_statsDropped`），`./run.sh` 报三条错（`no member named 'RunSignalOnce'` /
`out-of-line definition does not match` / `use of undeclared identifier 'm_statsDropped'`）。
已补齐声明并同步 `.cpp`。**重跑 `./run.sh`：356 PASS / 0 FAIL**（D4 / G17 / G19 / G20
四条依赖门控的用例全部通过，说明"`lastStatsUpdateUs = 0` 替代 `force`"是等价的）。

顺带对齐的文档：`tests/market_to_algo/README.md` 的 `PumpSignal` 段与 `SpinBackoff.h`
那一行、`run.sh` 的两处注释。
