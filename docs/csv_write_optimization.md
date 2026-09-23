# CSV 写入优化方案：按文件缓存 ofstream + 定期 flush

> 现状是「写一条 = 一次 `access` + 一次 `open` + 一次 `write` + 一次 `close`」。
> 本方案把它降到「写一条 ≈ 一次内存拼接」，同时把持久化语义讲清楚。
> 本文档只给方案，**不改任何源代码**。
> 配合 `docs/writefilecontent_refactor.md`（把 `fmt::format` 搬到写线程）一起看。

---

## 1. 现状：一条记录 = 4 次系统调用

5 个写函数（`WriteFileContent.h:15 / 39 / 63 / 86 / 109`）的结构完全一样，以 `WriteQuantOrderToFile` 为例：

```cpp
void WriteQuantOrderToFile(const string& s) {
    string dateStr = CovertToUtcDate(GetCurrentTimeUs());     // ① gmtime + strftime + string 构造
    string quantOrderPath = dateStr + "_quantOrder.csv";      // ② string 拼接（又一次分配）

    ofstream f;                                               // ③ 构造
    if (access(quantOrderPath.c_str(), F_OK) != 0) {          // ④ ★ access 系统调用
        f.open(quantOrderPath.c_str(), ios::app);             // ⑤ ★ open 系统调用 + 缓冲区分配
        f << "strategyName,...\n";                            // ⑥ 表头
        f << s << "\n";
    } else {
        f.open(quantOrderPath.c_str(), ios::app);             // ⑤ ★ open 系统调用
        f << s << "\n";
    }
    f.close();                                                // ⑦ ★ write + close 两个系统调用
}
```

单条记录的代价：

| 项 | 次数 | 量级 |
|---|---|---|
| `access()` | 1 | ~1~2µs |
| `open()` | 1 | ~2~5µs（含文件缓冲区分配） |
| `write()`（close 时刷出） | 1 | ~2~5µs |
| `close()` | 1 | ~1~2µs |
| `gmtime` + `strftime` + 2 次 `string` 分配 | 1 组 | ~1~2µs |
| **合计** | | **~8~20µs / 条** |

而一次报单会写 2 条（1 条 pairOrder + 1 条 quantOrder）→ **单次报单在文件 IO 上花 16~40µs**。
这个量级和 `fmt::format` 相当，甚至更大。**所以你的判断是对的：这块和格式化一样值得改。**

---

## 2. 先澄清一件事：「写一条保存一下」并没有提供你以为的持久化

`ofstream::close()` **不等于** `fsync()`。它做的事情只是「把进程缓冲区里的数据交给内核」，
数据落在 **page cache** 里，还没有落盘。所以：

| 场景 | 现在（每条 close） | 改成缓冲后 |
|---|---|---|
| 进程崩溃 / segfault | 数据**在**（内核已持有） | 最多丢一个缓冲区 |
| 机器掉电 / 内核崩溃 | **一样丢** | 一样丢 |
| 真要抗掉电 | 需要 `fsync` / `fdatasync`（比 write 贵 10~100 倍）或 `O_SYNC` | 同左 |

也就是说，现在这套「每条 open/close」换来的唯一好处是
**「进程崩溃时最多丢 0 条」**，代价是每条 4 次系统调用。而全仓 grep 确认：
**没有任何地方调用 `fsync` / `fdatasync` / `.flush()`**，所以它本来就不是奔着落盘去的。

→ 结论：只要接受「进程崩溃时最多丢 N 条」，就可以把系统调用降到 0。
这个 N 是可以自己定的（见 §3.6）。

---

## 3. 方案：按文件缓存 `ofstream`，定期 flush

### 3.1 结构

`content.type` 只有 5 个取值（1~5），文件路径只由「日期 + 类型」决定，
所以天然可以缓存 5 个 sink：

