cmake_minimum_required(VERSION 3.21)
project(ASTRA_NativeRenderer VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(ASTRA_ENABLE_VULKAN "Enable Vulkan 1.3 backend" ON)
option(ASTRA_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(ASTRA_ENABLE_TRACY "Enable Tracy profiler" OFF)
option(ASTRA_BUILD_TESTS "Build tests" OFF)
option(ASTRA_BUILD_TOOLS "Build tools" OFF)

# ─── Vulkan SDK discovery (portable — no machine-specific paths) ──────────────
# The LunarG installer sets VULKAN_SDK system-wide and registers the loader;
# CMake's FindVulkan consumes both. Never hardcode SDK locations here.
if(ASTRA_ENABLE_VULKAN)
    find_package(Vulkan)
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
            "Vulkan SDK not found.\n"
            "  Install the LunarG Vulkan SDK (https://vulkan.lunarg.com/sdk/home#windows)\n"
            "  or set the VULKAN_SDK environment variable to an existing SDK root, then\n"
            "  re-run CMake in a fresh build directory.")
    endif()
    message(STATUS "Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
endif()

# GLFW / SDL3 — optional, via pkg-config where available
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GLFW glfw3)
    pkg_check_modules(SDL3 sdl3)
endif()

# Optional header-only dependencies — discovered portably.
# Each may be supplied via an environment variable or CMake -D hint.
find_path(ENTT_INCLUDE_DIR
    NAMES entt/entt.hpp
    HINTS ENV ENTT_DIR
    PATH_SUFFIXES src include)
if(NOT ENTT_INCLUDE_DIR)
    message(STATUS "EnTT not found (set ENTT_DIR) — using minimal internal ECS fallback")
endif()

find_path(IMGUI_DIR
    NAMES imgui.h
    HINTS ENV IMGUI_DIR)
if(NOT IMGUI_DIR)
    message(STATUS "Dear ImGui not found (set IMGUI_DIR) — debug UI disabled")
endif()

find_path(MINIAUDIO_INCLUDE_DIR
    NAMES miniaudio.h
    HINTS ENV MINIAUDIO_DIR)
if(NOT MINIAUDIO_INCLUDE_DIR)
    message(STATUS "miniaudio not found (set MINIAUDIO_DIR) — audio headers disabled")
endif()

# Compiler flags for AAA quality: strict, Werror-ish, optimization
if(MSVC)
    # Windows bootstrap builds the existing native target, not the legacy launcher.
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native -flto)
        add_link_options(-flto)
    else()
        add_compile_options(-O1 -g -DDEBUG)
    endif()
endif()

