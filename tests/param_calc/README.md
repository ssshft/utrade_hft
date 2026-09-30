# 开仓参数计算器

**用真实的 `SignalGenerator.cpp` 算开仓阈值**，而不是靠人肉推公式。

存在的理由：开仓阈值不是配置项，是 `SignalGenerator::RecalcOrderParams` 从价差分位数
**推导**出来的（`StartSpread = 分位数 × spreadAdjPct − direction × 执行成本`），
开关判据又是 `StartSpread` 穿过 0。手算很容易把**符号**、**成本口径**、**分位数方向**
搞错一个，结论就完全反了。

## 怎么跑

```bash
./run.sh                                    # 内置演示（用户报的"价差 0.0002 上下"）
./run.sh band <center> <halfwidth>          # 四个轴围绕 center 波动 ±halfwidth
./run.sh axes <bBA_DQ> <bBA_UQ> <bBB_DQ> <bBB_UQ> <aSB_DQ> <aSB_UQ> <aSA_DQ> <aSA_UQ>
./run.sh sweep <center> <halfwidth>         # 反解：要让开仓成立，参数得改成什么
CXX=g++-13 ./run.sh band 0 0.00213          # 指定编译器
```

例子（DOGE ≈ 0.0937，绝对价差 0.0002 USDT → 相对 21.3 bp）：

```bash
./run.sh band 0 0.00213     # -> 四个开仓开关全开，能开仓
./run.sh band 0.0002 0.0002 # -> 四个开仓开关全关，开不了
```

输出五节：成本口径 / 输入分位数 / 推导出的 orderParams / 开仓门槛与缺口 / 信号试算，
`demo` 与 `sweep` 模式额外输出第六节"反解"。

> 边界情况：若下分位 `askAskDQ = 0`（历史上价差从未跌破 0），
> `F_mt / |askAskDQ|` 会除零。工具**不打印 `inf`**，而是直接说明
> 「开多在任何参数下都不可能成立」—— 因为 `spreadAdjPct` 只能放大一个非零的分位数。
> 这恰恰是最干净的"没有 edge"证据。

## 价差口径（读输出前必须先懂这个）

`dbp/etc/dbprocess.xml` 里 `spreadcalctype = 1` = `SPCT_PRICEDIV2`
（`dbp/sig/dbp/include.h:9-14`），即 `spread = (d2 − d1) / d1`；
`dbp/dbprocess/dbsnap.h:303-306` 的调用把 **active 放 d1、passive 放 d2**。

所以四个轴是**无量纲的相对价差**，且（`config.json` 的 pairKeys 是 `BINANCE|GATEIO`）：

```
spreadBidAsk = (GATEIO_ask − BINANCE_bid) / BINANCE_bid
spreadBidBid = (GATEIO_bid − BINANCE_bid) / BINANCE_bid
spreadAskBid = (GATEIO_bid − BINANCE_ask) / BINANCE_ask
spreadAskAsk = (GATEIO_ask − BINANCE_ask) / BINANCE_ask
```

**正 = GATEIO 比 BINANCE 贵。** 工具内部一律用这个口径，输入输出都是它。

> `dbp/etc/dbprocess.json` 里写的是 `spreadcalctype = 0`，与 xml 冲突。
> **代码只读 xml**（`dbpinitialer.cpp:28` 硬编码 `dbprocess.xml`），以 xml 为准。

## 自足性

不读配置、不起 `dbprocess` / `tb`、不碰共享内存、不联网。
临时树建在 `/tmp/paramcalc_build`，只软链两个真实文件
（`signal/SignalGenerator.cpp/.h`），其余全部来自
`../market_to_algo/stubs/ext/**`（与另外两个套件共用）。

链接的源码只有 `signal/SignalGenerator.cpp` 一个 TU —— 因为阈值计算的全部逻辑都在它里面，
`PairInfo` 是纯数据结构（header-only），构造一个填好分位数的 `PairInfo` 就够了。

编译加了 `-w`：真实头文件（`DataStruct.h` / `time_util.h`）在 clang 下有一堆
deprecated `sprintf` / string-compare 警告，那是生产代码的历史包袱，不是本工具的问题；
目标是 Ubuntu 的 g++，那边本来也不出。

## 它**不能**做什么

- 不校验 `PairInfo` 之外的状态（对账、持仓、限额）—— 只看阈值与开关。
- 不模拟报单/成交/撤单 —— 那是 `../algo_exec` 的活。
- 不读真实分位数 —— 分位数得你自己从 `dbp` 数据或日志里取，用 `axes` 模式喂进来。

相关文档：`../../docs/实盘接入与参数配置.md`。
