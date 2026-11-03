#include <iostream>
#include <csignal>
#include <cstdio>
#include <vector>
#include "log_engine.h"
#include "program_util.h"
#include "config.h"
#include "crypto_exception.h"
#include "hft_metrics.h"
#include "base/base_strategy.h"
#include "tools/CReflectRegister.h"


std::vector<BaseStrategy*> strategyVec;
static void signal_handler(int signum) {
    LOG_INFO("handler interrupt signal ({}) received.", signum);
    for(auto str : strategyVec){
        str->pre_stop();
    }
    fmtlog::poll();
    sleep(0.5);//wait logger write data to disk
    exit(signum);
}

static void usage(void){
    fprintf(stderr, "\nusage:\n");
    fprintf(stderr, "./application json_config_file \n");
    fprintf(stderr, "\n");
    exit(-1);
}

int main(int argc, char* argv[]) {
    if ( argc != 2 ){
        usage();
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    Config* config = Config::instance();
    config->load(argv[1]);

    std::string tag = "";
    std::string logPath = "";
    std::string logLevel = "";

    if (config->get_string("tag", tag) && config->get_log_config("log_path", logPath) && config->get_log_config("level", logLevel)) {
        if (crypto::create_directory(logPath)) {
            log_maintain(tag, logPath, logLevel);
        }
        else {
            exit(-1);
        }
    }
    else {
        fprintf(stderr, "not found log config info in %s,\nneed to add log config\n", argv[1]);
        exit(-1);
    }

    std::string program = tag;
    long currentPid = getpid();
    long filePid = crypto::get_program_pid(program);
    if(!crypto::ensure_one_instance(program) && currentPid != filePid) {
        std::string errormsg = tag + " with pid=" + std::to_string(filePid) + " already exists, aborted";
        cryptothrow(errormsg.c_str(), -1);
    }
    crypto::write_program_pid(program);

    // 延时统计的线程分片/直方图要在**起工作线程之前**分配掉（heavy_work 由 start() 拉起）。
    // 编译期关掉时这个函数是空实现。
    hft::metrics::warmup();

    std::string opStr = "";
    if (config->get_op_config("strategyOp", opStr)) {
        auto obj = ClassNameCreateObj(opStr);
        if (nullptr != obj) {
            obj->pre_start(config);
            obj->start();
            strategyVec.push_back(obj);
        }
        else {
            cryptothrow("not found strategy:" + opStr + ", need to include your strategy files.", -1);
        }
    }

   // 统计落盘：文件名前缀故意用 "ut_metrics"，避免与同目录下 db 的 metrics_*.log、
   // tb 的 tb_metrics_*.log 撞名。写失败（目录没权限等）就退到 stderr ——
   // 统计失败不能把交易进程拖死。
   //
   // 周期 10 s：报告是给"事后看曲线"用的，1 s 一版只会把文件撑大；
   // 而 ut.exec.idle 这类计数器自己会累加，10 s 粒度不影响判断。
#if HFT_METRICS_ENABLED
    hft::metrics::TextFileSink metricsSink(logPath, "ut_metrics", 8u << 20);
    hft::metrics::Poller metricsPoller(
        10000000000LL, [&metricsSink](const hft::metrics::Snapshot& s) {
            if (!metricsSink.write(s)) {
                std::fputs(s.to_text().c_str(), stderr);
            }
        });
#else
    LOG_WARN("latency metrics disabled at compile time (-DHFT_METRICS=OFF); no ut_metrics file will be written. rebuild with -DHFT_METRICS=ON to enable");
#endif

   while(1) {
        log_maintain(tag, logPath, logLevel);
#if HFT_METRICS_ENABLED
        metricsPoller.poll();   // 0 参 = 自己读时钟
#endif
        usleep(1000);
    }

    return 0;
}