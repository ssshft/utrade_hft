# 数据源链路（价差行情 / balance / position / 报单回报）

审查对象：`utrade_hft` 策略层收到的**四类外部输入**——价差行情、资金、仓位、报单回报——各自的**生产者、传输介质、进程内入口、分发去向**。

审查目的：回答「这些数据到底从哪来」，并顺带标出**跨进程配置必须对齐的地方**，以及链路里几个会静默吞数据的位置。

**本文只做链路梳理，未修改任何源码。**

---

## 〇、结论速览

| 数据 | 生产者 | 传输介质 | 进程内入口 | 分发给谁 |
|---|---|---|---|---|
| 价差行情 | `dbprocess`（`dbp/`，独立进程） | `/dev/shm/DBP_TOPIC_*` + `DBP_DATAS_*`（mmap 文件） | `dbp::DbpReader`，**轮询拉取** | `on_dbpdata` → `algoContext` **和** `ptContext` |
| balance | `tb`（独立进程，交易所 WS/REST 回报） | `Tb2UtradeRCommandSHM`（POSIX shm 环形，SPMC） | `rcmdQueue->pop()`，**主动消费** | `on_balance` → 两个 context |
| position | 同上 | 同上 | 同上 | `on_position` → 两个 context |
| total_account | 同上 | 同上 | 同上 | `on_total_account` → 两个 context |
| 报单回报 | 同上 | 同上 | 同上 | `on_ordertrade` → **只有** `algoContext` |
| 定时器 | **本进程自己** | 无（`heavy_work` 里算时间差） | — | `on_timer` |

一句话：**行情走 DBP 共享内存「拉」，其余三类走 pubsub 共享内存队列「推」，两者都在同一个 `heavy_work` 线程里被消费；定时器不是数据源，是这个线程自己合成的事件。**

出向（下单/撤单）走**第三条通道**：`Utrade2TbTCommandSHM`（System V 共享内存队列），和入向的回报通道是两条不同的队列，见 §4。

---

## 一、全景图

```
                 ┌──────────────────── 交易所 (BINANCE / GATEIO / OKX / BYBIT) ────────────────────┐
                 │                    WS depth / trades / funding · REST account                  │
                 └───────────────┬──────────────────────────────────────┬───────────────────────┘
                                 │ 行情                                  │ 账户 / 下单 / 回报
                                 ▼                                       ▼
                    ┌────────────────────────┐              ┌────────────────────────────┐
                    │  dbp 进程 (dbprocess)  │              │      tb 进程               │
                    │  DbpServer             │              │      TbOperation           │
                    │  sp::Writer<DbpTopic,  │              │  ┌──────────────────────┐  │
                    │            DbpData>    │              │  │ utrade2TbTCommandShm │  │
                    │  按 pairlist.xml 算    │              │  │  .pop(tcmd)          │  │
                    │  四轴价差 + Tema/Max/  │              │  └──────────┬───────────┘  │
                    │  Min + 5 档深度 +      │              │             ▼              │
                    │  funding + 延迟        │              │     交易所下单/撤单/查询   │
                    └───────────┬────────────┘              │             │              │
                                │                           │             ▼              │
        /dev/shm/DBP_TOPIC_*    │                           │     交易所回报 → RCommand  │
        /dev/shm/DBP_DATAS_*    │                           └─────────────┬──────────────┘
        (mmap 文件, journal)    │                                         │
                                │                       Tb2UtradeRCommandSHM (POSIX shm 环形)
                                │                                         │
                                ▼                                         ▼
        ╔════════════════════════════════════════════════════════════════════════════════╗
        ║                     utrade_hft  进程  (BaseStrategy::heavy_work 单线程)         ║
        ║                                                                                ║
        ║   while(1) {                                                                   ║
        ║     ① rcmdQueue->pop(rcmd)          ← 推：balance / position / total / 回报     ║
        ║        └─ convert_rcmd_2_xxx + _strategyIds 过滤                               ║
        ║     ② dbpreader->FetchLast()        ← 拉：价差行情                             ║
        ║        └─ 回调 PairTradingStrategy::on_dbpdata                                 ║
        ║     ③ now - utcTime >= timerInterval*1000 → on_timer(now)   ← 自己合成         ║
        ║   }                                                                            ║
        ╚════════════════════════════════════════════════════════════════════════════════╝
                                │
                                ▼
                    PairTradingStrategy::on_xxx
                    ├── on_dbpdata      → algoContext.OnSpread    + ptContext.OnSpread
                    ├── on_balance      → algoContext.OnBalance   + ptContext.OnBalance
                    ├── on_position     → algoContext.OnPosition  + ptContext.OnPosition
                    ├── on_total_account→ algoContext.OnTotalAccount + ptContext.OnTotalAccount
                    └── on_ordertrade   → algoContext.OnOrder     （只有 algoContext）
```

---

## 二、价差行情（DBP）

### 2.1 谁算的价差

**价差不是本工程算的。** `utrade_hft` 拿到的 `dbp::DbpData` 里，四个轴的价格已经是成品：

