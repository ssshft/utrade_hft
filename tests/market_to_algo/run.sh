#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 行情 -> 算法单 半程测试套件的构建/运行脚本
#
# 自足：不读配置文件、不起 dbprocess / tb、不碰共享内存、不联网。
# 只编译 6 个真实 .cpp + 桩，产物只链 libc++/libstdc++ 与 libc。
#
# 编译树 = 「桩头文件 + 真实源码软链」：
#
#   - 真实源码（软链，零改写）：
#       quant_library/algo/PairTradingContext.cpp/.h
#       quant_library/basic/PairInfoManager.cpp/.h
#       quant_library/basic/PairInfo.h
#       quant_library/basic/RuntimeConfig.cpp/.h
#       quant_library/signal/SignalGenerator.cpp/.h
#       quant_library/signal/SpreadStatsBuilder.cpp/.h
#       quant_library/risk/RiskManager.cpp/.h
#   - 真实公共头（软链）：仓库顶层 include/SpinBackoff.h（当前已无人 include，保留备用）
#   - 桩（stubs/，只补执行端和外部依赖）：
#       basic/DataStruct.h         stra:: 常量/枚举/AlgoOrderModify + LOG_*
#       basic/BaseAlgoOrder.h      字段集与真实头一致，方法声明去掉
#       basic/AlgoPairOrder.h      只留构造函数
#       basic/LarkRebot.h          飞书告警：接口照抄真实头，实现留空
#       ext/**                     dbp / pubsub / securitymanager / fmt / json / log_engine
#   - boost：spsc_queue.hpp 是 header-only。系统里有真 boost 就用真的（服务器），
#            没有就用 stubs/noboost 下的等价桩（macOS）—— 见下面的探测逻辑。
#
# 执行端（AlgoContext / BaseAlgoOrder 的报单与撤单）不参与 —— 那部分已经在
# OnCommand 里单独测过，不属于本套件的范围。
#
# 用法：
#   ./run.sh              # 编译并运行
#   ./run.sh -v           # 额外打开 -Wall -Wextra
#   CXX=g++-13 ./run.sh   # 指定编译器（默认自动挑 g++）
#
# 编译器选择顺序：$CXX -> /usr/bin/g++ -> PATH 上的 g++ -> c++ -> clang++
# 目标环境是 Ubuntu 服务器（g++ + 系统已装的三方库）；macOS 上 clang++ 也能跑。
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"   # 仓库根：顶层 include/ 在这里（utrade_hft 的上一级）
QL="$REPO/quant_library"
TREE="${TMPDIR:-/tmp}/ptsuite_build"
# 本机（macOS）没有系统装的 rapidjson，而 basic/RuntimeConfig.cpp 要用它。
# tests/third_party/rapidjson 是 rapidjson 1.1.0 的原版头文件（header-only，未改一行），
# 只为让这套件在没装 rapidjson 的机器上也能编译。服务器上系统包优先 —— 见下面的探测。
TP="$HERE/../third_party"

# ---- 挑编译器 -------------------------------------------------------------
# 优先 g++：目标环境是 Ubuntu，/usr/bin/g++ 是系统自带的那个。
# 注意 macOS 上 /usr/bin/g++ 是 clang++ 的软链，也能用，所以这个顺序两边都安全。
pick_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        echo "$CXX"
        return
    fi
    local cand
    for cand in /usr/bin/g++ /usr/bin/c++ /usr/bin/clang++; do
        if [[ -x "$cand" ]]; then
            echo "$cand"
            return
        fi
    done
    for cand in g++ c++ clang++; do
        if command -v "$cand" >/dev/null 2>&1; then
            echo "$cand"
            return
        fi
    done
}

CXX="$(pick_cxx)"
if [[ -z "$CXX" ]]; then
    echo "错误：找不到 C++ 编译器。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
    exit 1
fi