```cpp
class WriteFileContent {
private:
    struct FileSink {
        std::ofstream f;
        std::string   path;
        std::vector<char> buf;          // ★ 必须由我们自己持有，见 §3.3
        bool          opened{false};
        bool          headerWritten{false};
        int64_t       lastFlushUs{0};
    };

    FileSink    mSinks[6];              // 下标 = content.type（1~5）
    std::string mCurDate;               // 当前文件日期，只有跨天才重算
    static constexpr size_t kBufSize       = 256 * 1024;   // 256KB
    static constexpr int64_t kFlushIntervalUs = 200 * 1000; // 200ms
};
```

### 3.2 写一条记录的流程

```cpp
void Write(const content& c) {
    // ① 用「记录自身的时间」选文件（不是写线程的当前时刻，见 §3.5）
    std::string date = CovertToUtcDate(RecordTimeUs(c));
    if (date != mCurDate) {
        RotateAll(date);                       // 关掉全部 sink、清 headerWritten
    }

    // ② 取/建对应的 sink
    FileSink& sink = mSinks[c.type];
    if (!sink.opened) {
        OpenSink(sink, c.type, date);
    }

    // ③ 表头只写一次
    if (!sink.headerWritten) {
        sink.f << HeaderOf(c.type);
        sink.headerWritten = true;
    }

    // ④ 写内容（只进内存缓冲区）
    sink.f << Format(c) << "\n";

    // ⑤ 定期 flush，而不是每条 close
    int64_t now = crypto::getCurrentTime();
    if (now - sink.lastFlushUs > kFlushIntervalUs) {
        sink.f.flush();
        sink.lastFlushUs = now;
    }
}
```

### 3.3 `OpenSink` 的两个要点

```cpp
void OpenSink(FileSink& sink, int type, const std::string& date) {
    sink.path = date + SuffixOf(type);                 // "_quantOrder.csv" 等

    // ★ pubsetbuf 必须在 open() 之前调用，之后调用无效（常见坑）
    sink.buf.resize(kBufSize);
    sink.f.rdbuf()->pubsetbuf(sink.buf.data(), sink.buf.size());

    sink.f.open(sink.path.c_str(), std::ios::app | std::ios::binary);
    sink.opened = true;

    // ★ 用文件大小判断要不要写表头，替掉每条的 access()
    struct stat st;
    sink.headerWritten = (::stat(sink.path.c_str(), &st) == 0 && st.st_size > 0);
}
```

**为什么必须显式 `pubsetbuf`：**
`ofstream` 的默认缓冲区大小是**实现相关的**——libstdc++ 是 8192 字节，
**libc++ 是 `BUFSIZ`，也就是 1024 字节**。
1024 字节意味着每写几条就触发一次 `write` 系统调用。
当前是 macOS + clang（大概率 libc++），所以这一条不是「可选优化」而是**必须做的**。
（另外 `pubsetbuf` 只是告诉 `basic_filebuf` 用哪块内存，**不接管所有权**，
所以 `buf` 必须是成员变量，不能是局部数组。）

**为什么用 `::stat` 而不是 `std::filesystem::file_size`：**
`std::filesystem` 在 GCC 9 以下需要额外链接 `-lstdc++fs`，用 POSIX 的 `stat` 零依赖。

### 3.4 表头判断顺带修掉一个隐患

现在的逻辑是 `if (access(path, F_OK) != 0) { 写表头 }`——
**只判断「文件不存在」，不判断「文件是空的」**。
如果上一次运行在创建文件之后、写完表头之前崩了，会留下一个 0 字节文件，
下次启动就会**跳过表头直接写数据**，导致整列错位。
改成 `st_size > 0` 就没有这个问题。

### 3.5 跨天轮转：用记录自身的时间

两个好处：

1. **正确性**：现在用的是 `GetCurrentTimeUs()`（写线程的当前时刻）。
   如果队列有积压、正好跨零点，零点前产生的记录会被写进**第二天**的文件。
   改用记录自身的时间（`QuantOrderRecord::updateTime` / `PairOrderRecord::createTime`，
   它们都来自 `crypto::getCurrentTime()`，是墙钟）就不会错。
