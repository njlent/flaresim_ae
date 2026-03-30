cmake_minimum_required(VERSION 3.20)
project(flaresim_ae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLARESIM_AE_ENABLE_AE_PLUGIN "Build the After Effects plugin target if the SDK is available." OFF)
option(FLARESIM_ENABLE_CUDA "Build the CUDA ghost renderer when a CUDA toolkit is available." ON)
set(FLARESIM_CUDA_ARCHITECTURES "75;86;89" CACHE STRING "CUDA architectures for the ghost renderer target")
set(AE_SDK_ROOT "" CACHE PATH "Path to the Adobe After Effects SDK root")

set(FLARESIM_CUDA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda.cu")
set(FLARESIM_CUDA_STUB_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda_stub.cpp")

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

set(FLARESIM_CUDA_ENABLED OFF)

if(FLARESIM_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit QUIET)

        if(CUDAToolkit_FOUND AND EXISTS "${FLARESIM_CUDA_SOURCE}")
            set(FLARESIM_CUDA_ENABLED ON)

            set(CMAKE_CUDA_STANDARD 17)
            set(CMAKE_CUDA_STANDARD_REQUIRED ON)

            if(WIN32)
                set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
            endif()
        endif()
    endif()
endif()

if(FLARESIM_CUDA_ENABLED)
    add_library(flaresim_cuda STATIC
        ${FLARESIM_CUDA_SOURCE}
    )

    target_include_directories(flaresim_cuda
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )

    target_link_libraries(flaresim_cuda PUBLIC CUDA::cudart)

    set_target_properties(flaresim_cuda PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        CUDA_ARCHITECTURES "${FLARESIM_CUDA_ARCHITECTURES}"
    )
elseif(EXISTS "${FLARESIM_CUDA_STUB_SOURCE}")
    add_library(flaresim_cuda STATIC
        ${FLARESIM_CUDA_STUB_SOURCE}
    )

    target_include_directories(flaresim_cuda
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    )
else()
    add_library(flaresim_cuda INTERFACE)
endif()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(flaresim_core PUBLIC OpenMP::OpenMP_CXX)
endif()

target_link_libraries(flaresim_core PUBLIC flaresim_cuda)

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
