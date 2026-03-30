cmake_minimum_required(VERSION 3.20)
project(flaresim_ae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLARESIM_AE_ENABLE_AE_PLUGIN "Build the After Effects plugin target if the SDK is available." OFF)

find_package(OpenMP)

add_library(flaresim_core STATIC
    src/core/bloom.cpp
    src/core/ghost.cpp
    src/core/lens.cpp
    src/core/source_extract.cpp
    src/core/trace.cpp
)

target_include_directories(flaresim_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(flaresim_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_library(flaresim_runtime STATIC
    src/runtime/render_frame.cpp
)

target_include_directories(flaresim_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
)

target_link_libraries(flaresim_runtime PUBLIC flaresim_core)

include(CTest)

if(BUILD_TESTING)
    add_executable(flaresim_core_smoke
        tests/core_smoke.cpp
    )
    target_link_libraries(flaresim_core_smoke PRIVATE flaresim_runtime)
    target_compile_definitions(flaresim_core_smoke PRIVATE
        FLARESIM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME flaresim_core_smoke COMMAND flaresim_core_smoke)
endif()

if(FLARESIM_AE_ENABLE_AE_PLUGIN)
    add_subdirectory(src/ae)
endif()