```cpp
// dbp/sig/dbp/include.h:61-64
double spreadBidAsk{0.0};  // 主动腿 bid1 + 被动腿 ask1
double spreadBidBid{0.0};  // 主动腿 bid1 + 被动腿 bid1
double spreadAskBid{0.0};  // 主动腿 ask1 + 被动腿 bid1
double spreadAskAsk{0.0};  // 主动腿 ask1 + 被动腿 ask1
```

生产者是 `dbp/` 目录下的 **`dbprocess`** 进程（`dbp/dbprocess/dbpserver.{h,cpp}`）。它的输入是：

- `sp::Reader<dbw::DBTopic, dbw::DBdata>* reader`（`dbpserver.h:57`）——上游行情总线 DBW；
- 三个 SPMC 订阅：`suberDepth` / `suberTrades` / `suberFunding`（`dbpserver.h:65-67`），元素类型 `md::Depth1` / `md::Trades` / `md::FundingRate`。

它的输出是：

```cpp
sp::Writer<DbpTopic, DbpData>* writer;   // dbp/dbprocess/dbpserver.h:56
```

即把算好的价差写进 DBP 共享内存。`dbp/etc/dbprocess.json` 里给了落点：

```json
"dbprocess": {
    "version": 20230223,
    "mpath": "/dev/shm/DBP_TOPIC_20230223",
    "dpath": "/dev/shm/DBP_DATAS_20230223",
    ...
    "pairlistfile": "/mnt/hft/dbp/etc/pairlist.xml",
    "submkttypes": "DEPTH1|FUNDING_RATE",
    ...
}
```

`pairlist.xml` 决定**算了哪些对子**，`<name>` 的格式就是价差 topic 名：

```xml
<pair>
    <ID>0</ID>
    <enable>1</enable>
    <name>BINANCE.USDT_SWAP.BTC-USDT|GATEIO.USDT_SWAP.BTC-USDT</name>
    <activemultiply>1</activemultiply>
    <passivemultiply>1</passivemultiply>
    <delayintvel>500</delayintvel>
</pair>
```

### 2.2 传输介质：两块 mmap 文件 + journal

DBP 用的不是队列，是 `dbp/sig/shmpool/shmpool.h` 里的 `sp::Shm2DArray` / `sp::Reader` / `sp::Writer`：

- `mpath`（`DBP_TOPIC_*`）存**列头**（`DbpTopic`，每个 pair 一列）；
- `dpath`（`DBP_DATAS_*`）存**数据槽**（`DbpData`，每列一个环形缓冲，槽数 = `__bufsize`，默认 `DBP_COL_SIZE = 500`）。

两者都是 `/dev/shm` 下的 mmap 文件，用 `fmapmem::journal` 的 `reader` / `single_writer` 打开。列头里维护一个单调递增的 `__datacount`，写者每写一条就 `+1`；读者自己记 `readcount`，靠**比较两者差值**判断有没有新数据（`shmpool.h:186-209`）。

### 2.3 进程内入口

`BaseStrategy` 构造时建 reader（`include/base/base_strategy.h:31-40`）：

```cpp
auto mpath = tradeConfig["DBP"]["mpath"].GetString();
auto dpath = tradeConfig["DBP"]["dpath"].GetString();
dbpreader = new dbp::DbpReader(mpath, dpath);
auto f = std::bind(&BaseStrategy::on_dbpdata, this, _1, _2, _3);
dbpreader->SetCallback(f);
```

注意 `mpath` / `dpath` **不是** `etc/config.json` 给的，是从 **`/inc/trade_config.json`**（部署机绝对路径）读的，和 tb 读的是同一个文件。`utrade_hft/etc/config.json` 里的 `"dbp": { "topics": [] }` 目前是**没被用到的**（`BaseStrategy` 没读它）。

### 2.4 订阅时机

`PairTradingStrategy::pre_start` 里按 `op.pairKeys` 逐条订阅（`src/strategy/PairTradingStrategy.cpp:39-48`）：

```cpp
for (auto& pk : op["pairKeys"].GetArray()) {
    std::string pairKey = pk.GetString();
    m_ptCfg.pairKeys.emplace_back(pairKey);

    LOG_INFO("pre_start Subscribe pairInstrumentKey:{}", pairKey);
    SpreadManager::Instance().AddSpreadPara(pairKey);
    dbpreader->Subscribe(pairKey);
}
```

`Subscribe` 做的事（`dbp/sig/dbp/dbpreader.h:28-37`）：

```cpp
bool Subscribe(const std::string& topic) {
    UpdateByHeader();                                  // 先把新增的列头读进来
    auto iter = _pcols->find(topic);                   // 按 __name 找列
    if (iter != _pcols->end()) {
        submap[iter->second->__mnodeid] = 1;           // 只记下 mnodeid
        return true;
    }
    return false;                                      // 找不到就返回 false，且【不报错】
}
```

