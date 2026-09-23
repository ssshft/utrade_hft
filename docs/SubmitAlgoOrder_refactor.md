# SubmitAlgoOrder 重构方案：从 JSON 字符串改为直接创建对象

> **⚠️ 本文档的推荐方案（新增 `AlgoOrderRequest.h`）已被否决，代码按"方案 A 变体"落地。**
> 实际实现见文末《已实施记录》。本文档保留作背景分析（现状问题、字段归属、量级估算仍然有效）。

> 目标：`PairTradingContext::SubmitAlgoOrder` 不再拼 JSON 字符串，改为直接产出类型化请求；
> `AlgoContext` 负责建对象 + 填字段 + 注册；`OnCommand` 只保留外部控制通道（CANCEL / MODIFY / QUERY）。
> 本文档只给方案，不改动任何源文件。

---

## 1. 现状数据流

```
ProcessPairSignal / ProcessRisk                    PairTradingContext.cpp:70 / :133
   └─ SubmitAlgoOrder(pi, "TT", "CL", forgoProfit)                  :162
        ├─ BuildAlgoOrderJson(...)                                  :181   ← 拼 JSON（未实现完）
        ├─ pim.SetActiveAlgoOrder(pairKey, algoId)                  :174
        └─ m_algoCommandCb(json)                                    :176
              └─ PairTradingStrategy::SubmitAlgoCommand             PairTradingStrategy.cpp:78
                    └─ AlgoContext::OnCommand(json)                 AlgoContext.cpp:79
                         ├─ rapidjson 解析整包
                         ├─ new AlgoPairOrder / AlgoFishingOrder / AlgoRebalanceOrder   :221 / :223 / :225
                         ├─ 逐字段 if (body.HasMember("x")) p->x = ...   ← 约 600 行（:266 - :688）
                         └─ 注册：commandType / algoOrderStatus / algoType / algoOrderId /
                                  Init / InsertAlgoOrderByAlgoOrder /
                                  GeneratePubStr / WriteAlgoOrder / 订阅 spread        :711 - :776
```

### 四个具体问题

1. **`BuildAlgoOrderJson` 是个空壳**。`PairTradingContext.cpp:275-278` 只拼了 `{"commandType"}` 就 return，
   前面算好的 `startSpread / endSpread / startVolume / endVolume / sw` 全部丢弃，等于没有任何字段传出去。
2. **`OnCommand` 的 NEW 分支整段被注释**。`AlgoContext.cpp:81-828` 全在 `/* */` 内，
   实际运行的是 `:832` 起的硬编码 DOGE 测试单（`new AlgoPairOrder()`，且**没有设 `algoType`**，
   而 `algoType` 在 `:1044 / :1225 / :1484 / :1615 / :1914` 五处决定整条交易流程）。
3. **字段靠字符串名匹配**。约 600 行 `if (body.HasMember("x"))`，名字写错编译期不报错，运行期静默丢字段。
4. **两层之间传字符串**，多一次序列化 + 反序列化 + 一批 `stod/stoll/stoi` 转换。

---

## 2. 方案选型

| 方案 | 做法 | 评价 |
|---|---|---|
| **A** | `PairTradingContext` 直接 `new AlgoPairOrder()` 填完字段，把裸指针交给 `AlgoContext::SubmitAlgoOrder(BaseAlgoOrder*)` 注册 | 最贴 `PairTradingContext.cpp:178` 的现有注释；但策略层要知道具体算法单类，且 `AlgoOrderManager` 在 `AlgoContext` 私有，仍要绕一层 |
| **B** | 定义 `AlgoOrderRequest` 结构体，`AlgoContext::SubmitAlgoOrder(const AlgoOrderRequest&)` 内部 new + 填 + 注册 | **推荐**。策略层不依赖算法单类；查重 / 订阅 / 持久化仍集中在 AlgoContext |
| C | 保留 JSON，只把 `BuildAlgoOrderJson` 补完 | 不满足"不拼 JSON"的要求 |

下面按 **方案 B** 展开。

---

## 3. 具体改动

### 3.1 新增 `quant_library/basic/AlgoOrderRequest.h`

与 `PairInfo.h` 同级（避免 `AlgoContext.h` 反向依赖 `PairTradingContext.h`）。

