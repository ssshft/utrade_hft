# config_keys —— 配置一致性静态检查

参数是在 `PairTradingStrategy::pre_start` 里用 rapidjson **逐字段读**的，没有中间解析器，
所以没有"解析器读过的 key 清单"可以自动比对，也没有 C++ 单测能覆盖（`pre_start` 依赖
rapidjson，本地 macOS 编译不了 —— 见 `docs/实盘接入与参数配置.md` §9）。
这两个脚本用**源码扫描**补上这个缺口，都不需要编译、不依赖 rapidjson。

| 脚本 | 查什么 | 一句话 |
|---|---|---|
| `check.py` | **key 名**：`etc/config.json` 的 key 集合 ⟷ `pre_start` 里 `HasMember` 的 key 集合 | 防"配置写了没人读 / 代码读了不存在的 key" |
| `field_check.py` | **字段**：结构体成员名、取值写法、字段覆盖率 | 防"赋到不存在的字段 / 用错取值函数 / 加了字段忘了读" |

```bash
python3 tests/config_keys/check.py          # key 级
python3 tests/config_keys/field_check.py    # 字段级
python3 tests/config_keys/field_check.py -v # 顺带打印各段的逐字段明细
```

两个都是退出码 0 = 通过。当前实测：`check.py` 两侧各 88 个 key 一致；
`field_check.py` 90 个结构体字段 / 81 条块内赋值 / 7 条平铺赋值，全部一致。

---

## 配置长什么样

`etc/config.json` 的 `op` 段下有**三个**子对象，加上 7 个平铺 key：

| 段 | 结构体 | 参数个数 |
|---|---|---|
| `op.feeSlippage` | `pt::FeeSlippageConfig` | 12 |
| `op.risk` | `pt::RiskConfig` | 14 |
| `op.pairTrading` | `pt::PairTradingConfig`（本体） | 55 |

`op.pairTrading` 那 55 个里有 18 个是策略参数、2 个是分位数快照（重启免预热）、
**35 个是算法单参数**（报单类型 / 撤单门槛 / rebalance …）。算法单参数**没有独立结构体**，
就是 `PairTradingConfig` 的普通字段，由 `PairTradingContext::BuildAlgoOrderJson` 直接读。

平铺在 `op` 下的 7 个：`pairKeys` / `activeAccountId` / `passiveAccountId` /
`maxPositionValue` / `maxAmount` / `targetAmount` / `csvStatePath`。

---

## check.py —— key 名一致性

把源码里 `HasMember("x")` / `["x"]` 的 key 抠出来（先剥掉注释），
和 `etc/config.json` 的 `op` 段（含三个子对象）做**双向**集合比较：

```
$ # 配置里加了一个没人读的 key
[FAIL] 配置里有、pre_start 没读（写了不生效，或 key 拼错了）：
        typoKeyNobodyReads                       (在 pairTrading 段)

$ # 源码里把 tier1WaitSec 拼成了 tier1WaitSecX
[FAIL] pre_start 读了、配置里没有（读了不存在的 key，多半是拼错）：
        tier1WaitSecX
```

`_note*` 是给人看的文档键，自动跳过。

### 不参与比较的 key

`strategyOp` / `timerInterval` / `strategyIds` —— 这三个由框架侧（基类 `_init` / 策略装载）
读取，写在 `config.json` 的 `op` 段里是既有约定，不属于本策略的参数。
名单在脚本的 `NOT_IN_PRE_START` 里。

---

## field_check.py —— 字段级（取值写法 / 覆盖率）

key 名对了不代表赋值对了。下面三类错误 `check.py` 看不出来，而它们**都能编译过**：

| 类别 | 例子 | 后果 |
|---|---|---|
| ① 赋到不存在的字段 | `m_ptCfg.tier1WaitSec = ...`（真名 `tier1WaitUs`） | 编译期报错，算轻的 |
| ② 取值写法与字段类型不匹配 | `bool` 字段写成 `std::stod(...)`；`int` 字段写成 `std::stoll(...)` | 静默截断 / 1-0 隐式转 bool |
| ③ 加了字段但没读 | `PairTradingConfig` 新增 `tier4ForgoWaitSec`，`pre_start` 忘加一行 | **最危险**：配置里写它完全没反应，静默失效 |

脚本做的事：

1. 从两个头文件里按花括号配对抠出 `FeeSlippageConfig` / `RiskConfig` /
   `PairTradingConfig` 的字段表（剥注释 → 按 `;` 切 → 拆 `TYPE NAME`）；
2. 从 `pre_start` 的三个 `if (op.HasMember("<段>")) { ... }` 块里抓赋值语句，
   记录 `字段名 + 取值写法`；
3. 三项比对：
   - 字段名必须在结构体里；
   - 取值写法必须匹配字段声明类型：

     | 字段类型 | 唯一允许的写法 |
     |---|---|
     | `double` | `std::stod(s["x"].GetString())` |
     | `int64_t` | `std::stoll(...)` |
     | `int` | `std::stoi(...)` |
     | `bool` | `(s["x"].GetString() == std::string("true"))` |
     | `std::string` | `s["x"].GetString()`（如 `spreadStatsStatePath`） |
     | 枚举（`OrderType` / `stra::*`） | `GetString()` 后查表或字符串直比 |

   - 结构体的每个字段都要被读到。

### 豁免名单（两类，都不会误报）

- **平铺字段**：`pairKeys` / `activeAccountId` / `passiveAccountId` /
  `maxPositionValue` / `maxAmount` / `targetAmount` / `csvStatePath` 直接挂在 `op` 段下，
  不在三个块里，走 `std::stod` / `std::stoi` / `emplace_back`。
  名单在 `FLAT_FIELDS`。
- **子对象挂载点**：`PairTradingConfig` 里的 `feeSlippage` / `risk`
  两个字段是**子对象本身**，读取方式是 `auto& c = m_ptCfg.feeSlippage;` 而不是 `x = ...`。
  脚本自动识别"字段类型是个已知配置结构体"并豁免，以后再加子对象块不用改名单。

### 覆盖范围与局限

- ✅ 字段名、取值写法类型、字段覆盖率三项。
- ❌ **不检查数值/单位换算**（如秒 → 微秒的 `× 1000000`）。那是语义，静态看不出来；
  单位约定见 `PairTradingContext.h` 里 `PairTradingConfig` 的字段注释。
- ❌ 不检查 key 落在哪一段（与 `check.py` 同）。`quantileUp` 写进 `op.risk` 而不是
  `op.pairTrading` 看不出来，段的归属靠 `pre_start` 的 `if (op.HasMember("<段名>"))` 结构人工核对。
- ❌ 不检查 `SetConfig` 是否真的被调用（只看赋值语句）。
- ❌ 不检查值的合法性（运行期的事：写错会抛 `std::invalid_argument`，启动直接失败）。

> 结论：这两个脚本是**回归护栏**，不是正确性证明。字段级改动后跑一下，
> 能挡住"改配置改了个寂寞"这一类静默失效。

---

## 相关

- `../../docs/实盘接入与参数配置.md` §3 —— 参数清单与语义约定
- `../../src/strategy/PairTradingStrategy.cpp` 的 `pre_start` —— 唯一的解析点
- `../../quant_library/algo/PairTradingContext.h` —— `PairTradingConfig` 的字段名/单位约定
