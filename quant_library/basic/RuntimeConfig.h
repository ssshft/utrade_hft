/***
 * 运行时配置（可热改的那几个字段）
 *
 * 用法：
 *
 *     #include "basic/RuntimeConfig.h"
 *
 *     pt::RuntimeConfig rc;
 *     if (pt::ReadRuntimeConfigFile("config.json", rc) && rc.hasCloseOnly) {
 *         use(rc.closeOnly);
 *     }
 *
 * 只在"文件指纹变了"之后才调用它（见 PairTradingContext::PollRuntimeConfig），
 * 它自己不做任何缓存，每次调用都会真读一遍文件。
 *
 * ⚠️ 这是**唯一**允许在运行期重读配置的地方。PairTradingConfig 那套是启动时读一次的
 *    静态配置：对子清单、账户、快照路径改了必须重启（要以另一套状态重来）。
 *    往这里加字段的门槛是"改了立刻生效、且不需要重建对子"。
 *
 * 加一个可热改参数（3 处）：
 *   ① 本文件 RuntimeConfig 加字段 + hasXxx
 *   ② ReadRuntimeConfigFile 里解析它（照抄其他配置字段的写法）
 *   ③ PairTradingContext::OnTimer 第 0.6 步里落地它
 *   队列本身不用动 —— 元素是整份 RuntimeConfig，不是"某个参数的消息"。
 * ****/

#pragma once

#include <string>

namespace pt {

// 运行时配置（一次解析的结果）。必须是 trivially copyable：它要进 SPSC 队列。
struct RuntimeConfig {
    // 只平不开（运维急停）：true 时压掉全部开仓信号，平仓照常。
    bool closeOnly{false};
};

bool ReadRuntimeConfigFile(const std::string& path, RuntimeConfig& out);

} // namespace pt
