#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pre_start 逐字段读写的**字段级**交叉校验（key 级校验见同目录 check.py）。

check.py 只保证 "配置里的 key" 与 "源码里 HasMember 的 key" 两侧一致；
它看不出下面三类错误 —— 这三类都是改完能编译过、跑起来才炸的：

  (1) 赋到了一个不存在的字段上
      例：m_ptCfg.tier1WaitSec = ...   （真名 tier1WaitUs） -> 编译期就报错，算轻的

  (2) 用错了取值函数，静默截断
      例：int 字段写成 m_ptCfg.activePriceTickNum = std::stoll(...)
          -> 隐式窄化，编译器给 narrowing 警告（可能被 -w 吞掉）
      例：bool 字段写成 std::stod(...) -> 1/0 隐式转 bool 居然能过

  (3) 字段加进结构体了、配置也写了、但 pre_start 忘了读
      例：PairTradingConfig 新增 tier4ForgoProfit，配置里加了 key，pre_start 没加一行
          -> 配置静默失效，改配置文件完全没反应（**最危险**的一类）

用法：
    python3 tests/config_keys/field_check.py          # 在仓库根目录跑
    python3 tests/config_keys/field_check.py -v       # 顺带打印各段的逐字段明细

退出码：0 = 全部通过；1 = 有上述三类问题。

已知不检查的东西（有意为之）：
  - 不检查数值/单位换算是否正确（如 ×1000000 的秒->微秒），那是语义，静态看不出来；
  - 不检查 key 落在哪个段里（check.py 也不查，两个脚本都只看名字集合）；
  - 不检查 SetConfig 是否真的被调用（只看赋值语句）。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

STRATEGY_CPP = os.path.join(ROOT, "src", "strategy", "PairTradingStrategy.cpp")
HDR_SIGNAL = os.path.join(ROOT, "quant_library", "signal", "SignalGenerator.h")
HDR_RISK = os.path.join(ROOT, "quant_library", "risk", "RiskManager.h")
HDR_CTX = os.path.join(ROOT, "quant_library", "algo", "PairTradingContext.h")

# 三个块：(配置段名, 赋值目标前缀, 结构体名, 结构体所在头文件)
# 算法单参数没有独立结构体，2026-10-02 起并进 PairTradingConfig 的 op.pairTrading 段。
BLOCKS = [
    ("feeSlippage", "c", "FeeSlippageConfig", HDR_SIGNAL),
    ("risk", "c", "RiskConfig", HDR_RISK),
    ("pairTrading", "m_ptCfg", "PairTradingConfig", HDR_CTX),
]

# 字段类型 -> 允许的取值写法。
# pre_start 的取值形态就三种（见 PairTradingStrategy.cpp 的注释）：
#   数值  Value.GetString() -> std::stod / std::stoll / std::stoi
#   布尔  Value.GetString() == std::string("true")          -> 记作 StrTrue
#   枚举  Value.GetString() 查表 / 字符串直比               -> 记作 GetString
ALLOWED = {
    "double": {"std::stod"},
    "int64_t": {"std::stoll"},
    "int": {"std::stoi"},
    "bool": {"StrTrue"},
    "OrderType": {"GetString"},
    # 字符串字段（如 spreadStatsStatePath）直接 GetString()，没有转换函数
    "std::string": {"GetString"},
}
READABLE = {
    "std::stod": "std::stod(...)",
    "std::stoll": "std::stoll(...)",
    "std::stoi": "std::stoi(...)",
    "StrTrue": 'GetString() == "true"',
    "GetString": "GetString() + 查表/直比",
    "vector": "emplace_back",
}
# stra::xxx 枚举统一走 GetString + 查表
ENUM_PREFIX = "stra::"

# 平铺在 op 段（不在三个子对象里）的字段，由 pre_start 直接读。
# 这些字段不在任何块内，单独列出以免被 "字段没被读" 误报。
FLAT_FIELDS = {
    "PairTradingConfig": {"pairKeys", "activeAccountId", "passiveAccountId",
                          "maxPositionValue", "maxAmount", "targetAmount",
                          "csvStatePath"},
}

