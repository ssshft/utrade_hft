#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 行情 -> 算法单 半程测试套件的构建/运行脚本
#
# 自足：不读配置文件、不起 dbprocess / tb、不碰共享内存、不联网。
# 只编译 5 个真实 .cpp + 桩，产物只链 libc++/libstdc++ 与 libc。
#
# 编译树 = 「桩头文件 + 真实源码软链」：
#
#   - 真实源码（软链，零改写）：
#       quant_library/algo/PairTradingContext.cpp/.h
#       quant_library/basic/PairInfoManager.cpp/.h
#       quant_library/basic/PairInfo.h
#       quant_library/signal/SignalGenerator.cpp/.h
#       quant_library/signal/SpreadStatsBuilder.cpp/.h
#       quant_library/risk/RiskManager.cpp/.h
#   - 桩（stubs/，只补执行端和外部依赖）：
#       basic/DataStruct.h         stra:: 常量/枚举/AlgoOrderModify + LOG_*
#       basic/BaseAlgoOrder.h      字段集与真实头一致，方法声明去掉
#       basic/AlgoPairOrder.h      只留构造函数
#       ext/**                     dbp / pubsub / securitymanager / fmt / json
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
QL="$REPO/quant_library"
TREE="${TMPDIR:-/tmp}/ptsuite_build"

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
ln -sf "$QL/signal/SignalGenerator.h"   "$TREE/signal/SignalGenerator.h"
ln -sf "$QL/signal/SignalGenerator.cpp" "$TREE/signal/SignalGenerator.cpp"
ln -sf "$QL/signal/SpreadStatsBuilder.h"   "$TREE/signal/SpreadStatsBuilder.h"
ln -sf "$QL/signal/SpreadStatsBuilder.cpp" "$TREE/signal/SpreadStatsBuilder.cpp"
ln -sf "$QL/risk/RiskManager.h"         "$TREE/risk/RiskManager.h"
ln -sf "$QL/risk/RiskManager.cpp"       "$TREE/risk/RiskManager.cpp"
ln -sf "$QL/algo/PairTradingContext.h"   "$TREE/algo/PairTradingContext.h"
ln -sf "$QL/algo/PairTradingContext.cpp" "$TREE/algo/PairTradingContext.cpp"

cp "$HERE/suite.cpp" "$TREE/suite.cpp"
mkdir -p "$TREE/out"

echo "==> 编译"
cd "$TREE"
"$CXX" -std=c++17 -pthread -I . -I ext ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
    -o "$TREE/suite" \
    suite.cpp \
    algo/PairTradingContext.cpp \
    basic/PairInfoManager.cpp \
    signal/SignalGenerator.cpp \
    signal/SpreadStatsBuilder.cpp \
    risk/RiskManager.cpp \
    stubs.cpp

echo "==> 运行"
"$TREE/suite"
