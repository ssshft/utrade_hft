// =============================================================================
// boost::lockfree::spsc_queue 的**等价桩**
// （只在系统里没有真 boost 头时才进 include 路径 —— 见 run.sh 的探测逻辑）
//
// 为什么需要它：PairTradingContext.h 现在 include <boost/lockfree/spsc_queue.hpp>，
// 用它做「分位数计算分线程」的 SPSC 通道（见 docs/thread_split_design.md §8 / §9）。
// 目标环境是 Ubuntu 服务器（系统自带 boost）；这台 macOS 开发机上没有 boost，
// 而本套件要在本机能编译能跑，所以给一个只实现「实际用到的那几个成员」的等价桩。
//
// ⚠️ 这是桩，不是 boost。run.sh 会先探测系统里有没有真的 boost 头：
//      有   -> 用真的（服务器走这条路，本文件根本不会进 include 路径）
//      没有 -> 才把本目录加进 -I
//    所以它不会在服务器上把真的 boost 挡掉。
//
// 语义严格对齐 boost 文档（1.85）：
//   · bool push(T const&)  满则返回 false，且丢弃的是**传进来的这一条（最新）**，
//                          不是队首最旧那条 —— include/MdSlotRing.h 的注释也是这么写的。
//   · bool pop(T&)         空则返回 false。
//   · read_available()     只能由**消费者**线程调；write_available() 只能由**生产者**线程调。
//   · 单生产者单消费者、wait-free；T 必须可默认构造且可拷贝。
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>

namespace boost {
namespace lockfree {

// 编译期容量策略（真 boost 里是个 options 结构体，这里只需要 value）
template <std::size_t N>
struct capacity {
    static constexpr std::size_t value = N;
};

template <typename T, typename... Options>
class spsc_queue;

template <typename T, std::size_t N>
class spsc_queue<T, capacity<N>> {
public:
    using value_type = T;
    using size_type  = std::size_t;

    static_assert(N > 0, "spsc_queue capacity must be > 0");

    spsc_queue() = default;
    spsc_queue(const spsc_queue&) = delete;
    spsc_queue& operator=(const spsc_queue&) = delete;

    // 满 -> 返回 false，并丢弃**本条**（与 boost 一致）
    bool push(const T& t) {
        const size_type w = m_write.load(std::memory_order_relaxed);
        if (w - m_read.load(std::memory_order_acquire) >= N) {
            return false;
        }
        m_buf[w % N] = t;
        m_write.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& ret) {
        const size_type r = m_read.load(std::memory_order_relaxed);
        if (r == m_write.load(std::memory_order_acquire)) {
            return false;
        }
        ret = m_buf[r % N];
        m_read.store(r + 1, std::memory_order_release);
        return true;
    }

    size_type read_available() const {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed);
    }

    size_type write_available() const {
        return N - read_available();
    }

    bool empty() const {
        return read_available() == 0;
    }

private:
    T m_buf[N];
    std::atomic<size_type> m_write{0};
    std::atomic<size_type> m_read{0};
};

} // namespace lockfree
} // namespace boost
