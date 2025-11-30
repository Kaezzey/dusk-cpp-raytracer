cmake_minimum_required(VERSION 3.16)
project(dusktracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")

# Headers (your core engine)
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# ImGui (from external/imgui)
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp          # optional but useful while developing
    ${IMGUI_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
)

# ---------------------------------------------------------------------------
# GLFW: headers + prebuilt libs in include/external
# ---------------------------------------------------------------------------
# Layout:
#   include/external/GLFW/glfw3.h
#   include/external/GLFW/glfw3native.h
#   include/external/glfw3.lib
#   include/external/glfw3.dll
set(GLFW_ROOT        "${CMAKE_SOURCE_DIR}/include/external")
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}")
set(GLFW_LIB_PATH    "${GLFW_ROOT}/glfw3.lib")   # use glfw3_mt.lib if you prefer

# Make sure everyone can see <GLFW/glfw3.h>
include_directories(${GLFW_INCLUDE_DIR})
target_include_directories(imgui PUBLIC ${GLFW_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# GLEW: headers + prebuilt libs in external/glew
# ---------------------------------------------------------------------------
# Expected layout:
#   external/glew/include/GL/glew.h
#   external/glew/lib/Release/x64/glew32.lib
#   external/glew/bin/glew32.dll
set(GLEW_ROOT         "${CMAKE_SOURCE_DIR}/external/glew")
set(GLEW_INCLUDE_DIR  "${GLEW_ROOT}/include")
set(GLEW_LIB_PATH     "${GLEW_ROOT}/lib/Release/x64/glew32.lib")

include_directories(${GLEW_INCLUDE_DIR})
target_include_directories(imgui PUBLIC ${GLEW_INCLUDE_DIR})

# ---------------------------------------------------------------------------
# Core sources (everything except the two mains)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE DUSK_SOURCES CONFIGURE_DEPENDS
    "src/core/*.cpp"
)

# Remove main.cpp and editor_main.cpp from the shared source list
list(REMOVE_ITEM DUSK_SOURCES
    "${CMAKE_SOURCE_DIR}/src/core/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/editor_main.cpp"
)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------

# CLI / main renderer
add_executable(dusktracer
    src/core/main.cpp
    ${DUSK_SOURCES}
)

# Editor executable
add_executable(dusktracer_editor
    src/core/editor_main.cpp
    ${DUSK_SOURCES}
)

# ---------------------------------------------------------------------------
# Windows icon resource (for EXE/taskbar/titlebar)
# ---------------------------------------------------------------------------
if (WIN32)
    # Single RC used by both targets. Path is relative to CMakeLists.txt.
    set(APP_ICON_RC "${CMAKE_SOURCE_DIR}/appicon.rc")

    target_sources(dusktracer        PRIVATE ${APP_ICON_RC})
    target_sources(dusktracer_editor PRIVATE ${APP_ICON_RC})
endif()

# ---------------------------------------------------------------------------
# OpenMP
# ---------------------------------------------------------------------------
find_package(OpenMP)
if (OpenMP_CXX_FOUND)
    target_link_libraries(dusktracer        PRIVATE OpenMP::OpenMP_CXX)
    target_link_libraries(dusktracer_editor PRIVATE OpenMP::OpenMP_CXX)
endif()

# ---------------------------------------------------------------------------
# Assimp (Release)
# ---------------------------------------------------------------------------
set(ASSIMP_ROOT        "${CMAKE_SOURCE_DIR}/external/assimp")
set(ASSIMP_INCLUDE_DIR "${ASSIMP_ROOT}/include")
set(ASSIMP_LIB_RELEASE "${ASSIMP_ROOT}/lib/assimp-vc143-mt.lib")

target_include_directories(dusktracer        PRIVATE ${ASSIMP_INCLUDE_DIR})
target_include_directories(dusktracer_editor PRIVATE ${ASSIMP_INCLUDE_DIR})

target_link_libraries(dusktracer        PRIVATE ${ASSIMP_LIB_RELEASE})
target_link_libraries(dusktracer_editor PRIVATE ${ASSIMP_LIB_RELEASE})

# ---------------------------------------------------------------------------
# OpenGL + link ImGui + GLFW + GLEW into the editor
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

target_link_libraries(dusktracer_editor
    PRIVATE
        imgui
        ${GLFW_LIB_PATH}
        ${GLEW_LIB_PATH}
        OpenGL::GL
)

# ---------------------------------------------------------------------------
# Post-build: copy DLL/models/textures for main renderer
# ---------------------------------------------------------------------------
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

# Copy application icon PNG next to main exe so runtime can find it by relative path
add_custom_command(TARGET dusktracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/dusktracer.png"
        "$<TARGET_FILE_DIR:dusktracer>"
)

# Copy glfw3.dll next to the editor exe (if using dynamic GLFW)
add_custom_command(TARGET dusktracer_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLFW_ROOT}/glfw3.dll"
        "$<TARGET_FILE_DIR:dusktracer_editor>"
)

# Copy glew32.dll next to the editor exe
add_custom_command(TARGET dusktracer_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLEW_ROOT}/bin/glew32.dll"
        "$<TARGET_FILE_DIR:dusktracer_editor>"
)

# Copy application icon PNG next to editor exe so runtime can find it by relative path
add_custom_command(TARGET dusktracer_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/dusktracer.png"
        "$<TARGET_FILE_DIR:dusktracer_editor>"
)
