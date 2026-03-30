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
    if(NOT AE_SDK_ROOT)
        message(FATAL_ERROR "FLARESIM_AE_ENABLE_AE_PLUGIN=ON requires AE_SDK_ROOT to be set.")
    endif()

    set(AE_SDK_INCLUDE_DIRS
        ${AE_SDK_ROOT}
        ${AE_SDK_ROOT}/Examples
        ${AE_SDK_ROOT}/Examples/Headers
        ${AE_SDK_ROOT}/Examples/Util
        ${AE_SDK_ROOT}/Headers
    )

    add_library(flaresim_ae_plugin MODULE
        plugin_entry.cpp
        plugin_global_setup.cpp
        plugin_params.cpp
        plugin_sequence.cpp
        plugin_smart_render.cpp
    )

    target_include_directories(flaresim_ae_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${AE_SDK_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
    )

    target_link_libraries(flaresim_ae_plugin PRIVATE flaresim_ae_adapter)

    set_target_properties(flaresim_ae_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".aex"
        OUTPUT_NAME "FlareSimAE"
    )

    message(WARNING "AE plugin target is scaffold-only for now. PiPL/resource wiring and full selector logic still need implementation.")
endif()
