#pragma once

// 运行时配置：允许在进程运行中改、不用重启的那几个开关。
//
// 用法（三行）：
//     pt::RuntimeConfig rc;
//     if (!pt::ReadRuntimeConfigFile(cfgPath, rc)) {
//         // 读失败（文件半截 / 不是 JSON / 没有 op 段）：**保持上一次的值**，下一轮重试。
//         // 千万不要把失败当成"全部关掉" —— 那会在编辑器保存的一瞬间放开所有限制。
//     }
//     // 成功：rc.closeOnly / rc.stopAllAlgo / rc.pairCommands 就是文件里的值
//
// 只读 `op.pairTrading` 下的白名单字段，**没写的字段 = 默认值**（删 key 等于回到默认，
// 不是"保持当前值"）。布尔必须写裸 `true` / `false`：写成 `"true"` 时 `GetBool()` 作用在
// 字符串上，Debug 直接 assert abort、Release 静默读成 false。
//
// 生产调用点：
//   ① `src/strategy/PairTradingStrategy.cpp:69-74` 的 `pre_start` **不用本函数** —— 它自己解析
//      config.json 拿 `closeOnly` / `stopAllAlgo` 的初值（同一份文件、同一组 key）；
//   ② `PairTradingContext::RunSignalOnce` 第 0 步（`T_signal` 线程，1 Hz 轮询文件指纹）——
//      **本函数唯一的调用点**。`m_cfgStamp` 起始是 invalid，所以 Init 之后第一轮一定会读一次，
//      文件里已经存在的 `pairCommands` 不会因为"没改过文件"而漏掉（只是要等对账放行才生效）。
// 交易线程**不读文件**，只从 SPSC 队列里取已经解析好的结构体。
//
// ---------------------------------------------------------------------------
// 指定币对命令（`op.pairTrading.pairCommands`）
// ---------------------------------------------------------------------------
// 只对某个对子做急停 / 只平不开 / 恢复，其它对子照常跑：
//
//     "pairTrading": {
//         "closeOnly": false,
//         "stopAllAlgo": false,
//         "pairCommands": [
//             {
//                 "pairKey": "BINANCE.USDT_SWAP.DOGE-USDT|GATEIO.USDT_SWAP.DOGE-USDT",
//                 "cmdType": 2,
//                 "profitPct": -0.002,
//                 "closeRemainVolume": 0
//             }
//         ]
//     }
//
//   · `pairKey`（**必填**）与 `PairInfo::pairInstrumentKey` 逐字符一致（区分大小写）；
//   · `cmdType`（**必填**）1=停止交易 2=只平不开 3=恢复 4=改参数（未实现，落地端报错跳过）；
//   · `profitPct`，**写负数才是让利平仓**：平仓阈值 = 分位数边界 + profitPct，
//     越小越容易触发（-0.002 = 愿意亏 20bp 出场）。**不写就是 0**；
//   · `closeRemainVolume`，0 = 平干净，r > 0 = 只平到还剩 r（单位同持仓量，非金额）。**不写就是 0**。
//   ⚠️ 两个数值字段的缺省是 0，不是"不改"。命令要按**完整一条**写（两个字段都带上），
//      否则没写的那个会被写成 0 —— 对 `closeRemainVolume` 恰好等于"平干净"，
//      对 `profitPct` 等于"比默认 0.0001 略严"，都不危险，但要知道它会变。
//
// 三种 cmdType 各自改什么（`PairTradingContext::ApplyPairCommands`）：
//   1 STOP     `stopFlag = true`  -> **开仓和平仓都不发**（`CanOpen` / `CanClose` 都判 stopFlag）。
//                等于把该对子冻住；在跑的单靠 `CheckAlgoOrderTimeout` 撤，不是立刻。
//   2 CLOSE    `closeFlag = true` -> 只平不开：`CanOpen` 立刻挡住新开仓，平仓照常。
//                同时把 `profitPct` / `closeRemainVolume` 写成命令里的值。
//   3 RESUME   清 `stopFlag` / `closeFlag`，并把两个数值字段写成命令里的值
//                （不写 = 0：`closeRemainVolume = 0` 就是平干净，`profitPct = 0` 比默认略严）。
//                **不碰 `autoFlag`** —— 那只是"参数由谁算"的标签，不是急停状态。
//   ⚠️ 对 `stopFlag == true` 的对子，**2 CLOSE 会被拒绝**（那时连平仓单都发不出去，
//      写了也没用），但 **3 RESUME 必须能进** —— 它是 STOP 的唯一出路。
//
// **单向：删掉条目 = 什么都不做**（不自动恢复）。命令只是"把字段写成这几个值"，
// 写完就粘住，只有下一次写同一个字段才会变。要恢复发 `cmdType: 3`，或者人工改快照再重启：
//     · `stopFlag` 在第 **28** 列 -> 改 `false`（手工恢复时**先看这一列**，它挡死一切）
//     · `closeFlag` 在快照第 **29** 列 -> 改 `false`
//     · `profitPct` 在快照第 **30** 列 -> 改 `0.0001`
//     · `closeRemainVolume` 不进快照 -> 重启后自动回 0（= 平干净），不用管
// 为什么不做自动恢复：那要记住"上一版命令了哪些对子"，还要区分"快照带来的 closeFlag"与
// "命令写的 closeFlag"（否则启动 1 秒就会把人工停机解开）—— 一整套账本 + 基线 + 差集，
// 只为了一件很少发生的事。运维重启一次更省事。
//
// 语义边界（两条别指望它）：
//   ① 它**不主动发平仓单**，只把该对子切成"只平不开" + 放松平仓门槛。
//      真正的平仓仍由正常信号链路在价差满足时触发；
//   ② 它**不写 `autoFlag`**：`autoFlag = false` 会让该对子的单免掉超时撤单
//      （`CheckAlgoOrderTimeout` 直接 return），写错了就是单子永远挂着。
//
// 生效时机：`OnTimer` 0.6 里落地，同一步就 `RecalcOrderParams` + `algoModifyTime = 0`，
// 所以是**下一个 1 Hz tick**。真掉到周期路径（`volumeRecalcIntervalSec` / 统计刷新各 60s，
// `ProcessModify` 节流 60s）最坏约 2 分钟 —— 这只是兜底，不是常态。

