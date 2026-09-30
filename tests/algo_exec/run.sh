#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 算法单执行（拆单 / 成交 / 撤单 / 风控平仓）+ 全链路串联 测试套件
#
# 与 ../market_to_algo 的分工：
#   market_to_algo —— 策略侧「行情 -> 创建算法单」，执行端是桩
#   algo_exec      —— 执行侧「算法单 -> 子单 -> 回报 -> 更新 -> 撤单」，外加
#                     一条把两边串起来的循环（行情 -> 建单 -> 拆单 -> 成交 -> 撤单 -> 风控）
#
# 自足：不读配置、不起 dbprocess / tb、不碰共享内存、不联网。
# 出向指令（报单/撤单/查询）被换成进程内记录器，可以逐字段断言。
#
# 编译树 = 「桩头文件 + 真实源码软链」：
#
#   真实源码（软链，零改写）：
#     algo/AlgoContext.cpp/.h          ← 执行侧总入口（OnSpread / OnOrder / OnTimer）
#     algo/PairTradingContext.cpp/.h   ← 策略侧（串联用例要用）
#     basic/DataStruct.h               ← 真实！不再用桩（见 README「为什么这次能用真实 DataStruct」）
#     basic/BaseAlgoOrder.cpp/.h
#     basic/AlgoPairOrder.cpp/.h
#     basic/PositionManager.cpp/.h
#     basic/OrderManager.cpp/.h
#     basic/PairManager.cpp/.h
#     basic/LimitManager.cpp/.h  basic/LimitBoard.h
#     basic/AlgoOrderManager.cpp/.h
#     basic/AccountManager.cpp/.h
#     basic/SpreadManager.cpp/.h
#     basic/QuantTrade.h               ← 真实（header-only），出向边界在 om::TradeClient
#     basic/PairInfo.h / PairInfoManager.*
#     signal/*  risk/*
#
#   桩（stubs/basic，只补外部依赖和纯副作用）：
#     Utility.h        ConcurrentQueue 换成单线程 deque（丢掉 moodycamel / perf.h）
#     Convert.h        落盘日志 -> no-op
#     QuantPub.h       发 redis  -> no-op
#     QuantDbp.h / LarkRebot.h / WriteFileContent.h  同上
#     StrategyConfig.h 读 ini（boost）-> 固定值
#     AlgoFishingOrder.h / AlgoRebalanceOrder.h  只留类声明（AlgoContext 里有 static_cast）
#
#   桩（../market_to_algo/stubs/ext，与策略侧共用）：
#     data_struct.h / pubsub_protocol.h / dbp/include.h / fmt / json / time_util /
#     log_engine / securitymanager / StraException / crypto_errors
#     command_helper.h  ← om::TradeClient 记录器
#
# 用法：
#   ./run.sh              # 编译并运行
#   ./run.sh -v           # 额外打开 -Wall -Wextra
#   CXX=g++-13 ./run.sh   # 指定编译器（默认自动挑 g++）
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QL="$REPO/quant_library"
EXT="$HERE/../market_to_algo/stubs/ext"
TREE="${TMPDIR:-/tmp}/algosuite_build"

# ---- 挑编译器 -------------------------------------------------------------
# 优先 g++：目标环境是 Ubuntu，/usr/bin/g++ 是系统自带的那个。
# macOS 上 /usr/bin/g++ 是 clang++ 的软链，也能用，所以这个顺序两边都安全。
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

