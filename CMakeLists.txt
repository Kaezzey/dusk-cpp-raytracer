cmake_minimum_required(VERSION 3.16)
project(dusktracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- Sources ----
# Grab all .cpp files under src/core
file(GLOB_RECURSE DUSK_SOURCES CONFIGURE_DEPENDS
    "src/core/main.cpp"
)

# Single offline executable
add_executable(dusktracer
    ${DUSK_SOURCES}
)

# ---- OpenMP ----
find_package(OpenMP)
if (OpenMP_CXX_FOUND)
    target_link_libraries(dusktracer PRIVATE OpenMP::OpenMP_CXX)
endif()
