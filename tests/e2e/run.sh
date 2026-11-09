#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 逐个功能跑 utrade_hft 的端到端用例
#
#   ./run.sh              # 全跑，最后一行是总计
#   ./run.sh 03           # 只跑 03 号（前缀匹配，写 "03" 或 "03_close" 都行）
#   ./run.sh 01 03 07     # 跑几个
#   ./run.sh --list       # 列出所有用例，不跑
#   ./run.sh -f           # 先强制重编真实源码（改了 quant_library 之后用）
#
# 每个用例是 tests/e2e/NN_xxx.cpp，一个文件只测一个功能，从上往下读就是一条链路。
# 加一个用例：复制任意一个 .cpp 改名，改里面的场景和断言 —— run.sh 自动发现。
#
# ⚠️ macOS 自带 bash 3.2：不要用 mapfile / 关联数组。
# ---------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TREE="${E2E_BUILD:-${TMPDIR:-/tmp}/e2e_build}"
TMPBASE="${TMPDIR:-/tmp}/utrade_e2e"

FORCE=""
if [[ "${1:-}" == "-f" ]]; then FORCE="-f"; shift; fi

# 用例清单（按文件名排序 = 执行顺序）
ALL=()
while IFS= read -r line; do
    [[ -n "$line" ]] && ALL+=("$line")
done <<< "$(cd "$HERE" && ls -1 [0-9][0-9]_*.cpp 2>/dev/null | sed 's/\.cpp$//')"

if [[ ${#ALL[@]} -eq 0 ]]; then echo "错误：$HERE 下没有 NN_*.cpp" >&2; exit 1; fi

if [[ "${1:-}" == "--list" ]]; then
    for c in "${ALL[@]}"; do printf '%s\n' "$c"; done
    exit 0
fi

# ---- 选中的用例 ------------------------------------------------------------
SEL=()
if [[ $# -eq 0 ]]; then
    SEL=("${ALL[@]}")
else
    for pat in "$@"; do
        hit=0
        for c in "${ALL[@]}"; do
            if [[ "$c" == "$pat"* ]]; then SEL+=("$c"); hit=1; fi
        done
        if [[ $hit -eq 0 ]]; then
            echo "错误：没有匹配 '$pat' 的用例（./run.sh --list 看清单）" >&2
            exit 1
        fi
    done
fi

# ---- 编译（真实源码只编一次，用例按需编）----------------------------------
if ! E2E_BUILD="$TREE" "$HERE/build.sh" $FORCE >/dev/null; then
    echo "编译真实源码失败，单独跑 ./build.sh 看原因" >&2
    exit 1
fi

if [[ -n "${CXX:-}" ]]; then
    CXXBIN="$CXX"
elif command -v g++ >/dev/null 2>&1; then
    CXXBIN="$(command -v g++)"
elif command -v clang++ >/dev/null 2>&1; then
    CXXBIN="$(command -v clang++)"
else
    CXXBIN="$(command -v c++)"
fi

RJ_INC=()
if ! echo '#include <rapidjson/document.h>' | "$CXXBIN" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1; then
    RJ_INC=(-isystem "$HERE/../third_party")
fi

echo "=============================================="
total_pass=0; total_fail=0; failed_cases=""
for c in "${SEL[@]}"; do
    bin="$TREE/obj/case_$c"
    if [[ ! -x "$bin" || "$HERE/$c.cpp" -nt "$bin" || "$HERE/harness.h" -nt "$bin" ]]; then
        # -Wno-* 只是压掉真实源码里的历史告警（sprintf 之类），跟用例本身无关
        # E2E_CXXFLAGS 同 build.sh：给覆盖率之类的一次性需求留的口子，默认空
        if ! "$CXXBIN" -std=c++17 -O0 -g -pthread ${E2E_CXXFLAGS:-} \
                -Wno-deprecated-declarations -Wno-format -Wno-unused-variable \
                -I "$TREE" -I "$TREE/ext" -I "$HERE" \
                ${RJ_INC[@]+"${RJ_INC[@]}"} \
                -o "$bin" "$HERE/$c.cpp" "$TREE/obj/libreal.a"; then
            echo "编译 $c 失败" >&2
            failed_cases="$failed_cases $c"
            continue
        fi
    fi

    # 每个用例一个干净目录：它写的 csv / json 不会被下一个用例读到
    export E2E_TMP="$TMPBASE/$c"
    rm -rf "$E2E_TMP"; mkdir -p "$E2E_TMP"

    out="$("$bin" 2>&1)"; rc=$?
    printf '%s\n' "$out"

    p=$(printf '%s\n' "$out" | sed -n 's/^--- .*PASS \([0-9]*\).*/\1/p' | tail -1)
    f=$(printf '%s\n' "$out" | sed -n 's/^--- .*FAIL \([0-9]*\).*/\1/p' | tail -1)
    total_pass=$((total_pass + ${p:-0}))
    total_fail=$((total_fail + ${f:-0}))
    [[ $rc -ne 0 ]] && failed_cases="$failed_cases $c"
    echo
done

echo "=============================================="
printf '用例 %d 个   PASS %d   FAIL %d\n' "${#SEL[@]}" "$total_pass" "$total_fail"
if [[ -n "$failed_cases" ]]; then
    printf '失败用例:%s\n' "$failed_cases"
    exit 1
fi
echo "ALL PASS"
