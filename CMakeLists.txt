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
        cpp_redis 
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
