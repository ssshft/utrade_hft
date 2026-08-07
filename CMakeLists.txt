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


option(USE_INFO_SHM "publish instrument into via share memory instead of redis" OFF)
if(USE_INFO_SHM)
    add_definitions(-DUSE_INFO_SHM)
    message(STATUS "USE_IFNO_SHM = ON (SHM writer enabled)")
else()
    message(STATUS "USE_IFNO_SHM = OFF (redis writer)")
endif()

# 读取整个文件
file(READ /inc/version.inc FILE_CONTENT)

# 查找 COMPILE_VERSION= 之后的内容
string(FIND "${FILE_CONTENT}" "COMPILE_VERSION=" COMPILE_POS)
if(COMPILE_POS GREATER -1)
    math(EXPR VALUE_START "${COMPILE_POS} + 16")  # "COMPILE_VERSION=" 的长度是 16
    string(SUBSTRING "${FILE_CONTENT}" ${VALUE_START} -1 COMPILE_VERSION)
    
    # 去除换行符
    string(REGEX REPLACE "\n.*$" "" COMPILE_VERSION "${COMPILE_VERSION}")
    message("COMPILE_VERSION = ${COMPILE_VERSION}")
endif()
set(COMPILE_PATH ${COMPILE_VERSION})

set(3RD_PATH /opt/version/3rd)

#include
set(INC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(QUANT_LIBRARY_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/quant_library)
set(BASIC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/quant_library/basic)
set(SYSTEM_INC_DIR          ${COMPILE_PATH}/include)
set(FMT_INCLUDE_DIR          ${3RD_PATH}/fmt/include)
set(DBP_INCLUDE_DIR        ${COMPILE_PATH}/dbp/sig)
set(SHMQUEUE_INC_DIR          ${COMPILE_PATH}/include/shmqueue)


#link
set(FMT_LIBRARY        ${3RD_PATH}/fmt/lib)



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
        ${3RD_PATH}
        ${INC_DIR}
        ${SYSTEM_INC_DIR}
        ${FMT_INCLUDE_DIR}
        ${DBP_INCLUDE_DIR}
        ${QUANT_LIBRARY_DIR}
        ${BASIC_DIR}
        ${SHMQUEUE_INC_DIR}
)

link_directories(
        ${FMT_LIBRARY}
)

link_libraries(
        pthread
        boost_system 
        crypto 
        ssl 
        cpprest 
        z 
        rt  
        dl 
        cpp_redis 
        tacopie 
        tbb 
        stdc++fs 
        fmt
        zmq
        uv
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
