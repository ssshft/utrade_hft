// Stub for the external include/log_engine.h.
//
// 真实的是 fmtlog（异步日志库），本机没有。这里把四个宏换成"吞掉参数"的空实现：
// 调用点照样编译（包括 fmt 风格的 `{}` 占位符与参数个数），但一个字节都不写。
//
// ⚠️ 这个文件必须定义 LOG_* —— 真实的 quant_library/basic/DataStruct.h 是靠
//    `#include "log_engine.h"` 拿到这四个宏的。之前 log_engine.h 是空的，
//    之所以没暴露问题，是因为策略侧的桩 DataStruct.h 自己重复定义了一份。
//    用真实 DataStruct.h 编译执行侧时立刻就炸了（unknown identifier 'LOG_INFO'）。
//    现在统一放在这里，桩 DataStruct.h 不再重复定义。
#pragma once

#include <string>

// Swallow fmt-style arguments; proves the call sites compile without a formatter.
template <typename... A> inline void log_sink(const char*, A&&...) {}

#define LOG_DEBUG(...) log_sink(__VA_ARGS__)
#define LOG_INFO(...)  log_sink(__VA_ARGS__)
#define LOG_WARN(...)  log_sink(__VA_ARGS__)
#define LOG_ERROR(...) log_sink(__VA_ARGS__)

// 真实的是按天切日志文件 + 设级别。测试里不需要。
inline void log_maintain(const std::string&, const std::string&, const std::string&) {}