> **`Subscribe` 失败是静默的**：`pre_start` 没有检查返回值。如果 `op.pairKeys` 里的字符串和 `pairlist.xml` 里的 `<name>` 不完全一致（大小写、分隔符、`-USDT` 后缀任一处不同），结果就是**这个对子永远收不到行情，而日志里只会打一条 `pre_start Subscribe pairInstrumentKey:...`，看不出失败**。

### 2.5 拉取（不是推送）

真正的取数在 `heavy_work` 每轮循环里（`base_strategy.h:113-115`）：

```cpp
if (dbpreader) {
    dbpreader->FetchLast();
}
```

`FetchLast()` 遍历 `submap`，对每个订阅的列调 `spreader.FetchLast(mnodeid, &data)`（`shmpool.h:180-210`）。语义：

- **返回 0 = 没有新数据**（`readcount == __datacount`），此时**不回调**；
- **返回 >0 = 有数据，且返回值是「跳过的条数」**。它直接取 `__datacount - 1` 那个槽（最新一条），把中间的都跳过，`readcount` 一次加到最新。所以：
  - 策略处理慢的时候，**中间行情会被整批丢弃**，回调只给最新一条；
  - 回调的第三个参数 `jumpedNum` 就是跳过的条数，`PairTradingStrategy::on_dbpdata` 目前**没有使用它**（`PairTradingStrategy.cpp:125`）；
- 写者正在写那一槽时（`__wflag == 'T'`）会退一格取前一条，避免读到半成品。

所以价差这条链是 **「轮询 + 只取最新」**，没有积压重放，天然是「落后就丢」。

### 2.6 分发

```cpp
// src/strategy/PairTradingStrategy.cpp:125-129
void PairTradingStrategy::on_dbpdata(const dbp::DbpTopic* topic, const dbp::DbpData* pdata, uint32_t jumpedNum) {
    std::cout << topic->__name << " " << pdata->activeAskPrice[0] << " " << pdata->activeBidPrice[0]
              << " " << pdata->passiveAskPrice[0] << " " << pdata->passiveBidPrice[0] << std::endl;
    algoContext.OnSpread(topic, pdata);
    ptContext.OnSpread(topic, pdata);
}
```

两边都发：`algoContext` 用行情驱动子单（挂单价格/撤单判断），`ptContext` 用行情驱动信号（`UpdateRtSpread` → `ProcessPairSignal`）。

> 这里有一句**无条件 `std::cout`**，每来一条价差就打印一次。生产环境跑起来会非常吵，量大的时候还会拖慢这个线程。

### 2.7 除了四轴价差，还能拿到什么

`DbpData`（`dbp/sig/dbp/include.h:57-112`）里一起带过来的还有：

| 字段 | 含义 |
|---|---|
| `spread{BidAsk,BidBid,AskBid,AskAsk}Tema` | 四轴的 Tema 平滑值 |
| `spread{...}Max` / `spread{...}Min` | 四轴在 `maxmintime` 窗口内的极值 |
| `activeAskPrice[5]` / `activeBidPrice[5]` / 对应 Volume | 主动腿 5 档深度 |
| `passiveAskPrice[5]` / `passiveBidPrice[5]` / 对应 Volume | 被动腿 5 档深度 |
| `activeFundingRate` / `passiveFundingRate` | 当前 funding |
| `activeNextFundingRate` / `passiveNextFundingRate` | 下期 funding |
| `activePriceTema` / `passivePriceTema` | 两腿各自的 Tema 价 |
| `spreadEffective` / `statEffective` | 价差 / 统计量是否有效 |
| `activeDepthTs` / `passiveDepthTs` / `diffTs` / `generateTs` | 两腿深度时间、时间差、价差生成时间 |
| `activeDepthDelay` / `passiveDepthDelay` / `exchActiveTradeDelay` / `exchPassiveTradeDelay` | 各类延迟 |

列头 `DbpTopic`（`include.h:29-55`）则给出这对子是怎么配的：两腿 instrumentKey、六个 DBW 源 ID、`spreadDrive`（`SPD_ALL` / `SPD_LEFT` / `SPD_RIGHT`）、`spreadType`（四轴选哪个）、`spreadCalcType`（价比1/价比2/价差1/价差2）、`activeMultiply` / `passiveMultiply`（两腿调整系数，用于 1000SHIB vs SHIB 这类）、`stematime` / `tematime` / `maxmintime` / `timsspan`。

---

## 三、balance / position / total_account（pubsub 队列）

### 3.0 两类触发：启动主动查 + 之后被动推

这三类数据**不是纯推送**。启动阶段 utrade 会**主动发查询请求**，把账户状态先拉起来：

```cpp
// src/strategy/PairTradingStrategy.cpp:64-66
algoContext.Init(smc);      // → StrategyConfig::LoadConfig() 读 strategy.ini
algoContext.SetDbp(dbpreader);
algoContext.PreStart();     // → AlgoContext::QueryAccount()

// quant_library/algo/AlgoContext.cpp:58-69
void AlgoContext::QueryAccount() {
    auto& mAccountInfo = StrategyConfig::GetInstance().GetAccountInfo();
    for (auto iter = mAccountInfo.begin(); iter != mAccountInfo.end(); ++iter) {
        stra::QuantOrder order;
        order.strategyAccountId = iter->second.accountId;
        order.exchangeType = iter->second.exchangeType;
        for (size_t i = 0; i < iter->second.vInstType.size(); ++i) {
            order.instType = iter->second.vInstType[i];
            QuantTrade::Instance().QueryAccount(order);   // → tradeClient->query_account(...)
        }
    }
}
```