```cpp
#pragma once

#include "PairInfo.h"      // pt::OrderParams
#include "DataStruct.h"

namespace pt {

// 创建算法单所需的全部入参，字段与 BaseAlgoOrder 一一对应。
// 由 AlgoContext::SubmitAlgoOrder 负责套用，策略层只填不判断。
struct AlgoOrderRequest {
    // ---- 身份 ----
    stra::AlgoType algoType{stra::AlgoType_PairTrading};
    std::string algoStrategyName;
    std::string pairInstrumentKey;
    std::string activeInstrumentKey;
    std::string passiveInstrumentKey;
    std::string baseAsset{"USDT"};
    std::string clientOrderId;                  // 可选，外部幂等用

    // ---- 账户 ----
    int activeAccountId{0};
    int passiveAccountId{0};

    // ---- 腿属性 ----
    stra::DriveType activeDriveType{stra::DriveType_ACTIVE};
    stra::DriveType passiveDriveType{stra::DriveType_PASSIVE};
    bool activeDepthMakerCheck{false};
    bool activeDepthTakerCheck{false};
    bool passiveDepthMakerCheck{false};
    bool passiveDepthTakerCheck{false};
    OrderType activeOrderType{OT_LIMIT};
    OrderType passiveOrderType{OT_LIMIT};
    double activePriceTakerPct{0.0};
    double activePriceMakerPct{0.0};
    double passivePriceTakerPct{0.0};
    double passivePriceMakerPct{0.0};
    double passiveVolumePct{0.5};

    // ---- 撤单参数 ----
    int64_t activeMakerCancelOrderTime{300LL * 1000 * 1000};
    int64_t activeTakerCancelOrderTime{5LL * 1000 * 1000};
    int64_t passiveMakerCancelOrderTime{60LL * 1000 * 1000};
    int64_t passiveTakerCancelOrderTime{5LL * 1000 * 1000};
    double activePassiveCancelOrderPct{0.0};
    double activeMakerCancelOrderPct{0.0};
    double activeTakerCancelOrderPct{0.0};
    double passiveMakerCancelOrderPct{0.0};
    double passiveTakerCancelOrderPct{0.0};

    // ---- 费率 / 滑点 ----
    double activeMakerFeeRate{0.0};
    double activeTakerFeeRate{0.0};
    double passiveMakerFeeRate{0.0};
    double passiveTakerFeeRate{0.0};
    double activeMakerSlippage{0.0};
    double activeTakerSlippage{0.0};
    double passiveMakerSlippage{0.0};
    double passiveTakerSlippage{0.0};

    // ---- 持仓 / 报单量 ----
    double pairTotalVolume{0.0};
    double pairActiveTotalPrice{-1.0};
    double pairPassiveTotalPrice{-1.0};
    double pairPassiveTotalVolume{0.0};
    double ttTargetVolume{0.0};
    double mtTargetVolume{0.0};
    double minVolume{0.0};
    double maxMTOrderSize{1.0};
    double maxTTOrderSize{1.0};

    // ---- 开关 / 策略参数 ----
    bool profitSwitch{false};
    double profitPct{0.0};
    stra::TargetSpredPrice targetSpreadType{stra::TargetSpredPrice_NOW};
    stra::ActiveVolumeCalcualteType activeVolumeCalcualteType{
        stra::ActiveVolumeCalcualteType_PassiveVolumePct};
    bool mtRebalanceSwitch{true};
    bool ttRebalanceSwitch{true};
    bool mtRebalanceFlag{true};
    bool ttRebalanceFlag{true};
    bool mtPriceTrendProtectFlag{false};
    bool ttPriceTrendProtectFlag{false};
    bool activePriceTickFlag{false};
    double activePriceTickNum{0.0};
    bool passivePriceTickFlag{false};
    int passivePriceTickNum{0};
    bool isManual{false};

    // ---- 32 个开平仓触发参数（直接复用 pt::OrderParams）----
    OrderParams orderParams;

    // ---- 子类专属 ----
    double fishingSlippagePct{0.0};   // AlgoFishingOrder
    int activeTrade{-1};              // AlgoRebalanceOrder
};

} // namespace pt
```

### 3.2 `BaseAlgoOrder` 增加批量套用入口

避免在 `AlgoContext` 里手抄 32 行。

`BaseAlgoOrder.h`：加 `#include "PairInfo.h"`（`PairInfo.h` 只依赖 `DataStruct.h`，无循环依赖），并加声明：

```cpp
void ApplyOrderParams(const pt::OrderParams& op);
```

`BaseAlgoOrder.cpp`：

```cpp
void BaseAlgoOrder::ApplyOrderParams(const pt::OrderParams& op) {
    ttOLStartSpread = op.ttOLStartSpread; ttOLEndSpread = op.ttOLEndSpread;
    ttOLStartVolume = op.ttOLStartVolume; ttOLEndVolume = op.ttOLEndVolume;
    ttOLSwitch      = op.ttOLSwitch;

    ttCLStartSpread = op.ttCLStartSpread; ttCLEndSpread = op.ttCLEndSpread;
    ttCLStartVolume = op.ttCLStartVolume; ttCLEndVolume = op.ttCLEndVolume;
    ttCLSwitch      = op.ttCLSwitch;

    ttOSStartSpread = op.ttOSStartSpread; ttOSEndSpread = op.ttOSEndSpread;
    ttOSStartVolume = op.ttOSStartVolume; ttOSEndVolume = op.ttOSEndVolume;
    ttOSSwitch      = op.ttOSSwitch;

    ttCSStartSpread = op.ttCSStartSpread; ttCSEndSpread = op.ttCSEndSpread;
    ttCSStartVolume = op.ttCSStartVolume; ttCSEndVolume = op.ttCSEndVolume;
    ttCSSwitch      = op.ttCSSwitch;

    mtOLStartSpread = op.mtOLStartSpread; mtOLEndSpread = op.mtOLEndSpread;
    mtOLStartVolume = op.mtOLStartVolume; mtOLEndVolume = op.mtOLEndVolume;
    mtOLSwitch      = op.mtOLSwitch;

    mtCLStartSpread = op.mtCLStartSpread; mtCLEndSpread = op.mtCLEndSpread;
    mtCLStartVolume = op.mtCLStartVolume; mtCLEndVolume = op.mtCLEndVolume;
    mtCLSwitch      = op.mtCLSwitch;

    mtOSStartSpread = op.mtOSStartSpread; mtOSEndSpread = op.mtOSEndSpread;
    mtOSStartVolume = op.mtOSStartVolume; mtOSEndVolume = op.mtOSEndVolume;
    mtOSSwitch      = op.mtOSSwitch;

    mtCSStartSpread = op.mtCSStartSpread; mtCSEndSpread = op.mtCSEndSpread;
    mtCSStartVolume = op.mtCSStartVolume; mtCSEndVolume = op.mtCSEndVolume;
    mtCSSwitch      = op.mtCSSwitch;
}
```

