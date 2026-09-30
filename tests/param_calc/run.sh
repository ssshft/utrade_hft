#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 开仓参数计算器 —— 用**真实的** SignalGenerator.cpp 算阈值，而不是靠人肉推公式
#
# 为什么需要它：
#   开仓阈值不是配置项，是 RecalcOrderParams 从价差分位数**推导**出来的：
#       StartSpread = 分位数 * spreadAdjPct - direction * (执行成本 - buffer)
#   开仓开关又要求 StartSpread 穿过 0（minSpreadTarget 的门槛）。
#   所以"价差 0.0002 能不能开仓"这个问题的答案，取决于
#       (a) 四个轴的上下分位数各是多少
#       (b) 执行成本口径 F_tt / F_mt
#   手算很容易把符号或成本口径搞错，所以这里直接链接真实源码。
#
# 用法：
#   ./run.sh                                   # 用内置场景（含用户报的 0.0002）跑一遍
#   ./run.sh band <center> <halfwidth>         # 四个轴围绕 center 波动 ±halfwidth
#   ./run.sh axes <bBA_DQ> <bBA_UQ> <bBB_DQ> <bBB_UQ> \
#                 <aSB_DQ> <aSB_UQ> <aSA_DQ> <aSA_UQ>
#   ./run.sh sweep <center> <halfwidth>        # 反解：要让开仓成立，参数得改成什么
#
# 自足：不读配置、不起进程、不联网。只链标准库。
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QL="$REPO/quant_library"
EXT="$HERE/../market_to_algo/stubs/ext"
TREE="${TMPDIR:-/tmp}/paramcalc_build"

pick_cxx() {
    if [[ -n "${CXX:-}" ]]; then echo "$CXX"; return; fi
    local cand
    for cand in /usr/bin/g++ /usr/bin/c++ /usr/bin/clang++; do
        [[ -x "$cand" ]] && { echo "$cand"; return; }
    done
    for cand in g++ c++ clang++; do
        command -v "$cand" >/dev/null 2>&1 && { echo "$cand"; return; }
    done
}

CXX="$(pick_cxx)"
if [[ -z "$CXX" ]]; then
    echo "错误：找不到 C++ 编译器。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
    exit 1
fi
if [[ "$CXX" == */* ]]; then
    [[ -x "$CXX" ]] || { echo "错误：$CXX 不存在或不可执行。" >&2; exit 1; }
elif ! command -v "$CXX" >/dev/null 2>&1; then
    echo "错误：PATH 上找不到 $CXX。" >&2
    exit 1
fi

rm -rf "$TREE"
mkdir -p "$TREE/signal" "$TREE/basic" "$TREE/risk" "$TREE/ext"

cp -R "$EXT/." "$TREE/ext/"

# 只软链这个工具真正要跑的源码。SignalGenerator 是唯一的被测对象。
ln -sf "$QL/basic/DataStruct.h"           "$TREE/basic/DataStruct.h"
ln -sf "$QL/basic/PairInfo.h"             "$TREE/basic/PairInfo.h"
ln -sf "$QL/signal/SignalGenerator.h"     "$TREE/signal/SignalGenerator.h"
ln -sf "$QL/signal/SignalGenerator.cpp"   "$TREE/signal/SignalGenerator.cpp"

cp "$HERE/calc.cpp" "$TREE/calc.cpp"

cd "$TREE"
# -w：真实头文件（DataStruct.h / time_util.h）在 clang 下有一堆 deprecated sprintf /
# string-compare 警告，那是生产代码的历史包袱，不是本工具的问题，全部静音。
# 目标是 Ubuntu 的 g++，那边这些警告本来也不出。
"$CXX" -std=c++17 -O1 -w -I . -I ext \
    -o "$TREE/calc" \
    calc.cpp \
    signal/SignalGenerator.cpp

exec "$TREE/calc" "$@"
