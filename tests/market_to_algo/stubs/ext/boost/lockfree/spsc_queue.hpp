#ifndef LOCAL_STUB_BOOST_LOCKFREE_SPSC_QUEUE_HPP
#define LOCAL_STUB_BOOST_LOCKFREE_SPSC_QUEUE_HPP

// ============================================================================
// boost/lockfree/spsc_queue.hpp 的最小本机桩 —— **仅供 tests/ 下的自足套件使用**
//
// 为什么要有这个文件:
//   `algo/PairTradingContext.h:21` 直接 `#include <boost/lockfree/spsc_queue.hpp>`，
//   而本机（macOS）没有 boost ⇒ 整套 algo_exec 测试编译不过，只能去部署机验。
//   这让"改执行层"变成盲改。这里用纯 std 实现同样的语义，把这条验证链路接回本机。
//
// ⚠️ 三个边界，别越界使用:
//   1. **只在测试树里生效**：靠 `-I ext`（ext -> tests/algo_exec/stubs）被搜到，
//      生产构建走真 boost，不会看到这个文件。
//   2. **只实现 PairTradingContext 用到的那几个接口**：push / pop / read_available /
//      write_available / empty。其它 boost 接口（如 consume_all）这里没有。
//   3. **语义按 SPSC 写**：单生产单消费，head 只由生产者写、tail 只由消费者写。
//      多线程抢同一个队列不是它的用法，也别拿它当多生产者队列。
//
// 用法（和真 boost 一致）:
//   using Q = boost::lockfree::spsc_queue<Msg, boost::lockfree::capacity<256>>;
//   Q q;  q.push(m);  Msg out;  while (q.pop(out)) { ... }
// ============================================================================

#include <array>
#include <atomic>
#include <cstddef>

namespace boost {
namespace lockfree {

// boost 的 capacity<N> 是"编译期容量标签"，这里原样保留语义
template <std::size_t N>
struct capacity {
    static constexpr std::size_t value = N;
};

namespace detail {

// 从可变参数里挑出 capacity<N>；没写就退回 128（真 boost 要求必须显式给，
// 这里给默认值只是为了编译期不炸，测试里两处都显式写了 capacity）
template <typename... Options>
struct capacity_of {
    static constexpr std::size_t value = 128;
};

template <std::size_t N, typename... Rest>
struct capacity_of<capacity<N>, Rest...> {
    static constexpr std::size_t value = N;
};

}  // namespace detail

template <typename T, typename... Options>
class spsc_queue {
public:
    static constexpr std::size_t capacity_value = detail::capacity_of<Options...>::value;

    spsc_queue() = default;
    spsc_queue(const spsc_queue&) = delete;
    spsc_queue& operator=(const spsc_queue&) = delete;

    // 生产者调用。满了返回 false（真 boost 同语义：不阻塞、不覆盖）
    bool push(const T& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= capacity_value) {
            return false;
        }
        buf_[h % capacity_value] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // 消费者调用。空返回 false
    bool pop(T& out) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (t == h) {
            return false;
        }
        out = buf_[t % capacity_value];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    std::size_t read_available() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    std::size_t write_available() const {
        return capacity_value - read_available();
    }

    bool empty() const { return read_available() == 0; }

private:
    std::array<T, capacity_value> buf_{};
    std::atomic<std::size_t> head_{0};   // 只由生产者写
    std::atomic<std::size_t> tail_{0};   // 只由消费者写
};

}  // namespace lockfree
}  // namespace boost

#endif