### 3.3 `AlgoContext` 新增 `SubmitAlgoOrder(const AlgoOrderRequest&)`

`AlgoContext.h`：

```cpp
#include "basic/AlgoOrderRequest.h"
// ...
    void SubmitAlgoOrder(const pt::AlgoOrderRequest& req);   // 直接建单并注册
```

`AlgoContext.cpp`：把原 `:711-776` 的"注册"逻辑抽出来，前面加字段套用。

```cpp
void AlgoContext::SubmitAlgoOrder(const pt::AlgoOrderRequest& req) {
    try {
        // 1) 查重：同一 pairInstrumentKey 只允许一个算法单（Fishing 除外）
        auto& allAlgoOrders = alogOrderManager.GetAllAlgoOrders();
        for (auto iter = allAlgoOrders.begin(); iter != allAlgoOrders.end(); ++iter) {
            if (!req.clientOrderId.empty() &&
                strcmp(iter->second->clientOrderId, req.clientOrderId.c_str()) == 0) {
                LOG_ERROR("duplicated clientOrderId: {}", req.clientOrderId);
                return;
            }
            if (strcmp(iter->second->pairInstrumentKey, req.pairInstrumentKey.c_str()) == 0 &&
                req.algoType != stra::AlgoType_FishingTrading) {
                LOG_ERROR("duplicated pairInstrumentKey: {}", req.pairInstrumentKey);
                return;
            }
        }

        // 2) 建对象
        BaseAlgoOrder* p = nullptr;
        switch (req.algoType) {
            case stra::AlgoType_PairTrading:    p = new AlgoPairOrder();      break;
            case stra::AlgoType_FishingTrading: p = new AlgoFishingOrder();   break;
            case stra::AlgoType_Rebalance:      p = new AlgoRebalanceOrder(); break;
            default:
                LOG_ERROR("unsupported algoType: {}", int(req.algoType));
                return;
        }

        // 3) 套字段
        p->algoType        = req.algoType;
        p->commandType     = stra::CommandType_TRADING;
        p->algoOrderStatus = stra::ALGO_OS_NEW;
        p->algoOrderId     = GenerateStrategyAlgoPairId();
        p->insertTime      = crypto::getCurrentTime();

        strncpy(p->algoStrategyName,    req.algoStrategyName.c_str(),    stra::NAME_LEN);
        strncpy(p->pairInstrumentKey,   req.pairInstrumentKey.c_str(),   stra::INST_KEY_LEN);
        strncpy(p->activeInstrumentKey, req.activeInstrumentKey.c_str(), stra::INST_KEY_LEN);
        strncpy(p->passiveInstrumentKey,req.passiveInstrumentKey.c_str(),stra::INST_KEY_LEN);
        strncpy(p->baseAsset,           req.baseAsset.c_str(),           stra::ASSET_LEN);
        if (!req.clientOrderId.empty()) {
            strncpy(p->clientOrderId, req.clientOrderId.c_str(), stra::ID_LEN);
        }

        p->activeAccountId        = req.activeAccountId;
        p->passiveAccountId       = req.passiveAccountId;
        p->activeDriveType        = req.activeDriveType;
        p->passiveDriveType       = req.passiveDriveType;
        p->activeDepthMakerCheck  = req.activeDepthMakerCheck;
        p->activeDepthTakerCheck  = req.activeDepthTakerCheck;
        p->passiveDepthMakerCheck = req.passiveDepthMakerCheck;
        p->passiveDepthTakerCheck = req.passiveDepthTakerCheck;
        p->activeOrderType        = req.activeOrderType;
        p->passiveOrderType       = req.passiveOrderType;
        p->activePriceTakerPct    = req.activePriceTakerPct;
        p->activePriceMakerPct    = req.activePriceMakerPct;
        p->passivePriceTakerPct   = req.passivePriceTakerPct;
        p->passivePriceMakerPct   = req.passivePriceMakerPct;
        p->passiveVolumePct       = req.passiveVolumePct;

        p->activeMakerCancelOrderTime   = req.activeMakerCancelOrderTime;
        p->activeTakerCancelOrderTime   = req.activeTakerCancelOrderTime;
        p->passiveMakerCancelOrderTime  = req.passiveMakerCancelOrderTime;
        p->passiveTakerCancelOrderTime  = req.passiveTakerCancelOrderTime;
        p->activePassiveCancelOrderPct  = req.activePassiveCancelOrderPct;
        p->activeMakerCancelOrderPct    = req.activeMakerCancelOrderPct;
        p->activeTakerCancelOrderPct    = req.activeTakerCancelOrderPct;
        p->passiveMakerCancelOrderPct   = req.passiveMakerCancelOrderPct;
        p->passiveTakerCancelOrderPct   = req.passiveTakerCancelOrderPct;

        p->activeMakerFeeRate   = req.activeMakerFeeRate;
        p->activeTakerFeeRate   = req.activeTakerFeeRate;
        p->passiveMakerFeeRate  = req.passiveMakerFeeRate;
        p->passiveTakerFeeRate  = req.passiveTakerFeeRate;
        p->activeMakerSlippage  = req.activeMakerSlippage;
        p->activeTakerSlippage  = req.activeTakerSlippage;
        p->passiveMakerSlippage = req.passiveMakerSlippage;
        p->passiveTakerSlippage = req.passiveTakerSlippage;
        p->takerTakerFs = p->activeTakerFeeRate + p->passiveTakerFeeRate
                        + p->activeTakerSlippage + p->passiveTakerSlippage;
        p->makerTakerFs = p->activeMakerFeeRate + p->passiveTakerFeeRate
                        + p->activeMakerSlippage + p->passiveTakerSlippage;

        p->pairTotalVolume        = req.pairTotalVolume;
        p->pairActiveTotalPrice   = req.pairActiveTotalPrice;
        p->pairPassiveTotalPrice  = req.pairPassiveTotalPrice;
        p->pairPassiveTotalVolume = req.pairPassiveTotalVolume;
        p->ttTargetVolume = req.ttTargetVolume;
        p->mtTargetVolume = req.mtTargetVolume;
        p->minVolume      = req.minVolume;
        p->maxMTOrderSize = req.maxMTOrderSize;
        p->maxTTOrderSize = req.maxTTOrderSize;

        p->profitSwitch              = req.profitSwitch;
        p->profitPct                 = req.profitPct;
        p->targetSpreadType          = req.targetSpreadType;
        p->activeVolumeCalcualteType = req.activeVolumeCalcualteType;
        p->mtRebalanceSwitch         = req.mtRebalanceSwitch;
        p->ttRebalanceSwitch         = req.ttRebalanceSwitch;
        p->mtRebalanceFlag           = req.mtRebalanceFlag;
        p->ttRebalanceFlag           = req.ttRebalanceFlag;
        p->mtPriceTrendProtectFlag   = req.mtPriceTrendProtectFlag;
        p->ttPriceTrendProtectFlag   = req.ttPriceTrendProtectFlag;
        p->activePriceTickFlag       = req.activePriceTickFlag;
        p->activePriceTickNum        = req.activePriceTickNum;
        p->passivePriceTickFlag      = req.passivePriceTickFlag;
        p->passivePriceTickNum       = req.passivePriceTickNum;
        p->isManual                  = req.isManual;

        p->ApplyOrderParams(req.orderParams);

        // 4) 子类专属
        if (req.algoType == stra::AlgoType_FishingTrading) {
            ((AlgoFishingOrder*)p)->fishingSlippagePct = req.fishingSlippagePct;
        } else if (req.algoType == stra::AlgoType_Rebalance) {
            ((AlgoRebalanceOrder*)p)->activeTrade = req.activeTrade;
        }

        // 5) 注册（原 :711-776 的逻辑）
        p->Init(smc);
        alogOrderManager.InsertAlgoOrderByAlgoOrder(p);
        rLarkMsg.Push(p->GeneratePubStr());
        WriteAlgoOrder(p);

        if (!SpreadManager::Instance().IsPairInstrumentKeyExist(p->pairInstrumentKey)) {
            LOG_INFO("Subscribe pairInstrumentKey:{}", p->pairInstrumentKey);
            SpreadManager::Instance().AddSpreadPara(p->pairInstrumentKey);
            QuantDbp::Instance().Subscribe(p->pairInstrumentKey);
        } else {
            LOG_INFO("Not Subscribe pairInstrumentKey:{} already exist!", p->pairInstrumentKey);
        }
    } catch (StraException& e) {
        LOG_ERROR("SubmitAlgoOrder StraException: {}", e.what());
    } catch (exception& e) {
        LOG_ERROR("SubmitAlgoOrder error: {}", e.what());
    }
}
```

