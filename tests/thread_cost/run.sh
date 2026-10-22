#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 「算法执行线程耗时操作」基准的构建/运行脚本
#
# 用法：
#   ./run.sh          # 编译并运行
#   ./run.sh -v       # 额外打开 -Wall -Wextra
#   CXX=g++-13 ./run.sh
#
# 与 tests/market_to_algo 的关系：
#   复用**同一套桩**（../market_to_algo/stubs），只链真实源码
#   quant_library/basic/PairInfoManager.cpp 一个文件。不读配置、不起进程、
#   不碰共享内存、不联网，产物只依赖 libc/libstdc++。
#
# 为什么只链 PairInfoManager.cpp：
#   本基准量的是 OnTimer 里的三处操作（SaveSnapshot / SaveSpreadStats /
#   GetAllPairInfos），它们全部落在 PairInfoManager 上，不需要 PairTradingContext。
#   少链一个文件，就少一份「因为桩不匹配而编不过」的维护面。
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QL="$REPO/quant_library"
STUBS="$HERE/../market_to_algo/stubs"
TREE="${TMPDIR:-/tmp}/thread_cost_build"

pick_cxx() {
    if [[ -n "${CXX:-}" ]]; then echo "$CXX"; return; fi
    local cand
    for cand in /usr/bin/g++ /usr/bin/c++ /usr/bin/clang++; do
        if [[ -x "$cand" ]]; then echo "$cand"; return; fi
    done
    for cand in g++ c++ clang++; do
        if command -v "$cand" >/dev/null 2>&1; then echo "$cand"; return; fi
    done
}

CXX="$(pick_cxx)"
if [[ -z "$CXX" ]]; then
    echo "错误：找不到 C++ 编译器。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
    exit 1
fi

EXTRA_FLAGS=()
if [[ "${1:-}" == "-v" ]]; then
    EXTRA_FLAGS=(-Wall -Wextra -Wno-unused-parameter)
fi

BOOST_INC=()
if ! echo '#include <boost/lockfree/spsc_queue.hpp>' \
        | "$CXX" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    BOOST_INC=(-I "$STUBS/noboost")
fi

echo "==> 编译器: $CXX"
echo "==> 重建临时编译树: $TREE"
rm -rf "$TREE"
mkdir -p "$TREE/basic" "$TREE/ext" "$TREE/signal" "$TREE/risk"

# 1) 桩头文件
cp -R "$STUBS/basic/." "$TREE/basic/"
cp -R "$STUBS/ext/."   "$TREE/ext/"
cp "$STUBS/stubs.cpp"  "$TREE/stubs.cpp"

# 2) 真实源码（软链，零改写）
ln -sf "$QL/basic/PairInfo.h"          "$TREE/basic/PairInfo.h"
ln -sf "$QL/basic/PairInfoManager.h"   "$TREE/basic/PairInfoManager.h"
ln -sf "$QL/basic/PairInfoManager.cpp" "$TREE/basic/PairInfoManager.cpp"
ln -sf "$QL/signal/SignalGenerator.h"  "$TREE/signal/SignalGenerator.h"
ln -sf "$QL/risk/RiskManager.h"        "$TREE/risk/RiskManager.h"

cp "$HERE/bench.cpp" "$TREE/bench.cpp"

echo "==> 编译"
cd "$TREE"
"$CXX" -std=c++17 -O2 -pthread -I . -I ext ${BOOST_INC[@]+"${BOOST_INC[@]}"} ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
    -o "$TREE/bench" \
    bench.cpp \
    basic/PairInfoManager.cpp \
    stubs.cpp

echo "==> 运行"
"$TREE/bench"