所以完整时序是：

```
utrade 启动 → 发 CMD_QUERY_ACCOUNT（走 §4.1 的出向队列）→ tb 查交易所 → 推 CMD_RPT_BALANCE 回来
                                                                          ↓
                              之后由 tb 的交易所 WS 持续推送（AS_WEBSOCKET）维持
```

tb 侧对 `CMD_QUERY_ACCOUNT` 的实现，多数交易所是「落成 `query_balance`」——例如币安 U 本位：

```cpp
// tb/src/binance/BinanceUFWsTrade.cpp:816-817
void BinanceUFWsTradeUnit::query_account(const pubsub::TCommand& tcmd) {
    query_balance(tcmd);          // account 靠 balance 的 total / available 表达
}
```

> **注意：启动时只主动查了 account（→ balance），没有主动查 position。** `AlgoContext::QueryAccount()` 只调 `QueryAccount`，不发 `CMD_QUERY_POSITION`。所以 **position 完全依赖 tb 的 WS 推送**——如果白名单（§3.4）或 tb 侧推送有问题，启动时不会有人帮你把仓位补回来。
>
> 反过来说，balance 因为有这一次主动查询兜底，**即使错过了停机期间的推送也能补齐**（对应 §3.2 性质 2 的「重启不补发」）。

### 3.1 生产者：`tb` 进程

这三类都是 **tb 从交易所拿到后，主动推给 utrade 的**。生产点在 tb 的交易所客户端里，以币安 U 本位 WS 为例（`tb/src/binance/BinanceUFWsTrade.cpp:608-621`）：

```cpp
pubsub::RCommand rcmd;
memset(&rcmd, 0, sizeof(pubsub::RCommand));
rcmd.cmdTypeEnum = pubsub::CMD_RPT_BALANCE;
rcmd.body.balance.exchangeTypeEnum = BINANCE;
rcmd.body.balance.instTypeEnum   = USDT_SWAP;
rcmd.body.balance.accountId      = acc.accountId;
crypto::copy_sv_to_char_array(rcmd.body.balance.accountName, acc.accountName);
crypto::copy_sv_to_char_array(rcmd.body.balance.strategyId,  acc.strategyId);   // ← 注意这里
crypto::copy_sv_to_char_array(rcmd.body.balance.currency, crypto::to_upper(std::string(a_sv)));
rcmd.body.balance.total      = crypto::fast_atod(wb_sv);
rcmd.body.balance.available  = crypto::fast_atod(cw_sv);
rcmd.body.balance.frozen     = rcmd.body.balance.total - rcmd.body.balance.available;
rcmd.body.balance.updateTime = crypto::getCurrentTime();
rcmd.body.balance.apiSourceEnum = AS_WEBSOCKET;
PUSH_RCMD(rcmd)
```

`PUSH_RCMD` 展开成 `tb2OmsRCommandInnerQueue.push(rcmd)`（`tb/include/utils/tb_global.h:8-9`），然后由 tb 的 `execute()` 线程转发到共享内存（`tb/src/operation/TbOperation.cpp:8-16`、`128-136`）：

```cpp
#define PUBLISH_RCMD(rcmd) \
    if (rcmd.cmdTypeEnum == pubsub::CMD_RPT_ORDER_RESPONSE) { \
        if (rcmd.body.orderResponse.clientOrderId != TESTCLIENTORDERID) { \
            tb2TradeRCommandPubSHM->push(rcmd); \
        } \
    } \
    else { \
        tb2TradeRCommandPubSHM->push(rcmd); \
    }

// TbOperation::execute() 里两条 tb 内部队列
if (rcmdInnerQueue.pop(rcmd)) {           // 系统内部合成的回报 → 直接发
    PUBLISH_RCMD(rcmd);
}
if (tb2OmsRCommandInnerQueue.pop(rcmd)) { // 交易所返回的回报 → 先过 OrderManager
    processRcmd(rcmd);                    // → orderManager.processRcmd → PUBLISH_RCMD
}
```

其中 `tb2TradeRCommandPubSHM` 就是 `Tb2TradeRCommandPubSHM = pubsub::SPMCPublisher<pubsub::RCommand>`（`include/shm_global.h:20`），构造时用 `/inc/trade_config.json` 的 `OMS.Tb2UtradeRCommandSHM` 字符串（`TbOperation.cpp:52`）。

> `TESTCLIENTORDERID`（`shm_global.h:10`，值 `1008610010`）是给自测单留的**过滤位**：带上这个 clientOrderId 的回报**不会**被推给 utrade。

### 3.2 传输：POSIX 共享内存环形 + SPMC