### 3.4 `PairTradingContext` 改造

`PairTradingContext.h`：

```cpp
#include "../basic/AlgoOrderRequest.h"
// ...
using AlgoSubmitCallback = std::function<void(const pt::AlgoOrderRequest& req)>;

    void SetAlgoSubmitCallback(AlgoSubmitCallback cb) { m_algoSubmitCb = std::move(cb); }
// ...
private:
    AlgoSubmitCallback m_algoSubmitCb;
    // 删除：AlgoCommandCallback / m_algoCommandCb / BuildAlgoOrderJson 声明
```

`PairTradingContext.cpp`：删掉 `BuildAlgoOrderJson` 整个函数（`:181-279`），`SubmitAlgoOrder` 改成：

```cpp
void PairTradingContext::SubmitAlgoOrder(const PairInfo& pi, const std::string& algoMode,
                                        const std::string& direction, double forgoProfit) const {
    if (!m_algoSubmitCb) {
        return;
    }

    const auto& op = pi.orderParams;
    const bool isTT    = (algoMode == "TT");
    const bool isClose = (direction == "CL" || direction == "CS");

    // 保留原 :271-273 的报单量有效性检查
    double targetVolume = isTT ? pi.ttTargetVolume : pi.mtTargetVolume;
    if (std::isnan(targetVolume) || targetVolume <= 0.0) {
        return;
    }

    pt::AlgoOrderRequest req;
    req.algoType             = stra::AlgoType_PairTrading;
    req.algoStrategyName     = m_cfg.algoStrategyName;
    req.pairInstrumentKey    = pi.pairInstrumentKey;
    req.activeInstrumentKey  = pi.activeInstrumentKey;
    req.passiveInstrumentKey = pi.passiveInstrumentKey;
    req.baseAsset            = baseAsset;
    req.activeAccountId      = pi.activeAccountId;
    req.passiveAccountId     = pi.passiveAccountId;

    // 腿属性 / 费率 / 撤单参数 —— 来自配置，见 §4.1
    req.activeOrderType  = isTT ? OT_MARKET : OT_LIMIT;   // 见 §5 待确认
    req.passiveOrderType = OT_MARKET;
    req.activeDriveType  = stra::DriveType_ACTIVE;
    req.passiveDriveType = stra::DriveType_PASSIVE;
    req.passiveVolumePct = m_cfg.passiveVolumePct;
    req.maxMTOrderSize   = m_cfg.maxMTOrderSize;
    req.maxTTOrderSize   = m_cfg.maxTTOrderSize;
    // ... 其余费率 / 滑点 / xxxCancelOrderTime / xxxCancelOrderPct 同样从 m_cfg 拷过来

    // 32 个触发参数整体拷贝，再按 forgoProfit 修正（原 :251-265 的语义）
    req.orderParams = op;
    if (isClose && forgoProfit > 0.0) {
        if (direction == "CL") {
            req.orderParams.ttCLStartSpread -= forgoProfit;
            req.orderParams.ttCLEndSpread   -= forgoProfit;
            req.orderParams.mtCLStartSpread -= forgoProfit;
            req.orderParams.mtCLEndSpread   -= forgoProfit;
        } else {
            req.orderParams.ttCSStartSpread += forgoProfit;
            req.orderParams.ttCSEndSpread   += forgoProfit;
            req.orderParams.mtCSStartSpread += forgoProfit;
            req.orderParams.mtCSEndSpread   += forgoProfit;
        }
    }

    // 开关校验（原 :251-254 的语义）
    bool sw = false;
    if (isTT) {
        sw = (direction == "OL") ? op.ttOLSwitch
           : (direction == "OS") ? op.ttOSSwitch
           : (direction == "CL") ? op.ttCLSwitch : op.ttCSSwitch;
    } else {
        sw = (direction == "OL") ? op.mtOLSwitch
           : (direction == "OS") ? op.mtOSSwitch
           : (direction == "CL") ? op.mtCLSwitch : op.mtCSSwitch;
    }
    if (!sw && !isClose && forgoProfit == 0.0) {
        return;
    }

    // 持仓 / 报单量
    req.pairTotalVolume        = pi.pairTotalVolume;
    req.pairActiveTotalPrice   = pi.pairActiveTotalPrice;
    req.pairPassiveTotalPrice  = pi.pairPassiveTotalPrice;
    req.pairPassiveTotalVolume = pi.pairPassiveTotalVolume;
    req.ttTargetVolume = pi.ttTargetVolume;
    req.mtTargetVolume = pi.mtTargetVolume;
    req.minVolume      = pi.minVolume;
    req.profitSwitch   = pi.profitSwitch;
    req.profitPct      = pi.profitPct;
    req.isManual       = pi.manualFlag;

    auto algoId = GenerateAlgoOrderId();
    req.clientOrderId = algoId;
    PairInfoManager::Instance().SetActiveAlgoOrder(pi.pairInstrumentKey, algoId.c_str());

    m_algoSubmitCb(req);
}
```

