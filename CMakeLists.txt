cmake_minimum_required(VERSION 3.18)
project(flaresim_ae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(OpenMP)

add_library(flaresim_core STATIC
  src/core/lens.cpp
  src/core/trace.cpp
  src/core/ghost.cpp
)

target_include_directories(flaresim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

if(OpenMP_CXX_FOUND)
  target_link_libraries(flaresim_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(flaresim_core_smoke
  tests/core_smoke.cpp
)

target_link_libraries(flaresim_core_smoke PRIVATE flaresim_core)
target_compile_definitions(flaresim_core_smoke PRIVATE
  FLARESIM_AE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(BUILD_TESTING)
  add_test(NAME flaresim_core_smoke COMMAND flaresim_core_smoke)
endif()