```cpp
// include/shm_global.h:23
typedef pubsub::SPMCSubscriber<pubsub::RCommand> Tb2TradeRCommandSubSHM;
```

底层是 `include/shm_spmc_queue.h` 的 `SPMCQueue<T, 1024*64>`，用 `shm_open` + `mmap` 打开，`sizeof(Q)` 定长：

```cpp
// shm_spmc_queue.h:140-147
SPMCSubscriber(const char* shm_file) {
    queue_ = shmmap_spmc<Q>(shm_file);
    if (!queue_) { throw std::runtime_error(...); }
    reader_ = queue_->getReader();       // next_idx = write_idx + 1
}
```

两个关键性质：

1. **容量 65536 槽的环形**（`kCnt = 1024*64`）。读者落后太多时，`read()` 里的 `int(new_idx - next_idx) < 0` 判断会让它**直接跳到最新**，中间的回报被丢掉，且**没有任何日志**。
2. **`getReader()` 把 `next_idx` 设成 `write_idx + 1`**，也就是**从当前位置开始读**。所以策略进程重启时，**停机期间 tb 推的 balance / position / 回报全部不会补发**。

### 3.3 进程内入口

```cpp
// include/base/base_strategy.h:29
rcmdQueue = std::make_shared<pubsub::SPMCSubscriber<pubsub::RCommand>>(tb2UtradeRCommandSHM.c_str());
```

在 `heavy_work` 里消费（`base_strategy.h:82-111`）：

```cpp
if (rcmdQueue->pop(rcmd)) {
    if (crypto::convert_rcmd_2_ordertrade(rcmd, orderResponse)) { ... on_ordertrade(...) }
    else if (crypto::convert_rcmd_2_balance(rcmd, balance))      { ... on_balance(...) }
    else if (crypto::convert_rcmd_2_position(rcmd, position))    { ... on_position(...) }
    else if (crypto::convert_rcmd_2_total_account(rcmd, totalAccount)) { ... on_total_account(...) }
    else { LOG_ERROR("it should not happen here, please contact your developer!"); }
}
```

`convert_rcmd_2_*` 就是「判 `cmdTypeEnum` + `memcpy` 对应的 union 成员」（`include/command_helper.h:9-39`），所以**每一轮循环只处理一条** `RCommand`。队列积压时这个线程会被持续占住，`FetchLast()` 和 `on_timer` 都要排在后面。

### 3.4 `_strategyIds` 过滤（会静默丢数据）

四类推送都过一遍白名单（`base_strategy.h:84-85` 等）：

```cpp
auto found = _strategyIds.find(balance.strategyId);
if (found != _strategyIds.end()) {
    on_balance(balance);
}
```

`_strategyIds` 来自 `etc/config.json` 的 `op.strategyIds`（`base_strategy.h:146-155`）。当前仓库里是：

```json
"strategyIds": ["bnt1", "gat1"],
```

> **不过滤不打日志**：`strategyId` 不在白名单里时，数据被**直接丢弃，一行日志都没有**。排查「策略收不到仓位/资金」时，这是第一个要看的地方——详见 §6 的配置对齐表。

### 3.5 分发

```cpp
// src/strategy/PairTradingStrategy.cpp:132-147
void PairTradingStrategy::on_balance(pubsub::Balance& balance) {
    algoContext.OnBalance(balance);
    ptContext.OnBalance(balance);
}
void PairTradingStrategy::on_position(pubsub::Position& position) {
    algoContext.OnPosition(position);
    ptContext.OnPosition(position);
}
void PairTradingStrategy::on_total_account(pubsub::TotalAccount& totalAccount) {
    algoContext.OnTotalAccount(totalAccount);
    ptContext.OnTotalAccount(totalAccount);
}
```

三类都是**两边都发**。`ptContext` 用它们做风控与开平仓额度判断，`algoContext` 用它们维护账户/仓位视图。

---

## 四、报单回报

报单是**唯一一条「出向」数据**，走的是和入向完全不同的通道。

### 4.1 出向：`TradeClient` → `Utrade2TbTCommandSHM` → tb

```cpp
// include/shm_global.h:13-14
typedef pubsub::ShmQueue<pubsub::TCommand> Utrade2TbTCommandSHM;   // 多写单读

// include/command_helper.h:46-48
TradeClient(int key) { utrade2TbTCommandShm = new Utrade2TbTCommandSHM(key); }
```

`BaseStrategy` 构造时建它（`base_strategy.h:25-28`）：

```cpp
int utrade2TbTCommandSHM = std::stoi(tradeConfig["OMS"]["Utrade2TbTCommandSHM"].GetString());
tradeClient = new om::TradeClient(utrade2TbTCommandSHM);
```

`TradeClient` 只有六个方法，全是「组 `TCommand` → `push`」：`add_new_order` / `cancel_order` / `query_order` / `query_account` / `query_position` / `query_balance`（`command_helper.h:57-149`）。以 `add_new_order` 为例：