if [[ "$CXX" == */* ]]; then
    if [[ ! -x "$CXX" ]]; then
        echo "错误：$CXX 不存在或不可执行。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
        exit 1
    fi
elif ! command -v "$CXX" >/dev/null 2>&1; then
    echo "错误：PATH 上找不到 $CXX。请用 CXX=/path/to/g++ ./run.sh 指定。" >&2
    exit 1
fi

if [[ ! -d "$EXT" ]]; then
    echo "错误：找不到共享的外部依赖桩目录 $EXT" >&2
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
mkdir -p "$TREE/algo" "$TREE/basic" "$TREE/signal" "$TREE/risk" "$TREE/ext"

# 1) 桩：执行侧的 basic 桩 + 共享的 ext 桩
cp -R "$HERE/stubs/basic/." "$TREE/basic/"
cp -R "$EXT/."              "$TREE/ext/"

# 2) 真实源码（软链，零改写）
ln -sf "$QL/basic/DataStruct.h"           "$TREE/basic/DataStruct.h"
ln -sf "$QL/basic/BaseAlgoOrder.h"        "$TREE/basic/BaseAlgoOrder.h"
ln -sf "$QL/basic/BaseAlgoOrder.cpp"      "$TREE/basic/BaseAlgoOrder.cpp"
ln -sf "$QL/basic/AlgoPairOrder.h"        "$TREE/basic/AlgoPairOrder.h"
ln -sf "$QL/basic/AlgoPairOrder.cpp"      "$TREE/basic/AlgoPairOrder.cpp"
ln -sf "$QL/basic/PositionManager.h"      "$TREE/basic/PositionManager.h"
ln -sf "$QL/basic/PositionManager.cpp"    "$TREE/basic/PositionManager.cpp"
ln -sf "$QL/basic/OrderManager.h"         "$TREE/basic/OrderManager.h"
ln -sf "$QL/basic/OrderManager.cpp"       "$TREE/basic/OrderManager.cpp"
ln -sf "$QL/basic/PairManager.h"          "$TREE/basic/PairManager.h"
ln -sf "$QL/basic/PairManager.cpp"        "$TREE/basic/PairManager.cpp"
ln -sf "$QL/basic/LimitManager.h"         "$TREE/basic/LimitManager.h"
ln -sf "$QL/basic/LimitManager.cpp"       "$TREE/basic/LimitManager.cpp"
ln -sf "$QL/basic/LimitBoard.h"           "$TREE/basic/LimitBoard.h"
ln -sf "$QL/basic/LimitBoard.cpp"         "$TREE/basic/LimitBoard.cpp"
ln -sf "$QL/basic/AlgoOrderManager.h"     "$TREE/basic/AlgoOrderManager.h"
ln -sf "$QL/basic/AlgoOrderManager.cpp"   "$TREE/basic/AlgoOrderManager.cpp"
ln -sf "$QL/basic/AccountManager.h"       "$TREE/basic/AccountManager.h"
ln -sf "$QL/basic/AccountManager.cpp"     "$TREE/basic/AccountManager.cpp"
ln -sf "$QL/basic/SpreadManager.h"        "$TREE/basic/SpreadManager.h"
ln -sf "$QL/basic/SpreadManager.cpp"      "$TREE/basic/SpreadManager.cpp"
ln -sf "$QL/basic/QuantTrade.h"           "$TREE/basic/QuantTrade.h"
ln -sf "$QL/basic/AlgoFishingOrder.h"     "$TREE/basic/AlgoFishingOrder.h"
ln -sf "$QL/basic/AlgoRebalanceOrder.h"   "$TREE/basic/AlgoRebalanceOrder.h"
ln -sf "$QL/basic/PairInfo.h"             "$TREE/basic/PairInfo.h"
ln -sf "$QL/basic/PairInfoManager.h"      "$TREE/basic/PairInfoManager.h"
ln -sf "$QL/basic/PairInfoManager.cpp"    "$TREE/basic/PairInfoManager.cpp"

ln -sf "$QL/algo/AlgoContext.h"           "$TREE/algo/AlgoContext.h"
ln -sf "$QL/algo/AlgoContext.cpp"         "$TREE/algo/AlgoContext.cpp"
ln -sf "$QL/algo/PairTradingContext.h"    "$TREE/algo/PairTradingContext.h"
ln -sf "$QL/algo/PairTradingContext.cpp"  "$TREE/algo/PairTradingContext.cpp"

ln -sf "$QL/signal/SignalGenerator.h"       "$TREE/signal/SignalGenerator.h"
ln -sf "$QL/signal/SignalGenerator.cpp"     "$TREE/signal/SignalGenerator.cpp"
ln -sf "$QL/signal/SpreadStatsBuilder.h"    "$TREE/signal/SpreadStatsBuilder.h"
ln -sf "$QL/signal/SpreadStatsBuilder.cpp"  "$TREE/signal/SpreadStatsBuilder.cpp"

ln -sf "$QL/risk/RiskManager.h"           "$TREE/risk/RiskManager.h"
ln -sf "$QL/risk/RiskManager.cpp"         "$TREE/risk/RiskManager.cpp"

cp "$HERE/suite.cpp" "$TREE/suite.cpp"

echo "==> 编译"
cd "$TREE"
"$CXX" -std=c++17 -pthread -I . -I ext ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
    -o "$TREE/suite" \
    suite.cpp \
    algo/AlgoContext.cpp \
    algo/PairTradingContext.cpp \
    basic/BaseAlgoOrder.cpp \
    basic/AlgoPairOrder.cpp \
    basic/PositionManager.cpp \
    basic/OrderManager.cpp \
    basic/PairManager.cpp \
    basic/LimitManager.cpp \
    basic/LimitBoard.cpp \
    basic/AlgoOrderManager.cpp \
    basic/AccountManager.cpp \
    basic/SpreadManager.cpp \
    basic/PairInfoManager.cpp \
    signal/SignalGenerator.cpp \
    signal/SpreadStatsBuilder.cpp \
    risk/RiskManager.cpp

echo "==> 运行"
"$TREE/suite"