### 3.5 `PairTradingStrategy` 接线

```cpp
ptContext.SetAlgoSubmitCallback([this](const pt::AlgoOrderRequest& req) {
    algoContext.SubmitAlgoOrder(req);
});
```

`PairTradingStrategy::SubmitAlgoCommand(json)` 可删。

### 3.6 `AlgoContext::OnCommand` 收尾

- **保留** CANCEL / MODIFY / QUERY（`:255-263`、`:778-811`）——这是外部控制通道，仍走 JSON。
- **NEW 分支**（`:711-776`）：整段删除。若还要支持外部（SCC）下发新建单，
  让它把 JSON 解析成 `AlgoOrderRequest` 后复用 `SubmitAlgoOrder`，而不是自己 `new`。
- **删除** `:832` 起的硬编码测试单，以及 `PairTradingStrategy::on_timer` 里
  `algoContext.OnCommand("")` 的调用（`PairTradingStrategy.cpp:96-102`）。
- `PairTradingStrategy::on_command`（`:82`）目前是死代码（`BaseStrategy` 无对应虚函数），可一并清理。

---

## 4. 改动清单

| 文件 | 改动 | 量级 |
|---|---|---|
| `quant_library/basic/AlgoOrderRequest.h` | 新增 | ~90 行 |
| `quant_library/basic/BaseAlgoOrder.h` | 加 `ApplyOrderParams` 声明 + include `PairInfo.h` | 2 行 |
| `quant_library/basic/BaseAlgoOrder.cpp` | 加 `ApplyOrderParams` 实现 | ~45 行 |
| `quant_library/algo/AlgoContext.h` | 加 `SubmitAlgoOrder` 声明 + include | 2 行 |
| `quant_library/algo/AlgoContext.cpp` | 新增 `SubmitAlgoOrder`（~120 行）；删 NEW 分支与测试单 | 净减 ~680 行 |
| `quant_library/algo/PairTradingContext.h` | 回调类型改 `AlgoOrderRequest`；`PairTradingConfig` 补配置项 | ~20 行 |
| `quant_library/algo/PairTradingContext.cpp` | 删 `BuildAlgoOrderJson`（-99 行）；重写 `SubmitAlgoOrder`（+60 行） | 净减 ~40 行 |
| `src/strategy/PairTradingStrategy.cpp` | 回调接线；删 `SubmitAlgoCommand` | ~5 行 |