#include <cstdint>
#include <string>
#include <type_traits>

#include "DataStruct.h"   // stra::INST_KEY_LEN / PairCommandType

namespace pt {

// 一次能下发多少条对子命令。超出部分会被 ReadRuntimeConfigFile **整份拒绝**并打日志
constexpr int kMaxPairCommands = 32;

// 一条"指定币对"的命令。**必须是可平凡拷贝的 POD**：它跟着 RuntimeConfig 整份按值塞进
// `boost::lockfree::spsc_queue<RuntimeConfig, capacity<16>>`（PairTradingContext.h），
// 里面不能出现 std::string / std::vector。
//
// 两个数值字段的缺省是 **0**，不是"不改"：命令要按完整一条写（见文件头的用法说明）。
struct PairCommand {
    PairCommandType cmdType{PairCmd_NONE};
    char pairKey[stra::INST_KEY_LEN]{""};
    double profitPct{0.0};
    double closeRemainVolume{0.0};
};

struct RuntimeConfig {
    bool closeOnly{false}; // 只平不开（运维急停）：true 时压掉全部开仓信号，平仓照常。
    bool stopAllAlgo{false}; // 停掉所有算法单：true 时撤掉在跑的算法单，且不再创建新的（连平仓/风控强平也不发）

    uint8_t pairCmdCount{0};
    PairCommand pairCommands[kMaxPairCommands];
};

static_assert(std::is_trivially_copyable<RuntimeConfig>::value, "RuntimeConfig 要整份塞进 lockfree SPSC 队列，必须可平凡拷贝");

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out);

} // namespace pt
