cmake_minimum_required(VERSION 3.20)
project(windbg_mcp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable fastmcpp tests/examples - we only need the core library
set(FASTMCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FASTMCPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTMCPP_FETCH_CURL OFF CACHE BOOL "" FORCE)

add_subdirectory(fastmcpp)

add_library(windbg_mcp SHARED
    src/output_callbacks.cpp
    src/dbg_engine.cpp
    src/mcp_server.cpp
    src/extension.cpp
)

target_include_directories(windbg_mcp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link fastmcpp core library
target_link_libraries(windbg_mcp PRIVATE fastmcpp_core)

# Link Windows debugger engine and system libraries
target_link_libraries(windbg_mcp PRIVATE
    dbgeng.lib
    ws2_32.lib
)

# Module definition file for exported symbols
set_target_properties(windbg_mcp PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/extension.def"
    OUTPUT_NAME "windbg-mcp"
)

if(MSVC)
    target_compile_options(windbg_mcp PRIVATE /FS /W3)
endif()

# STDIO proxy EXE - launched by Claude Desktop to bridge STDIO <-> HTTP
add_executable(windbg-mcp-proxy src/stdio_proxy.cpp)
target_link_libraries(windbg-mcp-proxy PRIVATE fastmcpp_core ws2_32.lib)
if(MSVC)
    target_compile_options(windbg-mcp-proxy PRIVATE /FS /W3)
endif()
