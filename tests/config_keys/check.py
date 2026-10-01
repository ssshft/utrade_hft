#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
config.json 与 pre_start 的 key 一致性静态检查

背景：参数是 `PairTradingStrategy::pre_start` 里用 rapidjson 逐字段读的，
没有中间解析器，所以也没有"解析器读过的 key 清单"可以自动比对。
这个脚本用**源码扫描**代替：把 `HasMember("x")` / `["x"]` 里的 key 抠出来，
和 `etc/config.json` 的 key 做双向集合比较。

两个方向都会红，而且都会打印具体是哪个 key：

  1. 配置里有、pre_start 没读  -> 写了也没用的死配置（或者 key 拼错了）
  2. pre_start 读了、配置里没有 -> 读的是不存在的 key（同理，多半是拼错）

用法：
    python3 tests/config_keys/check.py                 # 用默认路径
    python3 tests/config_keys/check.py <config.json> <PairTradingStrategy.cpp>

退出码 0 = 一致，1 = 有差异。不需要编译，不依赖 rapidjson。
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

DEFAULT_JSON = os.path.join(REPO, "etc", "config.json")
DEFAULT_SRC = os.path.join(REPO, "src", "strategy", "PairTradingStrategy.cpp")

# 挂在 op 下的三个参数段（与 pre_start 里的三个 HasMember 对应）。
# 算法单参数没有独立段，2026-10-02 起并在 op.pairTrading 里。
SECTIONS = ["feeSlippage", "risk", "pairTrading"]

# 不在 pre_start 里读的 key：
#   · strategyOp / timerInterval / strategyIds —— 框架侧读（基类 _init / 策略装载），
#     写在 config.json 的 op 段里是既有约定，不属于本策略的参数
#   · op / tag / log / redis / dbp —— 顶层容器名，不是参数
NOT_IN_PRE_START = {"strategyOp", "timerInterval", "strategyIds",
                    "op", "tag", "log", "redis", "dbp"}


def strip_comments(src):
    """去掉 // 与 /* */ 注释，免得注释里的示例 key 被当成真的读取点。"""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def main():
    cfg_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_JSON
    src_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_SRC

    for p in (cfg_path, src_path):
        if not os.path.exists(p):
            print("找不到文件: %s" % p)
            return 1

    with open(cfg_path, encoding="utf-8") as f:
        cfg = json.load(f)

    op = cfg.get("op", {})

    # ---- 配置侧：op 下的平铺 key + 四段里的 key（_note* 是文档键，跳过）----
    in_file = {}
    for k, v in op.items():
        if isinstance(v, dict):
            for kk in v:
                if not kk.startswith("_"):
                    in_file[kk] = k
        elif not k.startswith("_"):
            in_file[k] = "op"

    # ---- 源码侧：pre_start 里出现过的 key ----
    with open(src_path, encoding="utf-8") as f:
        src = strip_comments(f.read())

    in_src = set()
    for pat in (r'HasMember\("([^"]+)"\)', r'\["([^"]+)"\]'):
        in_src.update(re.findall(pat, src))
    # 段名本身不是参数 key
    for s in SECTIONS:
        in_src.discard(s)
    in_src -= NOT_IN_PRE_START
    for k in NOT_IN_PRE_START:
        in_file.pop(k, None)

    only_file = sorted(k for k in in_file if k not in in_src)
    only_src = sorted(k for k in in_src if k not in in_file)

    print("配置: %s" % os.path.relpath(cfg_path, REPO))
    print("源码: %s" % os.path.relpath(src_path, REPO))
    print("配置侧 %d 个 key，源码侧 %d 个 key" % (len(in_file), len(in_src)))

    ok = True
    if only_file:
        ok = False
        print("\n[FAIL] 配置里有、pre_start 没读（写了不生效，或 key 拼错了）：")
        for k in only_file:
            print("        %-40s (在 %s 段)" % (k, in_file[k]))
    if only_src:
        ok = False
        print("\n[FAIL] pre_start 读了、配置里没有（读了不存在的 key，多半是拼错）：")
        for k in only_src:
            print("        %s" % k)

    if ok:
        print("\n[PASS] 两侧 key 完全一致")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