### 4.1 `PairTradingConfig` 建议补的字段

（对应原 JSON 里那些"策略级常量"，见 §5.1）

```cpp
std::string algoStrategyName{"pair_trading"};

OrderType activeOrderTypeTT{OT_MARKET};   // TT 主动腿
OrderType activeOrderTypeMT{OT_LIMIT};    // MT 主动腿
OrderType passiveOrderType{OT_MARKET};
double passiveVolumePct{0.5};
double maxMTOrderSize{1.0};
double maxTTOrderSize{1.0};

int64_t activeMakerCancelOrderTime{300LL * 1000 * 1000};
int64_t activeTakerCancelOrderTime{5LL * 1000 * 1000};
int64_t passiveMakerCancelOrderTime{60LL * 1000 * 1000};
int64_t passiveTakerCancelOrderTime{5LL * 1000 * 1000};
double activePassiveCancelOrderPct{0.0};
double activeMakerCancelOrderPct{0.0};
double activeTakerCancelOrderPct{0.0};
double passiveMakerCancelOrderPct{0.0};
double passiveTakerCancelOrderPct{0.0};

double activeMakerFeeRate{0.0};
double activeTakerFeeRate{0.0};
double passiveMakerFeeRate{0.0};
double passiveTakerFeeRate{0.0};
double activeMakerSlippage{0.0};
double activeTakerSlippage{0.0};
double passiveMakerSlippage{0.0};
double passiveTakerSlippage{0.0};

bool mtRebalanceSwitch{true};
bool ttRebalanceSwitch{true};
```

---

## 5. 需要拍板的两个点

### 5.1 那批"策略级常量"从哪来？

现在它们由 SCC 通过 JSON 下发：手续费率、滑点、`xxxCancelOrderTime` / `xxxCancelOrderPct`、
`driveType`、`depthCheck`、`maxMT|TTOOrderSize`、`rebalanceSwitch/Flag`、`priceTick`。
改成直接建对象后没有 JSON 了，必须落到配置：

- **(a)** 加进 `PairTradingConfig`，从 `etc/config.json` 的 `op` 段读（和 `maxAmount` / `pairKeys` 一样）——**建议**；
- **(b)** 只保留少数必须可调的，其余用 `AlgoOrderRequest` 的默认值。

至少费率、滑点、`maxMT|TTOOrderSize`、撤单时间这几项建议走 (a)。

### 5.2 `forgoProfit` 的价差修正放在哪？

本方案放在"填 request 时"（§3.4）。也可以挪到 `AlgoContext::SubmitAlgoOrder` 里统一做。
前者更直观（策略层知道为什么要修），后者更集中。

---

## 6. 收益与风险

### 收益