# 参与检查的配置结构体名。作为**别的结构体的字段**出现时（PairTradingConfig 里的
# feeSlippage / risk），它们是子对象挂载点：读取方式是 `auto& c = m_ptCfg.feeSlippage;`
# 而不是 `x = ...`，所以 "字段覆盖率" 检查要豁免。
CONFIG_STRUCTS = {name for _, _, name, _ in BLOCKS}


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def strip_comments(text):
    """去掉 // 行注释（本工程不用 /* */ 块注释写参数，遇到就原样留下让正则失败报警）。"""
    out = []
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("//"):
            out.append("")
            continue
        i = line.find("//")
        out.append(line[:i] if i >= 0 else line)
    return "\n".join(out)


def struct_body(src, struct_name):
    """返回 struct <name> { ... } 的花括号内文本，按花括号配对找结尾。"""
    m = re.search(r"\bstruct\s+" + re.escape(struct_name) + r"\s*\{", src)
    if not m:
        raise SystemExit("找不到 struct %s" % struct_name)
    i = src.index("{", m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1:j]
    raise SystemExit("struct %s 花括号不配对" % struct_name)


def struct_fields(src, struct_name):
    """{字段名: 类型}。把 body 去注释后按 ';' 切开，每段是 'TYPE NAME {init}'。"""
    body = strip_comments(struct_body(src, struct_name))
    fields = {}
    for chunk in body.split(";"):
        chunk = chunk.strip()
        if not chunk:
            continue
        # 砍掉初始化列表 / 嵌套结构体
        k = chunk.find("{")
        if k >= 0:
            chunk = chunk[:k].strip()
        if not chunk or chunk.startswith("struct") or chunk.startswith("using"):
            continue
        parts = chunk.split()
        if len(parts) < 2:
            continue
        name = parts[-1]
        typ = " ".join(parts[:-1])
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            continue
        fields[name] = typ
    return fields


# ---- 解析 pre_start 里的赋值 -------------------------------------------------
#
# 形态统一是 `目标.字段 = 右值;`，所以按行抓 "字段 + 右值片段"，再按右值形态分类：
#   std::stod / std::stoll / std::stoi  -> 直接认出来
#   ...GetString() ... "true"           -> StrTrue（布尔字段）
#   其余（it->second / 字符串直比）      -> GetString（枚举字段）
ASSIGN_RE = re.compile(
    r"(?:^|[\s(=])(?:c|m_ptCfg)\.(?P<field>[A-Za-z_]\w*)\s*=\s*(?P<rhs>[^;\n]*)"
)


def classify(rhs):
    if "std::stod" in rhs:
        return "std::stod"
    if "std::stoll" in rhs:
        return "std::stoll"
    if "std::stoi" in rhs:
        return "std::stoi"
    if "GetString()" in rhs and '"true"' in rhs:
        return "StrTrue"
    return "GetString"


def block_text(src, section):
    """取 `if (op.HasMember("<section>")) { ... }` 的花括号内文本。"""
    m = re.search(r'if\s*\(\s*op\.HasMember\(\s*"' + re.escape(section) + r'"\s*\)\s*\)\s*\{', src)
    if not m:
        return None
    i = src.index("{", m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1:j]
    return None


def parse_block(text):
    """返回 (读到的字段集合, [(字段, 取值写法)])。"""
    if text is None:
        return set(), []
    plain = strip_comments(text)
    seen, found = set(), []
    for m in ASSIGN_RE.finditer(plain):
        field = m.group("field")
        if field in seen:
            continue
        seen.add(field)
        found.append((field, classify(m.group("rhs"))))
    return seen, found


def parse_flat(src):
    """平铺在 op 段、不在三个块里的赋值（m_ptCfg.x = std::stod(op["x"].GetString())）。"""
    plain = strip_comments(src)
    seen, found = set(), []
    for m in re.finditer(
        r"m_ptCfg\.([A-Za-z_]\w*)\s*=\s*(std::stod|std::stoi|std::stoll)\s*\(", plain
    ):
        if m.group(1) in seen:
            continue
        seen.add(m.group(1))
        found.append((m.group(1), m.group(2)))
    if re.search(r"m_ptCfg\.csvStatePath\s*=\s*op\[", plain):
        seen.add("csvStatePath")
        found.append(("csvStatePath", "GetString"))
    if re.search(r"m_ptCfg\.pairKeys\.emplace_back", plain):
        seen.add("pairKeys")
        found.append(("pairKeys", "vector"))
    return seen, found


