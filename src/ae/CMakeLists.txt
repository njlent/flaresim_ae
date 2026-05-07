add_library(flaresim_ae_adapter STATIC
    asset_root.cpp
    builtin_lenses.cpp
    frame_bridge.cpp
    lens_resolution.cpp
    output_view.cpp
    param_schema.cpp
    parameter_state.cpp
    pixel_convert.cpp
)

target_include_directories(flaresim_ae_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
)

target_link_libraries(flaresim_ae_adapter PUBLIC flaresim_runtime)

if(FLARESIM_AE_ENABLE_AE_PLUGIN)
    if(NOT AE_SDK_RESOLVED_ROOT)
        message(FATAL_ERROR "FLARESIM_AE_ENABLE_AE_PLUGIN=ON requires AE_SDK_ROOT to point at the SDK folder or its containing extracted package directory.")
    endif()

    set(AE_PIPL_TOOL "${AE_SDK_RESOLVED_ROOT}/Examples/Resources/PiPLtool.exe")

    set(AE_SDK_INCLUDE_DIRS
        ${AE_SDK_RESOLVED_ROOT}
        ${AE_SDK_RESOLVED_ROOT}/Examples
        ${AE_SDK_RESOLVED_ROOT}/Examples/Headers
        ${AE_SDK_RESOLVED_ROOT}/Examples/Headers/SP
        ${AE_SDK_RESOLVED_ROOT}/Examples/Util
        ${AE_SDK_RESOLVED_ROOT}/Headers
    )

    set(FLARESIM_AE_PIPL_R "${CMAKE_CURRENT_SOURCE_DIR}/FlareSimAEPiPL.r")
    set(FLARESIM_AE_PIPL_RR "${CMAKE_CURRENT_BINARY_DIR}/FlareSimAEPiPL.rr")
    set(FLARESIM_AE_PIPL_RRC "${CMAKE_CURRENT_BINARY_DIR}/FlareSimAEPiPL.rrc")
    set(FLARESIM_AE_PIPL_RC "${CMAKE_CURRENT_BINARY_DIR}/FlareSimAEPiPL.rc")

    add_custom_command(
        OUTPUT
            "${FLARESIM_AE_PIPL_RC}"
        COMMAND
            cmd /c cl /nologo /TC /EP /I ${CMAKE_CURRENT_SOURCE_DIR} /I ${AE_SDK_RESOLVED_ROOT}/Examples/Headers ${FLARESIM_AE_PIPL_R} > ${FLARESIM_AE_PIPL_RR}
        COMMAND
            "${AE_PIPL_TOOL}" "${FLARESIM_AE_PIPL_RR}" "${FLARESIM_AE_PIPL_RRC}"
        COMMAND
            cmd /c cl /nologo /TC /EP /D MSWindows ${FLARESIM_AE_PIPL_RRC} > ${FLARESIM_AE_PIPL_RC}
        DEPENDS
            "${FLARESIM_AE_PIPL_R}"
            "${CMAKE_CURRENT_SOURCE_DIR}/plugin_version.h"
        VERBATIM
    )

    add_library(flaresim_ae_plugin MODULE
        "${FLARESIM_AE_PIPL_RC}"
        plugin_entry.cpp
        plugin_gpu_render.cpp
        plugin_global_setup.cpp
        plugin_params.cpp
        render_shared.cpp
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

    target_compile_definitions(flaresim_ae_plugin PRIVATE
        FLARESIM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
    )

    set_target_properties(flaresim_ae_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".aex"
        OUTPUT_NAME "FlareSimAE"
    )
endif()