- 编译期字段校验：漏字段 / 写错名字会编译失败，而不是运行期静默丢弃。
- 删掉约 600 行 `if (body.HasMember(...))`、99 行 JSON 拼装，以及一次序列化 + 反序列化。
- `OnCommand` 职责单一（只处理外部控制），新建单走 `SubmitAlgoOrder`，两条路径不再重复。
- 顺手解决：`BuildAlgoOrderJson` 空壳、`OnCommand` 整段被注释、测试单残留、
  `algoType` 漏赋值（可在 `SubmitAlgoOrder` 里统一设，也可在构造函数里默认）。

### 风险

1. **`activeOrderType` 两处口径不一致**：
   `BuildAlgoOrderJson:267` 是 `isTT ? OT_MARKET : OT_LIMIT`，`passiveOrderType = OT_MARKET`；
   而硬编码测试单 `:847 / :856` 是 `OT_LIMIT` / `OT_LIMIT`。迁移前要确认以哪个为准。
2. **`AlgoOrderRequest` 与 `BaseAlgoOrder` 字段有重复**，将来加字段要同步两处。
   若不想维护两份，可改用方案 A（直接传对象）。
3. **字段语义迁移必须逐条核对**，特别是 `activeDriveType` / `depthCheck` /
   `activeVolumeCalcualteType` 这几个在测试单里根本没出现过的字段。

---

## 已实施记录（2026-09-23）

按"**不新增头文件、直接在 `BuildAlgoOrderJson` 里创建对象**"的方式落地（即原方案 A 的简化版）。

### 改动清单

| 文件 | 改动 |
|---|---|
| `quant_library/algo/PairTradingContext.h` | include `../basic/BaseAlgoOrder.h`；`AlgoCommandCallback` 载荷 `const std::string&` → `BaseAlgoOrder*`；`BuildAlgoOrderJson` 返回类型 `std::string` → `BaseAlgoOrder*`；`GenerateAlgoOrderId` 返回类型 `std::string` → `int64_t` |
| `quant_library/algo/PairTradingContext.cpp` | include `basic/AlgoPairOrder.h`；`GenerateAlgoOrderId` 去掉 `"PT_"` 前缀改返回 int64；`SubmitAlgoOrder` 改为接对象并回写 `PairInfoManager`；`BuildAlgoOrderJson` 整体重写为直接 `new AlgoPairOrder()` 并填字段 |
| `quant_library/algo/AlgoContext.h` | 新增 public `void SubmitAlgoOrder(BaseAlgoOrder* pAlgoOrder)` |
| `quant_library/algo/AlgoContext.cpp` | 新增 `SubmitAlgoOrder`（注册：`Init` / 插入 `alogOrderManager` / `GeneratePubStr` / `WriteAlgoOrder` / 订阅价差）；`OnCommand` 里硬编码的 DOGE 测试单整段注释保留 |
| `include/strategy/PairTradingStrategy.h` | `SubmitAlgoCommand` 参数改为 `BaseAlgoOrder*` |
| `src/strategy/PairTradingStrategy.cpp` | 回调 lambda 参数改 `BaseAlgoOrder*`；`SubmitAlgoCommand` 转发到 `algoContext.SubmitAlgoOrder` |

### `BuildAlgoOrderJson` 的实际结构（函数名沿用旧名，已不拼 JSON）

1. 用 `const bool* pSw` 定位本次触发的是 TT/MT × OL/OS/CL/CS 哪一组；非法组合 → `nullptr`
2. 开关校验：`!sw && !isClose && forgoProfit == 0.0` → `nullptr`（与原逻辑一致）
3. `targetVolume`（TT 取 `ttTargetVolume`，MT 取 `mtTargetVolume`）非法 → `nullptr`
4. `new AlgoPairOrder()`，逐个填字段
5. 32 个触发参数整体从 `pi.orderParams` 拷贝
6. 用指针定位本次那一组，套用 `forgoProfit` 修正，并把开关置 `true`

### 关键决策

- **费率 / 滑点**取自 `SignalGenerator::Instance().GetConfig()`（`FeeSlippageConfig`），不取 0。
  原因：`takerTakerFs` / `makerTakerFs` 会直接参与 `AlgoPairOrder::GetTargetPairOrder` 的目标价差计算
  （`AlgoPairOrder.cpp:175 / :217`），留 0 会让算法单侧的价差门槛与信号侧不一致。
  信号侧 `totalCost` 只计一次滑点，因此 `basicSlippage` 只放在主动腿，被动腿滑点为 0。
- **`activeOrderType = isTT ? OT_MARKET : OT_LIMIT`、`passiveOrderType = OT_MARKET`**，沿用原 `BuildAlgoOrderJson`
  的语义（TT 两腿都吃单；MT 主动腿挂单、被动腿吃单），未采用测试单的 `LIMIT/LIMIT`。
- **`maxMTOrderSize` / `maxTTOrderSize` = 1.0**、`mtRebalanceFlag` / `ttRebalanceFlag` = true，
  取 `BaseAlgoOrder` 构造函数默认值（与测试单的 5 / true 不同）。
