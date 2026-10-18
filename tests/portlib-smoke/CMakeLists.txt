cmake_minimum_required(VERSION 3.25)
project(CarbonPortlibSmoke LANGUAGES C CXX)
add_executable(portlib-smoke main.cpp
    ../../reference-nfsmw-nx/sdk/src/core/switch_entropy.c)
target_link_libraries(portlib-smoke PRIVATE expat z zstd nx m)
