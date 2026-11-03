cmake_minimum_required(VERSION 3.1)
project(utrade_hft)

# 设置编译器 c++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -fPIC -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -fPIC -DABI -DNDEBUG -DOPENBLAS  -O3 -Wall -flto -fuse-linker-plugin -minline-all-stringops -malign-double -ffast-math -march=native -ffunction-sections -fdata-sections -fmessage-length=0 -msse -msse2 -msse3 -funroll-loops")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)


add_definitions("-D_TURN_OFF_PLATFORM_STRING")
# add_definitions("-DUSE_BINANCE_UNIFIED")


add_definitions(-DUSE_INFO_SHM)

# 延时统计（../include/hft_metrics.h）的编译期开关。**默认关**。
#   cmake .. && make            → 关（宏展开成空，热路径零残留、不建 metrics 文件）
#   cmake -DHFT_METRICS=ON ..   → 开
#
# 这里两种速率差得很远，但都用 HFT_SCOPE 全量计时、不采样：
#   · 回报/余额/仓位（tb 推过来的）：每秒几次；
#   · 价差推送（dbp 每个对子一条）：500 对子 × 几 Hz ≈ 每秒几千次。
# 采样是为了保护"每秒几百万次"的热路径，这里没有这种路径；而 1/16 采样在
# 每秒几次的那条路上等于没有样本（10 s 窗口只剩几个点，p99 直接等于 max）。
# 全量计时的边际开销是恒定的 +15~20 ns：对 µs 级的价差处理是 1%~3%，
# 5000 次/秒折合单核 0.02%。
option(HFT_METRICS "编译延时统计埋点（hft_metrics.h）" OFF)
if(HFT_METRICS)
    add_definitions(-DHFT_METRICS_ENABLED=1)
else()
    add_definitions(-DHFT_METRICS_ENABLED=0)
endif()

#include
set(INC_DIR                ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(QUANT_LIBRARY_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/quant_library)
set(BASIC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/quant_library/basic)

set(SYSTEM_INC_DIR            ../include)
set(DBP_INCLUDE_DIR           ../dbp/sig)
set(SHMQUEUE_INC_DIR          ../include/shmqueue)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src        DIR_MAIN_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/strategy   DIR_STRATEGY_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/tools     DIR_TOOLS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/algo     DIR_ALGO_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/basic     DIR_BASIC_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/risk     DIR_RISK_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/signal    DIR_SIGNAL_SRCS)
aux_source_directory(${SHMQUEUE_INC_DIR}                   DIR_SHMQUEUE_SRCS)
aux_source_directory(${DBP_INCLUDE_DIR}/src                   DIR_DBP_SRCS)

include_directories(
        ${INC_DIR}
        ${SYSTEM_INC_DIR}
        ${DBP_INCLUDE_DIR}
        ${QUANT_LIBRARY_DIR}
        ${BASIC_DIR}
        ${SHMQUEUE_INC_DIR}
)


link_libraries(
        pthread
        boost_system 
        crypto 
        ssl 
        z 
        rt  
        dl 
        tacopie 
        tbb 
        stdc++fs 
        fmt
)

add_executable(utrade_hft
        ${DIR_MAIN_SRCS}
        ${DIR_STRATEGY_SRCS}
        ${DIR_TOOLS_SRCS}
        ${DIR_ALGO_SRCS}
        ${DIR_BASIC_SRCS}
        ${DIR_RISK_SRCS}
        ${DIR_SIGNAL_SRCS}
        ${DIR_SHMQUEUE_SRCS}
        ${DIR_DBP_SRCS}
)