- **开关强制打开**：`*pSwitch = true`。正常开仓信号本来就要求 `sw == true`，此处实际只影响风控强平路径
  （手动模式下 `RecalcOrderParams` 会把 CL/CS 开关置 false，原 JSON 路径下强平会因
  `AlgoPairOrder::GetTargetPairOrder` 的 `if (!swch) return pairOrder;` 而静默不下单）。

### 顺带修掉的 ID 断裂

原设计里两侧的算法单 ID 根本对不上：

- `PairTradingContext::GenerateAlgoOrderId()` 生成 `"PT_<n>"` 字符串，经 `SetActiveAlgoOrder` 存进
  `PairInfo::currentAlgoOrderId`；
- `AlgoContext` 注册时又自己调 `GenerateStrategyAlgoPairId()` 生成另一个 id 作为 `alogOrderManager` 的 key；
- `PairTradingStrategy::ScanFinishedAlgoOrders`（`PairTradingStrategy.cpp:142`）执行
  `std::stoll(pi->currentAlgoOrderId)` —— 对 `"PT_123"` 会直接抛 `std::invalid_argument`。

现在统一为：**PT 侧生成 int64 id → 写进 `pAlgoOrder->algoOrderId` → 以十进制字符串写入 `currentAlgoOrderId`**；
`AlgoContext::SubmitAlgoOrder` 只在 `algoOrderId == 0` 时才自己生成。`stoll` → `GetAlgoOrder` 链路因此能打通。

### 仍未处理（按"其他先不要管"保留）

- `PairTradingConfig` 尚未补费率 / 滑点 / 撤单时间 / `maxMT|TTOOrderSize` 等字段（目前是硬编码默认值）。
- `AlgoContext::OnCommand` 的 CANCEL / MODIFY / QUERY 分支仍在 `/* */` 里，尚未恢复。
- `PairTradingStrategy::on_timer` 里 `algoContext.OnCommand("")` 现在已是空操作（测试单已注释），
  相关 `createAlgo` / `lastOnCommand` 逻辑可清理。
- `algoStrategyName` 目前硬编码 `"pair_trading"`。

---

## 补充修复：算法单生命周期收尾（2026-09-23）

新链路真正开始建单后，暴露出一个**通知链路断裂**，已一并修掉。

### 问题

`AlgoContext::OnTimer` 在同一个循环迭代里**先标记终结状态、再立刻 `delete`**：

```
:1991  it->second->algoOrderStatus = stra::ALGO_OS_FILLED;   // 标记
:1991  deleteAlgoOrderFlag = true;
...
:2083  if (deleteAlgoOrderFlag) { delete it->second; allAlgoOrders.erase(it++); }   // 同一个 tick 就删掉
```

而唯一把结果告诉 `PairTradingContext` 的通道是策略层的轮询
`PairTradingStrategy::ScanFinishedAlgoOrders`（每 200ms 一次，`PairTradingStrategy.cpp:134`），
它靠 `algoContext.GetAlgoOrder(id)` 判断算法单是否终结。算法单在同一 tick 就被删掉，
策略层**永远只能命中 `if (!order)` 分支**，而该分支里

```cpp
double volFilled = order ? order->pairTotalVolume - pi->pairTotalVolume : 0.0;   // order 恒为 null → 恒为 0
```

三元表达式的条件恒假，`volFilled` 永远是 0。于是 `PairTradingContext::OnAlgoOrderUpdate` 里
`if (isFinished && std::abs(volumeFilled) > 1e-9)` 不成立 → `UpdateOnAlgoOrderFinished`（持仓均价）、
`RiskManager::OnAlgoFinished`（异常平仓档位复位）、`RecalcOrderParams` **全部被跳过**。
只有 `ClearActiveAlgoOrder` 生效，所以对子会被释放，但风控档位和持仓均价永远不复位/不更新。

### 修法

`AlgoContext::OnTimer` 里给终结的算法单留一个 **5 秒宽限期**（`AlgoContext.cpp`）：

1. 循环开头先判断是否已是终结状态（`FILLED` / `CANCELED` / `ERRORCANCELED`）：
   是则只做「宽限期到了就删 + 退订」，其余逻辑全部 `continue` 跳过（避免重复推送/重复落库）；
2. 底部 `if (deleteAlgoOrderFlag)`：刚被标记终结的单不再当场删除，交给开头的宽限期分支；
   非终结的删除请求（查询异常那条路径）保持原来的立即删除；
3. `deleteAlgoOrderFlag` 从函数级挪到循环体内（原来是跨迭代复用的，加了 `continue` 后存在串味风险）。

配套在 `AlgoContext::OnSpread` 里加了一道早退：终结状态的算法单不再走
`CancelOrderOnSpread`（否则宽限期内会去撤它已经成交/已撤掉的子单）。

### 顺带修掉的死代码

`PairTradingStrategy::ScanFinishedAlgoOrders` 的 `if (!order)` 分支里
`order ? order->pairTotalVolume - pi->pairTotalVolume : 0.0` 条件恒假（`order` 必为 null），
属于明显笔误。宽限期方案让这条分支基本不再被走到，但该表达式本身仍建议清理。
