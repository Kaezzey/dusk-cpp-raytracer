cmake_minimum_required(VERSION 3.16)
project(dusktracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Project headers
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(dusktracer_core STATIC
    main.cpp
)

# Single offline executable
add_executable(dusktracer_offline
    src/core/main.cpp
)