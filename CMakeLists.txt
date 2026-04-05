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


file(READ /inc/version.inc COMPILE_VERSION LIMIT 8 OFFSET 33)
message(${COMPILE_VERSION})
set(COMPILE_PATH ${COMPILE_VERSION})

set(3RD_PATH /opt/version/3rd)

#include
set(INC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(QUANT_LIBRARY_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/quant_library)
set(BASIC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/quant_library/basic)
set(SYSTEM_INC_DIR          ${COMPILE_PATH}/include)
set(FMT_INCLUDE_DIR          ${3RD_PATH}/fmt/include)
set(DBP_INCLUDE_DIR        ${COMPILE_PATH}/dbp/sig)


#link
set(FMT_LIBRARY        ${3RD_PATH}/fmt/lib)



aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src        DIR_MAIN_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/strategy   DIR_STRATEGY_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/tools     DIR_TOOLS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/algo     DIR_ALGO_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/quant_library/basic     DIR_BASIC_SRCS)



include_directories(
        ${3RD_PATH}
        ${INC_DIR}
        ${SYSTEM_INC_DIR}
        ${FMT_INCLUDE_DIR}
        ${DBP_INCLUDE_DIR}
        ${QUANT_LIBRARY_DIR}
        ${BASIC_DIR}
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
)