2. **顺手绕开一个可移植性坑**：`GetCurrentTimeUs()`（`Utility.h:30-32`）用的是
   `high_resolution_clock`。在 **libc++ 下它是 `steady_clock`**（开机以来的单调时钟，不是墙钟），
   `CovertToUtcDate` 会算出完全错误的日期；libstdc++ 下它是 `system_clock`，所以现在没事。
   换编译器/换平台时这里会静默出错。

另外 `CovertToUtcDate`（`Utility.h:61-72`）内部用的是**非可重入的 `gmtime`**，
建议换成 `gmtime_r`（`include/log_engine.h:20` 用的就是 `localtime_r`，是正确写法）。

### 3.6 flush 策略：持久化与吞吐的取舍

| 策略 | 进程崩溃最多丢 | 每条的系统调用 | 说明 |
|---|---|---|---|
| 现在：每条 `close()` | 0 条 | 4 次 | 抗掉电依然不行 |
| 纯缓冲，从不 flush | 一个缓冲区（256KB ≈ 数千条） | 0 次 | 崩溃代价太大 |
| **推荐：定期 flush** | 一个 flush 间隔的量 | 0 次（间隔触发 1 次） | 见下 |
| 每条 `fsync` | 0 条（抗掉电） | 4 次 + `fsync` | 慢 10~100 倍，不适合热路径 |

推荐做法：
- `kFlushIntervalUs = 200 * 1000`（200ms）——进程崩溃最多丢 200ms 的记录
- 另外在**关键事件后主动 flush**：算法单终结（`ALGO_OS_FILLED` / `CANCELED`）、
  配对单完结。这些时刻的落库最值得保，而它们本身是低频的。
  实现上可以给 `content` 加一个 `bool urgent` 字段，或者按 `type` 区分。

### 3.7 退出时必须收尾（这是现有的 bug，缓存流之后会被放大）

已核实：

- **`running` 全仓没有任何地方被置为 `false`**（grep 无结果）
- `~WriteFileContent() {}`（`WriteFileContent.h:13`）是空的，**线程从未 join**
- 线程在 `AlgoContext.cpp:45` 的 `WriteFileContent::GetInstance()` 里启动，然后一直跑到 `exit()`
- `src/main.cpp:13-21` 的 `signal_handler` 只调 `pre_stop()` 就 `exit()`；
  `exit()` 会执行静态析构，而写线程还在循环里访问全局的 `contentQueue`
  （定义在 `BaseAlgoOrder.cpp:11`）→ **可能访问已析构对象**

这个隐患现在就有（概率低，因为每条记录的生命周期很短），
但**改成缓存流之后，未 flush 的缓冲区会在静态析构时丢失**，问题会变明显。

改法：

```cpp
void Stop() {                      // 新增
    running = false;               // running 要改成 std::atomic<bool>
    if (runningThread && runningThread->joinable()) {
        runningThread->join();
    }
    for (FileSink& s : mSinks) {
        if (s.opened) { s.f.flush(); s.f.close(); s.opened = false; }
    }
}
```

在 `PairTradingStrategy::pre_stop()`（`PairTradingStrategy.cpp:71-76`，现在已经在那里存 CSV 快照）
里调用 `WriteFileContent::GetInstance().Stop();`。

> 顺带一提：`LarkRebot`（`LarkRebot.cpp:17`）是完全一样的模式——`running` 从不置 false、
> 析构空、线程不 join。要不要一起处理由你定。

---

## 4. 收益估算

| | 每条记录 | 相对 |
|---|---|---|
| 现在 | ~4 次系统调用 + ~3 次分配 + gmtime ≈ **8~20µs** | 1× |
| 改后 | 1 次 `string` 拼接 + 1 次 `f <<` ≈ **0.3~1µs**（每 256KB 才一次 write） | **~20~50×** |

