add_library(flaresim_ae_adapter STATIC
    builtin_lenses.cpp
    lens_resolution.cpp
    parameter_state.cpp
)

target_include_directories(flaresim_ae_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
)

target_link_libraries(flaresim_ae_adapter PUBLIC flaresim_runtime)

if(FLARESIM_AE_ENABLE_AE_PLUGIN)
    message(STATUS "AE SDK-backed plugin target is not wired yet; adapter library is available for continued implementation.")
endif()
