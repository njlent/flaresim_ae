cmake_minimum_required(VERSION 3.20)
project(flaresim_ae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLARESIM_AE_ENABLE_AE_PLUGIN "Build the After Effects plugin target if the SDK is available." OFF)
option(FLARESIM_PS_ENABLE_PS_PLUGIN "Build the Photoshop filter plugin target if the SDK is available." OFF)
option(FLARESIM_ENABLE_CUDA "Build the CUDA ghost renderer when a CUDA toolkit is available." ON)
set(FLARESIM_CUDA_ARCHITECTURES "75;86;89" CACHE STRING "CUDA architectures for the ghost renderer target")
set(AE_SDK_ROOT "" CACHE PATH "Path to the Adobe After Effects SDK folder or its containing extracted package directory")
set(FLARESIM_AE_SDK_SENTINEL "Examples/Headers/AEConfig.h")
set(FLARESIM_AE_SDK_INNER_DIRNAME "ae25.6_61.64bit.AfterEffectsSDK")
set(FLARESIM_AE_SDK_CANONICAL_ROOT "E:/projects/ae/SDK/AfterEffectsSDK_25.6_61_win")
set(PS_SDK_ROOT "" CACHE PATH "Path to the Adobe Photoshop SDK folder (contains pluginsdk/) or a directory holding the extracted SDK")
set(FLARESIM_PS_SDK_SENTINEL "pluginsdk/photoshopapi/photoshop/PIFilter.h")
set(FLARESIM_PS_SDK_CANONICAL_ROOT "E:/projects/ae/SDK/adobe_photoshop_sdk_2026_win_v2")

set(FLARESIM_CUDA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/frame_cuda.cu"
)
set(FLARESIM_CUDA_STUB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda_stub.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/frame_cuda_stub.cpp"
)

set(AE_SDK_RESOLVED_ROOT "")
set(_flaresim_ae_sdk_candidates)
set(_flaresim_ae_sdk_requested_root "${AE_SDK_ROOT}")

if(_flaresim_ae_sdk_requested_root)
    list(APPEND _flaresim_ae_sdk_candidates
        "${_flaresim_ae_sdk_requested_root}"
        "${_flaresim_ae_sdk_requested_root}/${FLARESIM_AE_SDK_INNER_DIRNAME}"
    )

    file(GLOB _flaresim_ae_sdk_nested_candidates LIST_DIRECTORIES true
        "${_flaresim_ae_sdk_requested_root}/*.AfterEffectsSDK"
    )
    list(APPEND _flaresim_ae_sdk_candidates ${_flaresim_ae_sdk_nested_candidates})
endif()

list(APPEND _flaresim_ae_sdk_candidates
    "${FLARESIM_AE_SDK_CANONICAL_ROOT}"
    "${FLARESIM_AE_SDK_CANONICAL_ROOT}/${FLARESIM_AE_SDK_INNER_DIRNAME}"
)

foreach(_flaresim_ae_sdk_candidate IN LISTS _flaresim_ae_sdk_candidates)
    if(EXISTS "${_flaresim_ae_sdk_candidate}/${FLARESIM_AE_SDK_SENTINEL}")
        set(AE_SDK_RESOLVED_ROOT "${_flaresim_ae_sdk_candidate}")
        break()
    endif()
endforeach()

if(NOT _flaresim_ae_sdk_requested_root)
    set(AE_SDK_ROOT "${FLARESIM_AE_SDK_CANONICAL_ROOT}" CACHE PATH "Path to the Adobe After Effects SDK folder or its containing extracted package directory" FORCE)
endif()

unset(_flaresim_ae_sdk_candidate)
unset(_flaresim_ae_sdk_candidates)
unset(_flaresim_ae_sdk_nested_candidates)
unset(_flaresim_ae_sdk_requested_root)

set(PS_SDK_RESOLVED_ROOT "")
set(_flaresim_ps_sdk_candidates)

if(PS_SDK_ROOT)
    list(APPEND _flaresim_ps_sdk_candidates "${PS_SDK_ROOT}")
    file(GLOB _flaresim_ps_sdk_nested_candidates LIST_DIRECTORIES true
        "${PS_SDK_ROOT}/adobe_photoshop_sdk_*"
    )
    list(APPEND _flaresim_ps_sdk_candidates ${_flaresim_ps_sdk_nested_candidates})
endif()

list(APPEND _flaresim_ps_sdk_candidates "${FLARESIM_PS_SDK_CANONICAL_ROOT}")

foreach(_flaresim_ps_sdk_candidate IN LISTS _flaresim_ps_sdk_candidates)
    if(EXISTS "${_flaresim_ps_sdk_candidate}/${FLARESIM_PS_SDK_SENTINEL}")
        set(PS_SDK_RESOLVED_ROOT "${_flaresim_ps_sdk_candidate}")
        break()
    endif()
endforeach()

unset(_flaresim_ps_sdk_candidate)
unset(_flaresim_ps_sdk_candidates)
unset(_flaresim_ps_sdk_nested_candidates)

find_package(OpenMP)

set(FLARESIM_CUDA_ENABLED OFF)

if(FLARESIM_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit QUIET)

        if(CUDAToolkit_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda.cu")
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
        ${FLARESIM_CUDA_SOURCES}
    )

    target_include_directories(flaresim_cuda
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ae
    )

    target_link_libraries(flaresim_cuda PUBLIC CUDA::cudart)

    set_target_properties(flaresim_cuda PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        CUDA_ARCHITECTURES "${FLARESIM_CUDA_ARCHITECTURES}"
    )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/ghost_cuda_stub.cpp")
    add_library(flaresim_cuda STATIC
        ${FLARESIM_CUDA_STUB_SOURCES}
    )

    target_include_directories(flaresim_cuda
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ae
    )
else()
    add_library(flaresim_cuda INTERFACE)
endif()

add_library(flaresim_core STATIC
    src/core/bloom.cpp
    src/core/color_space.cpp
    src/core/ghost.cpp
    src/core/lens.cpp
    src/core/post_process.cpp
    src/core/source_extract.cpp
    src/core/starburst.cpp
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
add_subdirectory(src/ps)

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

    add_executable(flaresim_ps_smoke
        tests/ps_smoke.cpp
    )
    target_link_libraries(flaresim_ps_smoke PRIVATE flaresim_ps_adapter)
    target_compile_definitions(flaresim_ps_smoke PRIVATE
        FLARESIM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME flaresim_ps_smoke COMMAND flaresim_ps_smoke)

    add_executable(flaresim_ps_reference
        tests/ps_reference_render.cpp
    )
    target_link_libraries(flaresim_ps_reference PRIVATE flaresim_ps_adapter)
    target_compile_definitions(flaresim_ps_reference PRIVATE
        FLARESIM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(WIN32)
        add_executable(flaresim_ps_dialog_harness
            tests/ps_dialog_harness.cpp
            src/ps/ps_dialog_win.cpp
        )
        target_link_libraries(flaresim_ps_dialog_harness PRIVATE flaresim_ps_adapter comctl32)
        target_compile_definitions(flaresim_ps_dialog_harness PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            FLARESIM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_options(flaresim_ps_dialog_harness PRIVATE
            "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
        )
    endif()
endif()
