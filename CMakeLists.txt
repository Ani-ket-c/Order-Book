cmake_minimum_required(VERSION 3.20)

project(LimitOrderBook VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_PROFILING "Enable LATENCY_PROFILING (Module 3)" OFF)

if(ENABLE_PROFILING)
    add_compile_definitions(LATENCY_PROFILING)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-march=native -O3 -flto)
        add_link_options(-flto)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<CONFIG:Debug>:/Od>)
    add_compile_options($<$<CONFIG:Debug>:/Zi>)
endif()

set(LOB_CORE_HEADERS
    Analytics/MicrostructureEngine.hpp
    Limit_Order_Book/Book.hpp
    Limit_Order_Book/Limit.hpp
    Limit_Order_Book/Order.hpp
    Limit_Order_Book/ObjectPool.hpp
    Process_Orders/OrderPipeline.hpp
    Generate_Orders/GenerateOrders.hpp
    Gateway/SPSCQueue.hpp
    Profiling/LatencyProfiler.hpp
    Protocol/OrderMessage.hpp
    Protocol/MessageEncoder.hpp
    Simulation/MarketSimulator.hpp
)

set(LOB_CORE_SOURCES
    Analytics/MicrostructureEngine.cpp
    Limit_Order_Book/Book.cpp
    Limit_Order_Book/Limit.cpp
    Limit_Order_Book/Order.cpp
    Process_Orders/OrderPipeline.cpp
    Generate_Orders/GenerateOrders.cpp
    Simulation/MarketSimulator.cpp
)

# Core LOB static library (gateway is header-only)
add_library(lob_core STATIC ${LOB_CORE_SOURCES} ${LOB_CORE_HEADERS})
target_include_directories(lob_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Analytics
        ${CMAKE_CURRENT_SOURCE_DIR}/Limit_Order_Book
        ${CMAKE_CURRENT_SOURCE_DIR}/Process_Orders
        ${CMAKE_CURRENT_SOURCE_DIR}/Generate_Orders
        ${CMAKE_CURRENT_SOURCE_DIR}/Gateway
        ${CMAKE_CURRENT_SOURCE_DIR}/Profiling
        ${CMAKE_CURRENT_SOURCE_DIR}/Protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/Simulation
)

# Backward-compatible alias for existing scripts
add_library(${PROJECT_NAME}_lib ALIAS lob_core)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE lob_core)

# Module 1: lock-free SPSC gateway smoke test (header-only; no LOB link required)
add_executable(lob_gateway_test Gateway/test_gateway.cpp)
target_include_directories(lob_gateway_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Gateway
)

# Module 3: HDR-histogram latency profiler smoke test (header-only)
add_executable(lob_profiler_test Profiling/test_profiler.cpp)
target_include_directories(lob_profiler_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# The test file #defines LATENCY_PROFILING itself, so no global define needed.

# Module 4: Binary wire protocol test
add_executable(lob_protocol_test Protocol/test_protocol.cpp)
target_link_libraries(lob_protocol_test PRIVATE lob_core)
target_include_directories(lob_protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Protocol
)

# Module 5: Market Simulator
add_executable(lob_sim Simulation/lob_sim.cpp Simulation/MarketSimulator.cpp)
target_link_libraries(lob_sim PRIVATE lob_core)
target_include_directories(lob_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Simulation
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
