#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 行情 -> 算法单 半程测试套件的构建/运行脚本
#
# 这台机器上编译不了整个工程（缺 fmt / rapidjson / boost / dbp / pubsub /
# securitymanager，且 build/ 里的 include 路径指向部署机）。所以这里搭一棵
# "桩头文件 + 真实源码软链" 的临时树：
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
# 用法：  ./run.sh            # 编译并运行
#         ./run.sh -v         # 额外打开 -Wall -Wextra
# ---------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QL="$REPO/quant_library"
TREE="${TMPDIR:-/tmp}/ptsuite_build"

CXX="${CXX:-/usr/bin/clang++}"
EXTRA_FLAGS=()
if [[ "${1:-}" == "-v" ]]; then
    EXTRA_FLAGS=(-Wall -Wextra -Wno-unused-parameter)
fi

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
"$CXX" -std=c++17 -I . -I ext ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
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