# Core library: astra_renderer
file(GLOB_RECURSE RENDERER_SOURCES
    src/*.cpp
)
# Entry points are executables, not library code — keeping them in the static
# library would duplicate main()/WinMain into every consumer.
list(REMOVE_ITEM RENDERER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main_production.cpp"
)

add_library(astra_renderer STATIC ${RENDERER_SOURCES})
find_package(Threads REQUIRED)

target_include_directories(astra_renderer PUBLIC
    src
    src/rhi
    ${Vulkan_INCLUDE_DIRS}
)

# Optional dependencies are consumed ONLY when actually found.
# find_path() yields <VAR>-NOTFOUND on failure; a NOTFOUND value passed to
# target_include_directories() is rejected by CMake at generate time, which is
# exactly the failure this guards against. When a dependency IS found, the
# include dir and the matching feature guard are exported to all consumers.
if(ENTT_INCLUDE_DIR)
    target_include_directories(astra_renderer PUBLIC ${ENTT_INCLUDE_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_ENTT=1)
endif()
if(IMGUI_DIR)
    target_include_directories(astra_renderer PUBLIC ${IMGUI_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_IMGUI=1)
endif()
if(MINIAUDIO_INCLUDE_DIR)
    target_include_directories(astra_renderer PUBLIC ${MINIAUDIO_INCLUDE_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_MINIAUDIO=1)
endif()

if(GLFW_FOUND)
    target_include_directories(astra_renderer PUBLIC ${GLFW_INCLUDE_DIRS})
    target_link_libraries(astra_renderer PUBLIC ${GLFW_LIBRARIES})
endif()

if(Vulkan_FOUND)
    target_link_libraries(astra_renderer PUBLIC Vulkan::Vulkan)
endif()

target_compile_definitions(astra_renderer PUBLIC
    ASTRA_VERSION_MAJOR=0 ASTRA_VERSION_MINOR=1 ASTRA_VERSION_PATCH=0
    $<$<BOOL:${ASTRA_ENABLE_VALIDATION}>:ASTRA_VALIDATION>
)

# Legacy launcher source retained for opt-in developer builds; START.bat is primary.
option(ASTRA_BUILD_LAUNCHER "Build legacy Windows launcher ASTRA COSMOS.exe" OFF)
if(ASTRA_BUILD_LAUNCHER)
    add_executable(astra_launcher launcher/launcher.cpp)
    # Output name with space — Windows launcher: "ASTRA COSMOS.exe"
    set_target_properties(astra_launcher PROPERTIES
        OUTPUT_NAME "ASTRA COSMOS"
        SUFFIX ".exe"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    target_link_libraries(astra_launcher PRIVATE Threads::Threads)
    # On Windows, avoid console? Keep console for logs (stdout/stderr preserved)
    if(MSVC)
        target_compile_options(astra_launcher PRIVATE /O2 /GL)
        set_target_properties(astra_launcher PROPERTIES LINK_FLAGS "/LTCG /SUBSYSTEM:CONSOLE")
    else()
        target_compile_options(astra_launcher PRIVATE -O3)
    endif()
    # Install launcher to release root
    install(TARGETS astra_launcher RUNTIME DESTINATION .)
    message(STATUS "Launcher: ASTRA COSMOS.exe from launcher/launcher.cpp")
endif()

# Main executable — headless + windowed (diagnostic entry point, developer-only)
add_executable(astra_native
    src/main.cpp
)
target_link_libraries(astra_native PRIVATE astra_renderer Threads::Threads)

# ─── PRODUCTION Win32 + Vulkan application ────────────────────────────────────
# Windows-only by design (Win32 API + /SUBSYSTEM:WINDOWS in main_production.cpp).
# Gated so the same project configures and builds cleanly on other hosts;
# on Windows the target and its output are exactly as before.
if(WIN32)
    add_executable(astra_cosmos WIN32
        src/main_production.cpp
    )
    set_target_properties(astra_cosmos PROPERTIES
        OUTPUT_NAME "ASTRA COSMOS"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
    target_include_directories(astra_cosmos PRIVATE
        src
        ${Vulkan_INCLUDE_DIRS}
    )
    target_link_libraries(astra_cosmos PRIVATE
        astra_renderer
        Threads::Threads
    )
    if(Vulkan_FOUND)
        target_link_libraries(astra_cosmos PRIVATE Vulkan::Vulkan)
    endif()
    if(MSVC)
        target_compile_options(astra_cosmos PRIVATE /W3 /utf-8)
        # WIN32 in add_executable already sets /SUBSYSTEM:WINDOWS.
    endif()
    message(STATUS "Production target: ASTRA COSMOS.exe (Win32 + Vulkan 1.3)")
else()
    message(STATUS "Production target astra_cosmos is Windows-only — skipped on ${CMAKE_SYSTEM_NAME}")
endif()
# Also link renderer threads
target_link_libraries(astra_renderer PUBLIC Threads::Threads)

# Tools
if(ASTRA_BUILD_TOOLS)
    add_subdirectory(tools EXCLUDE_FROM_ALL)
endif()

# Tests
if(ASTRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# ─── Shader compilation ───────────────────────────────────────────────────────
# glslangValidator ships with the Vulkan SDK (<SDK>/Bin) and with most Linux
# distributions; CMake's FindVulkan exposes it since 3.21.
if(NOT GLSLANG_VALIDATOR AND Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    set(GLSLANG_VALIDATOR "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
endif()
if(NOT GLSLANG_VALIDATOR)
    find_program(GLSLANG_VALIDATOR
        NAMES glslangValidator glslangValidator.exe
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
endif()

function(astra_compile_shaders OUT_VAR)
    # astra_compile_shaders(<out-var> shader1 shader2 ...)
    # Emits <CMAKE_BINARY_DIR>/shaders/<name>.spv for each GLSL source.
    set(outputs "")
    foreach(SHADER ${ARGN})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND ${GLSLANG_VALIDATOR} -V "${SHADER}" -o "${SPV}"
            DEPENDS "${SHADER}"
            COMMENT "Compiling ${SHADER_NAME} -> SPIR-V"
            VERBATIM
        )
        list(APPEND outputs "${SPV}")
    endforeach()
    set(${OUT_VAR} "${outputs}" PARENT_SCOPE)
endfunction()

if(GLSLANG_VALIDATOR)
    message(STATUS "glslangValidator: ${GLSLANG_VALIDATOR}")

    # Production shaders — REQUIRED by ASTRA COSMOS.exe at runtime; built by ALL.
    set(ASTRA_PRODUCTION_SHADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/astra.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/astra.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/sphere.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/sphere.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/orbit.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/orbit.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vector.vert"
    )
    astra_compile_shaders(ASTRA_PRODUCTION_SPV ${ASTRA_PRODUCTION_SHADERS})
    add_custom_target(astra_shaders ALL DEPENDS ${ASTRA_PRODUCTION_SPV})
    if(WIN32)
        add_dependencies(astra_cosmos astra_shaders)
        # Keep the SPIR-V next to the executable; main_production.cpp loads it from
        # <exe_dir>/shaders at runtime (with safe fallbacks).
        add_custom_command(TARGET astra_cosmos POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:astra_cosmos>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASTRA_PRODUCTION_SPV}
                    "$<TARGET_FILE_DIR:astra_cosmos>/shaders"
            COMMENT "Installing production shaders next to ASTRA COSMOS.exe"
            VERBATIM
        )
    endif()

    # Full visual-effects shader library — opt-in; validated statically too.
    file(GLOB_RECURSE ASTRA_LIBRARY_SHADERS
        shaders/*.glsl shaders/*.vert shaders/*.frag shaders/*.comp)
    if(ASTRA_LIBRARY_SHADERS)
        astra_compile_shaders(ASTRA_LIBRARY_SPV ${ASTRA_LIBRARY_SHADERS})
        add_custom_target(astra_shader_library DEPENDS ${ASTRA_LIBRARY_SPV})
    endif()
else()
    message(WARNING
        "glslangValidator not found — production shaders will NOT be compiled.\n"
        "  ASTRA COSMOS.exe needs shaders/astra.vert.spv and shaders/astra.frag.spv\n"
        "  next to the executable. Re-run CMake with the Vulkan SDK (or\n"
        "  glslangValidator) on PATH to build them.")
endif()

# Install
install(TARGETS astra_renderer astra_native
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY shaders DESTINATION share/astra/shaders)
install(DIRECTORY assets DESTINATION share/astra/assets)

message(STATUS "ASTRA Native Renderer — C++20 Vulkan 1.3")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Vulkan: ${Vulkan_FOUND} ${Vulkan_INCLUDE_DIRS}")
message(STATUS "  GLFW: ${GLFW_FOUND}")
foreach(_astra_opt ENTT_INCLUDE_DIR IMGUI_DIR MINIAUDIO_INCLUDE_DIR)
    if(${_astra_opt})
        message(STATUS "  ${_astra_opt}: ${${_astra_opt}}")
    else()
        message(STATUS "  ${_astra_opt}: not found — using project fallback (OK)")
    endif()
endforeach()
message(STATUS "  Tests: ${ASTRA_BUILD_TESTS} Tools: ${ASTRA_BUILD_TOOLS}")
