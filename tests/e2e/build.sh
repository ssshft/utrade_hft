#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# e2e 用例的编译脚本（一般不用手敲，run.sh 会自己调）
#
# 干两件事：
#   1. 在 $E2E_BUILD 下搭一棵"桩头文件 + 真实源码软链"的编译树
#      （和 ../algo_exec/run.sh 完全同构，只是把目标文件留下来复用）
#   2. 把真实源码编成 libreal.a —— 12 个用例共用，只编一次
#
# 真实源码是软链进来的，**零改写**。唯一被替换的是外部依赖：
#   ../algo_exec/stubs/basic   执行侧的纯副作用桩（落盘日志 / redis / 发消息）
#   ../market_to_algo/stubs/ext 两边共用的外部桩（fmt / json / dbp / command_helper）
#
# 用法：./build.sh [-f]     # -f 强制重编（改了真实源码之后用）
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QL="$REPO/quant_library"
EXT="$HERE/../market_to_algo/stubs/ext"
ALGO_STUBS="$HERE/../algo_exec/stubs/basic"
TP="$HERE/../third_party"
TREE="${E2E_BUILD:-${TMPDIR:-/tmp}/e2e_build}"
FORCE="${1:-}"

pick_cxx() {
    if [[ -n "${CXX:-}" ]]; then echo "$CXX"; return; fi
    local c
    for c in /usr/bin/g++ /usr/bin/c++ /usr/bin/clang++; do
        [[ -x "$c" ]] && { echo "$c"; return; }
    done
    for c in g++ c++ clang++; do
        command -v "$c" >/dev/null 2>&1 && { echo "$c"; return; }
    done
}
CXX="$(pick_cxx)"
[[ -n "$CXX" ]] || { echo "错误：找不到 C++ 编译器，用 CXX=... ./build.sh 指定" >&2; exit 1; }

[[ -d "$EXT" ]]        || { echo "错误：缺 $EXT" >&2; exit 1; }
[[ -d "$ALGO_STUBS" ]] || { echo "错误：缺 $ALGO_STUBS" >&2; exit 1; }

# rapidjson：系统装了就用系统的，没有才用 third_party（RuntimeConfig.cpp 需要它）
RJ_INC=()
if ! echo '#include <rapidjson/document.h>' | "$CXX" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    [[ -f "$TP/rapidjson/document.h" ]] || { echo "错误：没有 rapidjson" >&2; exit 1; }
    RJ_INC=(-isystem "$TP")
fi

# ---- 1) 编译树 -------------------------------------------------------------
if [[ ! -d "$TREE/basic" || "$FORCE" == "-f" ]]; then
    echo "==> 重建编译树 $TREE"
    rm -rf "$TREE"
    mkdir -p "$TREE/algo" "$TREE/basic" "$TREE/signal" "$TREE/risk" "$TREE/ext" "$TREE/obj"
    cp -R "$ALGO_STUBS/." "$TREE/basic/"
    cp -R "$EXT/."        "$TREE/ext/"

    link() { ln -sf "$QL/$1" "$TREE/$1"; }
    for f in \
        basic/DataStruct.h basic/BaseAlgoOrder.h basic/BaseAlgoOrder.cpp \
        basic/AlgoPairOrder.h basic/AlgoPairOrder.cpp \
        basic/AlgoRebalanceOrder.h basic/AlgoRebalanceOrder.cpp \
        basic/PositionManager.h basic/PositionManager.cpp \
        basic/OrderManager.h basic/OrderManager.cpp \
        basic/PairManager.h basic/PairManager.cpp \
        basic/LimitManager.h basic/LimitManager.cpp \
        basic/LimitBoard.h basic/LimitBoard.cpp \
        basic/AlgoOrderManager.h basic/AlgoOrderManager.cpp \
        basic/AccountManager.h basic/AccountManager.cpp \
        basic/SpreadManager.h basic/SpreadManager.cpp \
        basic/QuantTrade.h basic/AlgoFishingOrder.h \
        basic/PairInfo.h basic/PairInfoManager.h basic/PairInfoManager.cpp \
        basic/RuntimeConfig.h basic/RuntimeConfig.cpp \
        algo/AlgoContext.h algo/AlgoContext.cpp \
        algo/PairTradingContext.h algo/PairTradingContext.cpp \
        signal/SignalGenerator.h signal/SignalGenerator.cpp \
        signal/SpreadStatsBuilder.h signal/SpreadStatsBuilder.cpp \
        risk/RiskManager.h risk/RiskManager.cpp ; do
        link "$f"
    done
    # 仓库自带的 nlohmann（AlgoPairOrder.h 在用）；整目录软链，别让 -I ext 命中空壳桩
    ln -sfn "$QL/basic/json" "$TREE/basic/json"
fi

# ---- 2) 真实源码 -> libreal.a（只编一次）-----------------------------------
if [[ ! -f "$TREE/obj/libreal.a" || "$FORCE" == "-f" ]]; then
    echo "==> 编译真实源码（$CXX）"
    cd "$TREE"
    OBJS=()
    for src in \
        algo/AlgoContext.cpp algo/PairTradingContext.cpp \
        basic/BaseAlgoOrder.cpp basic/AlgoPairOrder.cpp basic/AlgoRebalanceOrder.cpp \
        basic/PositionManager.cpp basic/OrderManager.cpp basic/PairManager.cpp \
        basic/LimitManager.cpp basic/LimitBoard.cpp basic/AlgoOrderManager.cpp \
        basic/AccountManager.cpp basic/SpreadManager.cpp \
        basic/PairInfoManager.cpp basic/RuntimeConfig.cpp \
        signal/SignalGenerator.cpp signal/SpreadStatsBuilder.cpp \
        risk/RiskManager.cpp ; do
        o="obj/$(basename "$src" .cpp).o"
        # E2E_CXXFLAGS 是给覆盖率之类的一次性需求留的注入口，默认空 = 行为不变
        # 例：E2E_CXXFLAGS="-fprofile-instr-generate -fcoverage-mapping"
        "$CXX" -std=c++17 -O0 -g -pthread ${E2E_CXXFLAGS:-} -I . -I ext ${RJ_INC[@]+"${RJ_INC[@]}"} -c "$src" -o "$o"
        OBJS+=("$o")
    done
    ar rcs "$TREE/obj/libreal.a" "${OBJS[@]}"
    echo "==> libreal.a 就绪"
else
    echo "==> 复用 $TREE/obj/libreal.a（改了真实源码就 ./build.sh -f）"
fi