```cpp
pubsub::TCommand tcmd;
memset(&tcmd, 0, sizeof(pubsub::TCommand));
tcmd.cmdTypeEnum = pubsub::CMD_NEW_ORDER;
tcmd.body.newOrder.exchangeTypeEnum = exchangeTypeEnum;
tcmd.body.newOrder.instTypeEnum     = instTypeEnum;
strncpy(tcmd.body.newOrder.strategyId, strategyId, STRATEGYID_SIZE);
strncpy(tcmd.body.newOrder.instId, instId, INSTID_SIZE);
tcmd.body.newOrder.clientOrderId = clientOrderId;
strncpy(tcmd.body.newOrder.strategyRef, strategyRef, ORDER_SIZE);
tcmd.body.newOrder.offsetFlag = offsetFlag;
tcmd.body.newOrder.direction  = direction;
tcmd.body.newOrder.orderType  = orderType;
tcmd.body.newOrder.volumeTotal = volume;
tcmd.body.newOrder.limitPrice  = price;
tcmd.body.newOrder.reduceOnly  = reduceOnly;
utrade2TbTCommandShm->push(tcmd);
```

调用方是 `quant_library/basic/QuantTrade.h:24-27`，`strategyId` 由 `StrategyConfig::GetInstance().GetStrategyIdByAccountId(order.strategyAccountId)` 给出（即 `etc/strategy.ini` 的 `[ACCOUNTn] strategyid`）。

这条通道的底层是 **System V 共享内存队列**（`include/shmqueue/shm_queue.h:7-50` → `shmmqueue::CMessageQueue`），按 **int key**（`100031`）寻址，模式 `ONE_READ_MUL_WRITE`。

tb 侧消费（`tb/src/operation/TbOperation.cpp:123-126`、`154-242`）：

```cpp
if (utrade2TbTCommandShm->pop(tcmd)) {   // 策略端的请求
    processTcmd(tcmd);                    // → orderManager.processTcmd → tcmdInnerQueue
}
```

`executeTcmd()` 按 `cmdTypeEnum` 找 `mTradeClient[get_tradeclient_key(exchId, strategyId)]` 再调对应交易所客户端。

> **`Utrade2TbTCommandShm` 的构造函数会把队列头 `m_iBegin` / `m_iEnd` 归零**（`include/shmqueue/shmmqueue.cpp:20-25`），而 `CreateShareMem` 在段已存在时是**附着而不是重建**（`shmmqueue.cpp:352-393`）。两个进程都会 `new` 一次这个队列，所以 **谁后启动，谁就把队列清空**。启动顺序需要固定（见 §6）。

### 4.2 入向：回报从同一条入向队列回来

回报分**两种来源**，都汇进 `Tb2UtradeRCommandSHM`，靠 `OrderResponse.apiSourceEnum` 区分（枚举见 `include/data_struct.h:212-219`）：

| 来源 | 谁产生 | tb 内部队列 | 是否过 `OrderManager` | `apiSourceEnum` |
|---|---|---|---|---|
| **本地受理回报**（合成） | `OrderManager::processTcmd` | `rcmdInnerQueue`（SPSC） | ❌ 直接 `PUBLISH_RCMD` | `AS_ADD_NEW_ORDER` / `AS_CANCEL_ORDER` / `AS_QUERY_ORDER` |
| **交易所真实回报** | 各交易所客户端（WS / REST） | `tb2OmsRCommandInnerQueue` | ✅ 先 `processRcmd` 做映射与状态校验 | `AS_WEBSOCKET` / `AS_REST` |

**本地受理回报是提前发的**：utrade 的 `TCommand` 一到 tb，`OrderManager::processTcmd` 就立刻合成一条带 `orderSysId` 的回报推进 `rcmdInnerQueue`（`tb/src/oms/OrderManager.cpp:57-67`）：

```cpp
case pubsub::CMD_NEW_ORDER: {
    if (tcmd.body.newOrder.clientOrderId == TESTCLIENTORDERID) {
        return true;                     // 自测单：只走本地，不推给交易所
    }
    ADD_NEW_ORDER_2_ORDER_RESPONSE(tcmd) // 合成 rcmd，apiSourceEnum = AS_ADD_NEW_ORDER
    rcmdInnerQueue.push(rcmd);
    strncpy(tcmd.body.newOrder.orderSysId, rcmd.body.orderResponse.orderSysId, ORDER_SIZE);
    orderSysId2OrderResponseMap[rcmd.body.orderResponse.orderSysId] = rcmd;
    const std::string& clientOrderIdStra = fmt::format("{}{}", rcmd.body.orderResponse.strategyId, rcmd.body.orderResponse.clientOrderId);
    clientOrderId2OrderSysIdMap[clientOrderIdStra] = rcmd.body.orderResponse.orderSysId;
    return true;
}
```

所以 **utrade 会先收到一条「本地已受理」的回报（带 tb 生成的 orderSysId），之后才收到交易所的成交/状态回报**。撤单路径同理，`processTcmd` 的 `CMD_CANCEL_ORDER` 分支会先推一条 `orderStatus = OS_CANCELLING` 的回报（`OrderManager.cpp:74-82`）。