def check_type(errors, where, struct_name, field, typ, fn):
    """比对取值写法与声明类型。"""
    if typ.startswith(ENUM_PREFIX) or typ == "OrderType":
        allowed = ALLOWED["OrderType"]
    else:
        allowed = ALLOWED.get(typ)
    if allowed is None:
        errors.append("[%s] 字段 `%s.%s` 的类型 `%s` 没有约定取值写法" % (where, struct_name, field, typ))
        return
    if fn not in allowed:
        errors.append(
            "[%s] 字段 `%s.%s`（%s）用了 `%s`，应为 %s"
            % (where, struct_name, field, typ, READABLE.get(fn, fn),
               " 或 ".join(READABLE.get(a, a) for a in sorted(allowed)))
        )


def main():
    verbose = "-v" in sys.argv or "--verbose" in sys.argv
    src = read(STRATEGY_CPP)

    errors = []
    total_fields = 0
    total_reads = 0

    for section, prefix, struct_name, hdr in BLOCKS:
        fields = struct_fields(read(hdr), struct_name)
        text = block_text(src, section)
        read_set, reads = parse_block(text)

        if text is None:
            errors.append("[%s] 找不到 `if (op.HasMember(\"%s\"))` 块" % (section, section))
            continue

        # --- 检查 1：赋值的字段必须存在 ---
        for field, fn in reads:
            if field not in fields:
                errors.append(
                    "[%s] 字段 `%s.%s` 不存在（可能是拼写错，或写进了别的结构体）"
                    % (section, struct_name, field)
                )

        # --- 检查 2：取值写法必须匹配字段类型 ---
        for field, fn in reads:
            typ = fields.get(field)
            if typ is None:
                continue
            check_type(errors, section, struct_name, field, typ, fn)

        # --- 检查 3：结构体字段都必须被读到 ---
        # 豁免两类：平铺在 op 段的字段；子对象挂载点（类型本身是个配置结构体）。
        exempt = set(FLAT_FIELDS.get(struct_name, set()))
        exempt |= {f for f, t in fields.items() if t in CONFIG_STRUCTS}
        for field, typ in fields.items():
            if field in read_set or field in exempt:
                continue
            errors.append(
                "[%s] 字段 `%s.%s`（%s）在 pre_start 里没有读取 -> 配置里写它不会生效"
                % (section, struct_name, field, typ)
            )

        total_fields += len(fields)
        total_reads += len(reads)

        if verbose:
            print("  [%s] struct %s: %d 字段, 块内 %d 条赋值" % (section, struct_name, len(fields), len(reads)))
            for field, fn in sorted(reads):
                print("      %-32s %-16s %s" % (field, fn, fields.get(field, "<不存在>")))

    # 平铺字段单独校验（它们不属于任何块）—— 先把三个块的内容从源码里挖掉，
    # 否则块里的赋值会被当成"平铺"重复统计，条数就没意义了。
    src_flat = src
    for section, _, _, _ in BLOCKS:
        t = block_text(src, section)
        if t:
            src_flat = src_flat.replace(t, "", 1)
    flat_fields = struct_fields(read(HDR_CTX), "PairTradingConfig")
    flat_read, flat_reads = parse_flat(src_flat)
    for field, fn in flat_reads:
        if field not in flat_fields:
            errors.append("[flat] 字段 `PairTradingConfig.%s` 不存在" % field)
            continue
        typ = flat_fields[field]
        if fn == "vector":
            if not typ.startswith("std::vector"):
                errors.append("[flat] 字段 `PairTradingConfig.%s`（%s）是 emplace_back 写法，但类型不是 vector"
                              % (field, typ))
            continue
        if fn == "GetString":
            # csvStatePath 这类字符串字段
            if typ != "std::string":
                errors.append("[flat] 字段 `PairTradingConfig.%s`（%s）用了 GetString，应为数值取值"
                              % (field, typ))
            continue
        check_type(errors, "flat", "PairTradingConfig", field, typ, fn)
    for field in FLAT_FIELDS["PairTradingConfig"]:
        if field not in flat_read:
            errors.append("[flat] 字段 `PairTradingConfig.%s` 在 pre_start 里没有读取" % field)

    print("结构体字段合计 %d 个，块内赋值合计 %d 条，平铺赋值 %d 条"
          % (total_fields, total_reads, len(flat_reads)))

    if errors:
        print("\n[FAIL] 发现 %d 处问题：" % len(errors))
        for e in errors:
            print("  - " + e)
        return 1

    print("[PASS] 字段名、取值写法、字段覆盖率三项全部一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
