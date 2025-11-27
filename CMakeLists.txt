cmake_minimum_required(VERSION 3.16)
project(dusktracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")

# Headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# Core sources (everything except the two mains)
file(GLOB_RECURSE DUSK_SOURCES CONFIGURE_DEPENDS
    "src/core/*.cpp"
)

# Remove main.cpp and editor_main.cpp from the shared source list
list(REMOVE_ITEM DUSK_SOURCES
    "${CMAKE_SOURCE_DIR}/src/core/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/editor_main.cpp"
)

# -----------------------------------------------------------------------------
# Executables
# -----------------------------------------------------------------------------

# CLI / main renderer
add_executable(dusktracer
    src/core/main.cpp
    ${DUSK_SOURCES}
)

# Editor test harness (Phase 2)
add_executable(dusktracer_editor
    src/core/editor_main.cpp
    ${DUSK_SOURCES}
)

# -----------------------------------------------------------------------------
# OpenMP
# -----------------------------------------------------------------------------
find_package(OpenMP)
if (OpenMP_CXX_FOUND)
    target_link_libraries(dusktracer        PRIVATE OpenMP::OpenMP_CXX)
    target_link_libraries(dusktracer_editor PRIVATE OpenMP::OpenMP_CXX)
endif()

# -----------------------------------------------------------------------------
# Assimp (Release)
# -----------------------------------------------------------------------------
set(ASSIMP_ROOT        "${CMAKE_SOURCE_DIR}/external/assimp")
set(ASSIMP_INCLUDE_DIR "${ASSIMP_ROOT}/include")
set(ASSIMP_LIB_RELEASE "${ASSIMP_ROOT}/lib/assimp-vc143-mt.lib")

target_include_directories(dusktracer        PRIVATE ${ASSIMP_INCLUDE_DIR})
target_include_directories(dusktracer_editor PRIVATE ${ASSIMP_INCLUDE_DIR})

target_link_libraries(dusktracer        PRIVATE ${ASSIMP_LIB_RELEASE})
target_link_libraries(dusktracer_editor PRIVATE ${ASSIMP_LIB_RELEASE})

# -----------------------------------------------------------------------------
# Post-build: copy DLL/models/textures for main renderer
# -----------------------------------------------------------------------------
add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ASSIMP_ROOT}/bin/assimp-vc143-mt.dll"
        "$<TARGET_FILE_DIR:dusktracer>"
)

add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:dusktracer>/models"
)

add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/textures"
        "$<TARGET_FILE_DIR:dusktracer>/textures"
)