# 校验可用：带路径的看 -x，纯命令名看 command -v。
# 不校验的话，下一步的 "$CXX" --version 会直接抛一句难看的 shell 错误。
if [[ "$CXX" == */* ]]; then
    if [[ ! -x "$CXX" ]]; then
        echo "错误：$CXX 不存在或不可执行。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
        exit 1
    fi
elif ! command -v "$CXX" >/dev/null 2>&1; then
    echo "错误：PATH 上找不到 $CXX。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
    exit 1
fi

EXTRA_FLAGS=()
if [[ "${1:-}" == "-v" ]]; then
    EXTRA_FLAGS=(-Wall -Wextra -Wno-unused-parameter)
fi

# ---- boost 探测 ------------------------------------------------------------
# PairTradingContext.h 用 boost::lockfree::spsc_queue 做「分位数计算分线程」的 SPSC 通道
# （见 docs/thread_split_design.md §8/§9）。它是 header-only，不需要链库，但头必须找得到：
#   · Ubuntu 服务器：系统自带 boost -> 用真的；
#   · macOS 开发机：没有 boost -> 用 stubs/noboost 下的等价桩。
# 桩目录**只在探测失败时**才进 -I，否则 -I 会先于系统路径命中，把服务器上真的 boost 挡掉。
BOOST_INC=()
if ! echo '#include <boost/lockfree/spsc_queue.hpp>' \
        | "$CXX" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    echo "==> 系统里没有 boost/lockfree/spsc_queue.hpp -> 用 stubs/noboost 的等价桩"
    BOOST_INC=(-I "$HERE/stubs/noboost")
fi

# ---- rapidjson 探测 --------------------------------------------------------
# 同样的道理：服务器上系统装了 rapidjson（src/strategy 在用），优先用系统的；
# 没有才用 tests/third_party 里的原版头文件。顺序反了的话，第三方副本会盖掉系统包。
RJ_INC=()
if ! echo '#include <rapidjson/document.h>' \
        | "$CXX" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    if [[ ! -f "$TP/rapidjson/document.h" ]]; then
        echo "错误：系统没有 rapidjson，$TP/rapidjson 也不存在。" >&2
        echo "      RuntimeConfig.cpp 需要它。装一个：apt install rapidjson-dev" >&2
        exit 1
    fi
    echo "==> 系统里没有 rapidjson -> 用 $TP 下的原版头文件"
    # -isystem 而不是 -I：1.1.0 里用了 C++17 已废弃的 std::iterator，
    # 用 -I 会在 -Wall 下刷出一屏 -Wdeprecated-declarations（第三方代码，不该我们改）。
    RJ_INC=(-isystem "$TP")
fi

echo "==> 编译器: $CXX"
"$CXX" --version 2>&1 | head -1

echo "==> 重建临时编译树: $TREE"
rm -rf "$TREE"
mkdir -p "$TREE/algo" "$TREE/basic" "$TREE/signal" "$TREE/risk"

# 1) 桩头文件（先铺，真实头文件再覆盖需要的几个）
cp -R "$HERE/stubs/basic/." "$TREE/basic/"
cp -R "$HERE/stubs/ext/."   "$TREE/ext/"
cp "$HERE/stubs/stubs.cpp"  "$TREE/stubs.cpp"

# 2) 真实源码（软链，零改写）
ln -sf "$QL/basic/PairInfo.h"           "$TREE/basic/PairInfo.h"
ln -sf "$QL/basic/PairInfoManager.h"    "$TREE/basic/PairInfoManager.h"
ln -sf "$QL/basic/PairInfoManager.cpp"  "$TREE/basic/PairInfoManager.cpp"
ln -sf "$QL/basic/RuntimeConfig.h"      "$TREE/basic/RuntimeConfig.h"
ln -sf "$QL/basic/RuntimeConfig.cpp"    "$TREE/basic/RuntimeConfig.cpp"
ln -sf "$QL/signal/SignalGenerator.h"   "$TREE/signal/SignalGenerator.h"
ln -sf "$QL/signal/SignalGenerator.cpp" "$TREE/signal/SignalGenerator.cpp"
ln -sf "$QL/signal/SpreadStatsBuilder.h"   "$TREE/signal/SpreadStatsBuilder.h"
ln -sf "$QL/signal/SpreadStatsBuilder.cpp" "$TREE/signal/SpreadStatsBuilder.cpp"
ln -sf "$QL/risk/RiskManager.h"         "$TREE/risk/RiskManager.h"
ln -sf "$QL/risk/RiskManager.cpp"       "$TREE/risk/RiskManager.cpp"
ln -sf "$QL/algo/PairTradingContext.h"   "$TREE/algo/PairTradingContext.h"
ln -sf "$QL/algo/PairTradingContext.cpp" "$TREE/algo/PairTradingContext.cpp"

# 真实公共头（软链）：仓库顶层 include/ 下的，不属于 quant_library。
# 本套件当前**不再** include SpinBackoff.h —— signal_work 的低频等待已从
# spin::backoff_sleep 改成 std::this_thread::sleep_for（见 PairTradingContext.cpp）。
# 软链留着：include/ 是 utrade_hft 与兄弟模块共享的层，将来若恢复 spin:: 退避，
# 这里不用再改。
ln -sf "$ROOT/include/SpinBackoff.h"     "$TREE/ext/SpinBackoff.h"

cp "$HERE/suite.cpp" "$TREE/suite.cpp"
mkdir -p "$TREE/out"

echo "==> 编译"
cd "$TREE"
"$CXX" -std=c++17 -pthread -I . -I ext ${BOOST_INC[@]+"${BOOST_INC[@]}"} ${RJ_INC[@]+"${RJ_INC[@]}"} ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
    -o "$TREE/suite" \
    suite.cpp \
    algo/PairTradingContext.cpp \
    basic/PairInfoManager.cpp \
    basic/RuntimeConfig.cpp \
    signal/SignalGenerator.cpp \
    signal/SpreadStatsBuilder.cpp \
    risk/RiskManager.cpp \
    stubs.cpp

echo "==> 运行"
"$TREE/suite"