交易所真实回报走 `processRcmd`（`TbOperation.cpp:244-251`）：

```cpp
void TbOperation::processRcmd(pubsub::RCommand& rcmd) {
    if (orderManager.processRcmd(rcmd)) {
        PUBLISH_RCMD(rcmd);          // → Tb2UtradeRCommandSHM
    } else {
        LOG_INFO("tb will not publish rcmd: {}", rcmd.getString());
    }
}
```

`OrderManager::processRcmd` 负责把交易所的 orderSysId/orderId 和策略的 `clientOrderId` 对上，并且**回填 `strategyId`**（`tb/src/oms/OrderManager.cpp:15`）：

```cpp
strncpy(rcmd.body.orderResponse.strategyId, tcmd.body.newOrder.strategyId, 32);
```

于是回报和 balance / position **走的是同一条 `Tb2UtradeRCommandSHM`**，靠 `cmdTypeEnum == CMD_RPT_ORDER_RESPONSE` 区分，进 utrade 后由 `convert_rcmd_2_ordertrade` 认出来。

utrade 侧进 `on_ordertrade`，**只发给 `algoContext`**（`PairTradingStrategy.cpp:150-152`）：

```cpp
void PairTradingStrategy::on_ordertrade(pubsub::OrderResponse& orderResponse) {
    algoContext.OnOrder(orderResponse);
}
```

> `ptContext` **拿不到成交回报**。`ptContext` 只能通过 `on_position` / `on_balance` 间接感知成交结果，或者靠 `on_timer` 里的 `ScanFinishedAlgoOrders` 兜底扫描。设计如此，但在排查「信号层为什么不知道已经成交了」时要记住这一点。

`OrderResponse` 里可用的字段见 `include/pubsub_protocol.h:182-224`：`orderStatus`（`OS_CANCEL` 等）、`volumeTotal` / `volumeTraded` / `tradePrice` / `tradeDiff`（本次成交量）/ `fillPrice`（本次成交价）、`errorId` / `originMsg`、`apiSourceEnum`（`AS_WEBSOCKET` / `AS_REST`）。

---

## 五、定时器

`on_timer` **不是数据源**，是 `heavy_work` 自己合成的事件（`base_strategy.h:117-121`）：

```cpp
auto now = crypto::getCurrentTime();
if (now - utcTime >= timerInterval * 1000) {     // timerInterval 单位 ms，getCurrentTime 是 µs
    on_timer(now);
    utcTime = now;
}
```

`timerInterval` 来自 `etc/config.json` 的 `op.timerInterval`，当前是 **1000**（`base_strategy.h:146`）。

`PairTradingStrategy::on_timer` 在此基础上再叠一层兜底扫描（`PairTradingStrategy.cpp:539-551`）：
注意 `SCAN_INTERVAL_US = 200000` 只是**节流阀** —— 在 `timerInterval` = 1000ms 的 tick 下
`1000ms >= 200ms` 恒成立，节流阀从不生效，所以实际节奏 = **每个 tick 一次**（≈1s），不是 200ms：

```cpp
algoContext.OnTimer(utcTime);
ptContext.OnTimer(utcTime);

if (utcTime - m_lastScanUs >= SCAN_INTERVAL_US) {   // SCAN_INTERVAL_US = 200000
    ScanFinishedAlgoOrders(utcTime);
    m_lastScanUs = utcTime;
}
```

> 因为 `on_timer` 的触发条件是「`now - utcTime >= 1000ms`」，而循环本身是忙等的（`NEED_SLEEP` 未定义时**没有任何 sleep**），实际定时精度取决于行情/回报把这一轮循环拖了多久：**队列积压时定时器会整体后移**，不是严格 1s。

> `PairTradingStrategy::on_timer` 里还有一段**临时测试钩子**（`:116-122`）：每 10s 如果 `!createAlgo`，就调一次 `algoContext.OnCommand("")` 造一张硬编码测试算法单。上线前需要摘掉。

---

## 六、配置对齐表（跨进程）

链路上有**四组字符串/整数必须在多个文件里一致**，任何一组不一致都会表现为「收不到数据」或「发不出单」，且多数情况下**没有报错**。

| # | 要对齐的值 | 出现位置 | 不一致的后果 |
|---|---|---|---|
| 1 | `DBP.mpath` / `DBP.dpath` | `tb/etc/trade_config.json`、部署机 `/inc/trade_config.json`、`dbp/etc/dbprocess.json` | 版本号不同（例：`dbprocess.json` 是 `20230223`，`tb/etc/trade_config.json` 是 `20230328`）→ reader 打不开 / 读到空列 → **完全收不到价差** |
| 2 | 对子名 | `dbp/etc/pairlist.xml` 的 `<name>` ↔ `utrade_hft/etc/config.json` 的 `op.pairKeys[]` | `DbpReader::Subscribe` 返回 `false` 但**不报错** → 该对子**永远收不到价差** |
| 3 | `OMS.Utrade2TbTCommandSHM` / `OMS.Tb2UtradeRCommandSHM` | tb 与 utrade 都读 `/inc/trade_config.json` 的同一份 | key 不同 → 队列对不上，**下单发不出去 / 回报收不到** |
| 4 | `strategyId` | ① `utrade_hft/etc/config.json` `op.strategyIds`（入向白名单）<br>② `utrade_hft/etc/strategy.ini` `[ACCOUNTn] strategyid`（出向打标）<br>③ `tb/etc/trade_config.json` `TB.tb_accounts.<name>[0].strategyId`（tb 侧路由键） | 见下 |

