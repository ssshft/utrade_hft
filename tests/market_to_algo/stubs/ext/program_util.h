// 逐字拷贝自 include/program_util.h。
//
// 为什么不直接软链真实的那个：真实文件在 `hft/include/`（**本仓库之外的另一个 git 仓库**），
// 套件要能拷到 Ubuntu 服务器上单独跑，不能依赖同级目录的布局。
//
// 为什么不做成空桩：这个文件是**完全自足**的（只用 std + POSIX），没有任何外部依赖，
// 没有理由不照抄。而且它是纯工具函数（读文件 / 建目录 / 进程存活判断），
// 语义上不存在"少实现"的余地 —— 要么真的做，要么别要。
//
// 唯一一处改动：set_cpu 用了 cpu_set_t / pthread_setaffinity_np，那是 glibc 专有，
// macOS 上没有，所以用 __linux__ 包起来。本套件不调用它，包起来不影响任何断言。
#ifndef INCLUDE_PROGRAM_UTIL_H
#define INCLUDE_PROGRAM_UTIL_H
#include <stdio.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <thread>
#include <pthread.h>
#include <sys/types.h>
#include <iostream>
#include <filesystem>


namespace fs = std::filesystem;

namespace crypto {

    inline bool is_file_existed (const std::string& name) {
        struct stat buffer;
        return (stat (name.c_str(), &buffer) == 0);
    }

    inline std::string read_file(const char* filePath) {
        std::ifstream t(filePath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        std::string contents(buffer.str());
        return contents;
    }

#ifdef __linux__
    inline int set_cpu(std::thread &th, int i){
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        int rc = pthread_setaffinity_np(th.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
            return -1;
        }
        // cpu_set_t mask;
        // CPU_ZERO(&mask);
        // CPU_SET(i,&mask);
        // // printf("thread %u, i = %d\n",pthread_self(),i);
        // if(-1 == pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask))
        return 0;
    }
#else
    inline int set_cpu(std::thread &, int) { return 0; }
#endif

    inline bool is_process_exist(long pid){
        struct stat sts;
        std::string path = "/proc/" + std::to_string((long long)pid);
        if (stat(path.c_str(), &sts) == -1 && errno == ENOENT) {
          return false;
        }
        return true;
    }

    inline long get_program_pid(const std::string program) {
        long pid = 0 ;
        std::string filename = std::string("/run/") + program + std::string(".pid");
        if ( !is_file_existed(filename) )
            return pid;
        std::ifstream iffile;
        iffile.open( filename );
        if ( iffile.fail() )
            throw std::runtime_error("get_program_pid failed: "   );
        iffile >> pid;
        iffile.close();
        return pid;
    }

    inline bool ensure_one_instance(const std::string program){
        auto pid = get_program_pid(program);
        return (!is_process_exist(pid));
    }

    inline int write_program_pid(const std::string program)
    {
        pid_t pid = getpid();
        std::ofstream iffile;
        iffile.open( std::string("/run/") + program + std::string(".pid") );
        if ( iffile.fail() )
            throw std::runtime_error("write_program_pid failed: "   );
        iffile << pid;
        iffile.close();
        return 0;
    }

    inline bool create_directory(const std::string& path) {
        bool created = false;
        try {
            // 创建所有不存在的目录
            fs::create_directories(path);
            created = true;
        } catch (const fs::filesystem_error& e) {
            fprintf(stderr, "Failed to create log directory %s: %s\n", path.c_str(), e.what());
        }
        return created;
    }

}
#endif //INCLUDE_PROGRAM_UTIL_H