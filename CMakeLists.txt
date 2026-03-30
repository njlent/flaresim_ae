cmake_minimum_required(VERSION 3.20)
project(flaresim_ae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLARESIM_AE_ENABLE_AE_PLUGIN "Build the After Effects plugin target if the SDK is available." OFF)
set(AE_SDK_ROOT "" CACHE PATH "Path to the Adobe After Effects SDK root")

if(NOT AE_SDK_ROOT)
    set(_flaresim_ae_sdk_candidates
        "E:/projects/ae/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK"
        "E:/projects/ae/AfterEffectsSDK_25.6_61_win"
    )

    foreach(_flaresim_ae_sdk_candidate IN LISTS _flaresim_ae_sdk_candidates)
        if(EXISTS "${_flaresim_ae_sdk_candidate}/Examples/Headers/AEConfig.h")
            set(AE_SDK_ROOT "${_flaresim_ae_sdk_candidate}" CACHE PATH "Path to the Adobe After Effects SDK root" FORCE)
            break()
        endif()
    endforeach()

    unset(_flaresim_ae_sdk_candidate)
    unset(_flaresim_ae_sdk_candidates)
endif()

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

add_subdirectory(src/ae)

include(CTest)

if(BUILD_TESTING)
    add_executable(flaresim_core_smoke
        tests/core_smoke.cpp
    )
    target_link_libraries(flaresim_core_smoke PRIVATE flaresim_ae_adapter)
    target_compile_definitions(flaresim_core_smoke PRIVATE
        FLARESIM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME flaresim_core_smoke COMMAND flaresim_core_smoke)
endif()
