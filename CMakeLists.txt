cmake_minimum_required(VERSION 3.16)
project(dusktracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")

# Headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Sources
file(GLOB_RECURSE DUSK_SOURCES CONFIGURE_DEPENDS
    "src/core/main.cpp"
)

add_executable(dusktracer ${DUSK_SOURCES})

# OpenMP
find_package(OpenMP)
if (OpenMP_CXX_FOUND)
    target_link_libraries(dusktracer PRIVATE OpenMP::OpenMP_CXX)
endif()

# Assimp (Release)
set(ASSIMP_ROOT        "${CMAKE_SOURCE_DIR}/external/assimp")
set(ASSIMP_INCLUDE_DIR "${ASSIMP_ROOT}/include")
set(ASSIMP_LIB_RELEASE "${ASSIMP_ROOT}/lib/assimp-vc143-mt.lib")

target_include_directories(dusktracer PRIVATE ${ASSIMP_INCLUDE_DIR})
target_link_libraries(dusktracer PRIVATE ${ASSIMP_LIB_RELEASE})

# Copy DLL
add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ASSIMP_ROOT}/bin/assimp-vc143-mt.dll"
        "$<TARGET_FILE_DIR:dusktracer>"
)

# Copy models
add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:dusktracer>/models"
)

# Copy textures
add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/textures"
        "$<TARGET_FILE_DIR:dusktracer>/textures"
)