折算到报单：单次报单从 16~40µs 降到 1~2µs。

---

## 5. 改动清单

| 文件 | 改动 | 量级 |
|---|---|---|
| `quant_library/basic/WriteFileContent.h` | 新增 `FileSink` / `mSinks` / `OpenSink` / `RotateAll` / `HeaderOf` / `SuffixOf` / `Write`；5 个 `WriteXxxToFile` 收敛成「表头 + 后缀 + 写」三份常量；新增 `Stop()`；`running` 改 `std::atomic<bool>` | 重构，净减约 100 行 |
| `quant_library/basic/Utility.h` | `CovertToUtcDate` 的 `gmtime` → `gmtime_r` | 1 行 |
| `src/strategy/PairTradingStrategy.cpp` | `pre_stop()` 里调 `WriteFileContent::GetInstance().Stop()` | 2 行 |
| `WriteFileContent.h` 的 `Run()` | 排空队列 + 非空 catch + 只在空时 sleep（见 `docs/writefilecontent_refactor.md` §4） | ~10 行 |
| 所有调用点 | **不动** | 0 |

---

## 6. 风险与注意点

### R1（中）崩溃丢数据窗口变大
从「丢 0 条」变成「丢最多 200ms 的量」。这是**有意取舍**，用 §3.6 的定期 flush + 关键事件 flush 控制。
如果你要求「一笔成交都不能丢」，那就保留每条 flush（用 `flush()` 而不是 `close()`，
仍然能省掉 `access` + `open` + `close` 三次系统调用，只留一次 `write`）。

### R2（中）轮转必须在写之前判断
如果先写再判断日期，跨天的那一刻会有一条记录落到旧文件。
顺序必须是：算日期 → 需要轮转就轮转 → 再写。

### R3（中）`pubsetbuf` 必须在 `open()` 之前
之后调用无效，而且不会报错——是个静默失效的坑。缓冲区内存必须由我们自己持有（成员变量）。

### R4（低）表头判断改用文件大小后，首次创建文件时 `stat` 会失败
要用返回值为 0 且 `st_size > 0` 双重判断（见 §3.3 的写法），失败按「无内容」处理。

### R5（低）写失败现在是静默的
`ofstream` 写失败只会置 `badbit`，不会抛异常（除非开了 `exceptions()`）。
磁盘满 / 权限变更 / 文件被删都会静默丢数据。
建议定期检查 `sink.f.good()`，异常时通过 `rLarkMsg` 报警。
缓存流之后「文件被删」不再会像现在这样每条 open 都失败一次，问题会更隐蔽。

### R6（低）单实例保证
`src/main.cpp:61` 有 `crypto::ensure_one_instance`，所以不会有第二个进程写同一个文件，
不需要考虑 `O_APPEND` 的跨进程原子性。

### R7（低）不要为每个文件开线程
缓存流之后瓶颈已经不是系统调用而是内存拷贝，单线程足够。
开 5 个写线程只会增加复杂度和文件句柄竞争。

---

## 7. 落地顺序

1. **先量**：确认单条 `WriteXxxToFile` 的真实耗时（可以临时打点，或者用 `dtruss` / `strace -c` 数系统调用）。
2. **阶段一（收益最大）**：按文件缓存 `ofstream` + `pubsetbuf(256KB)` + 表头判断改 `stat`。
   这一步就能把 4 次系统调用降到 0。
3. **阶段二**：定期 flush + 关键事件 flush，确定可接受的丢数据窗口。
4. **阶段三**：`Stop()` + `join` + 退出前 flush，修掉现有的退出竞态。
5. 配合 `docs/writefilecontent_refactor.md`：`Run()` 排空队列 + 非空 catch。
   注意两份改动要一起上——只搬格式化不改缓冲，写线程会变成新瓶颈；
   只改缓冲不搬格式化，行情线程还是在替写线程干活。