第 4 组目前仓库里的三份样例**互不相同**：

- `utrade_hft/etc/config.json` → `op.strategyIds = ["bnt1", "gat1"]`
- `utrade_hft/etc/strategy.ini` → `[ACCOUNT1] accountid = 10000, strategyid = test1`
- `tb/etc/trade_config.json` → `TB.tb_accounts.test1[0] = { "strategyId": "sss_test1", "accountId": 10000 }`

按代码推演，这三处不一致会导致：

1. **下单发不出去**：utrade 用 `strategy.ini` 的 `test1` 填 `newOrder.strategyId`；tb 的 `mTradeClient` 是按 `get_tradeclient_key(exchId, "sss_test1")` 建的（`TbOperation.cpp:70`），`executeTcmd` 又用 `get_tradeclient_key(exchId, "test1")` 去查（`:161`）→ 查不到，只打一条 `LOG_ERROR("not found exchIdAccountKey: ...")`。
2. **即使发出去了，回报也被丢**：`OrderManager` 把 `test1` 回填进 `orderResponse.strategyId`，而 utrade 的白名单是 `["bnt1","gat1"]` → 静默丢弃。
3. **balance / position 全丢**：tb 推的 `strategyId` 是 `sss_test1` → 同样不在白名单 → 静默丢弃。

> 这是**样例配置**层面的问题，部署机上可能是对齐的。但「三处必须写成同一个值」这个约束是代码强制的，值得写进部署检查清单。

---

## 七、需要注意/已知的静默点

按「出问题时最难发现」排序：

| # | 位置 | 现象 | 参考 |
|---|---|---|---|
| 1 | `_strategyIds` 白名单过滤 | `strategyId` 不匹配 → **静默丢数据，无日志** | `base_strategy.h:84-105` |
| 2 | `DbpReader::Subscribe` | 对子名不匹配 → 返回 `false` 但 `pre_start` 不检查 → **静默收不到行情** | `dbpreader.h:28-37` |
| 3 | `SPMCSubscriber` 构造 | `getReader()` 从当前写位置开始 → **重启期间回报不补发** | `shm_spmc_queue.h:51-56` |
| 4 | `SPMCQueue::read()` | 落后超过 65536 条 → **跳到最新，无日志** | `shm_spmc_queue.h:22-31` |
| 5 | `sp::Reader::FetchLast` | 落后时**只取最新一条**，中间全丢；`jumpedNum` 传了但没人用 | `shmpool.h:180-210` |
| 6 | `ShmQueue` 构造 | 队列头 `m_iBegin`/`m_iEnd` 归零 → **后启动的一方清空对方队列** | `shmmqueue.cpp:20-25` |
| 7 | `on_dbpdata` 的 `std::cout` | 每条价差无条件打印 → 噪音 + 拖慢线程 | `PairTradingStrategy.cpp:126` |
| 8 | `heavy_work` 忙等 | `NEED_SLEEP` 未定义 → 无 sleep，空转吃满一核 | `base_strategy.h:127-129` |
| 9 | `on_timer` 的测试钩子 | 每 10s 造一张硬编码算法单 → **上线前必须摘** | `PairTradingStrategy.cpp:116-122` |

---

## 八、本文的边界（未展开的部分）

- **DBS 层**：`dbp/dbsource/dbsinitialer.cpp` 初始化的是 `/dev/shm/DBS_TOPIC_20230223` / `DBS_DATAS_20230223`（`dbsource/dbsource.xml`），也就是「交易所行情 → 共享内存」这一段。本次只确认了落点和 `exchlist.xml` 的交易所/市场类型清单（`SPOTMD` / `USDTMD` / `USDMD`），**没有逐行读 dbsource 的写入逻辑**。
- **DBW 层**：`dbp/sig/dbw/include.h` 定义 `dbw::DBTopic { md::MarketType t; md::CryptoMarketData d; md::InstrumentInfo i; }` / `dbw::DBdata { md::CryptoMarketData d; }`，`dbprocess` 从它读原始行情。谁写 DBW 未追。
- **tb 各交易所客户端的回报解析**：只读了币安 U 本位 WS 的 balance 片段作为样本，OKX / GATEIO / BYBIT 未逐一核对（结构一致）。
- **`StrategyConfig::LoadConfig()` 的调用点**：`StrategyConfig.cpp:20` 用的是相对路径 `read_ini("strategy.ini", ...)`，也就是**从进程 CWD 读**，所以启动时的 CWD 必须是 `etc/` 所在目录。启动脚本需要确认这一点。
