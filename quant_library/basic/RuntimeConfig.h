#pragma once

// 运行时配置：允许在进程运行中改、不用重启的那几个开关。
//
// 用法（三行）：
//     pt::RuntimeConfig rc;
//     if (!pt::ReadRuntimeConfigFile(cfgPath, rc)) {
//         // 读失败（文件半截 / 不是 JSON / 没有 op 段）：**保持上一次的值**，下一轮重试。
//         // 千万不要把失败当成"全部关掉" —— 那会在编辑器保存的一瞬间放开所有限制。
//     }
//     // 成功：rc.closeOnly / rc.stopAllAlgo 就是文件里的值
//
// 只读 `op.pairTrading` 下的白名单字段，**没写的字段 = 默认 false**（删 key 等于 false，
// 不是"保持当前值"）。布尔必须写裸 `true` / `false`：写成 `"true"` 时 `GetBool()` 作用在
// 字符串上，Debug 直接 assert abort、Release 静默读成 false。
//
// 生产调用点只有两个：
//   ① `src/strategy/PairTradingStrategy.cpp` 的 `pre_start`：读一次初值；
//   ② `PairTradingContext::RunSignalOnce` 第 0 步（`T_signal` 线程，1 Hz 轮询文件指纹）。
// 交易线程**不读文件**，只从 SPSC 队列里取已经解析好的结构体。

#include <string>

namespace pt {

struct RuntimeConfig {
    bool closeOnly{false}; // 只平不开（运维急停）：true 时压掉全部开仓信号，平仓照常。
    bool stopAllAlgo{false}; // 停掉所有算法单：true 时撤掉在跑的算法单，且不再创建新的（连平仓/风控强平也不发）
};

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out);

} // namespace pt
