add_library(flaresim_ps_adapter STATIC
    ps_image.cpp
    ps_settings.cpp
)

target_include_directories(flaresim_ps_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(flaresim_ps_adapter PUBLIC flaresim_ae_adapter)

# UI strings contain non-ASCII characters; without /utf-8 MSVC reads the
# sources in the system codepage and the dialog shows mojibake.
if(MSVC)
    target_compile_options(flaresim_ps_adapter PUBLIC /utf-8)
endif()

if(FLARESIM_PS_ENABLE_PS_PLUGIN)
    if(NOT WIN32)
        message(FATAL_ERROR "FLARESIM_PS_ENABLE_PS_PLUGIN=ON currently supports Windows only.")
    endif()
    if(NOT PS_SDK_RESOLVED_ROOT)
        message(FATAL_ERROR "FLARESIM_PS_ENABLE_PS_PLUGIN=ON requires PS_SDK_ROOT to point at the Photoshop SDK folder (the one containing pluginsdk/).")
    endif()

    set(PS_SDK_PLUGINSDK "${PS_SDK_RESOLVED_ROOT}/pluginsdk")
    set(FLARESIM_PS_PIPL_JSON "${CMAKE_CURRENT_SOURCE_DIR}/PiPLs.json")

    add_library(flaresim_ps_plugin MODULE
        FlareSimPS.rc
        ps_dialog_win.cpp
        ps_plugin_main.cpp
    )

    # The .rc embeds PiPLs.json by path; rebuild the resource when it changes.
    set_property(SOURCE FlareSimPS.rc APPEND PROPERTY OBJECT_DEPENDS
        "${FLARESIM_PS_PIPL_JSON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/ps_version.h"
    )

    target_include_directories(flaresim_ps_plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PS_SDK_PLUGINSDK}/photoshopapi/photoshop
        ${PS_SDK_PLUGINSDK}/photoshopapi/pica_sp
        ${PS_SDK_PLUGINSDK}/samplecode/common/includes
    )

    target_compile_definitions(flaresim_ps_plugin PRIVATE
        WIN32=1
        _WINDOWS
        _USRDLL
        ISOLATION_AWARE_ENABLED=1
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_DEPRECATE
        FLARESIM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
    )

    # The SDK headers carry cp1252 characters in comments; harmless under /utf-8.
    target_compile_options(flaresim_ps_plugin PRIVATE /wd4828)

    target_link_libraries(flaresim_ps_plugin PRIVATE
        flaresim_ps_adapter
        comctl32
        uxtheme
    )

    # Common Controls v6 for the dialog; the linker embeds it in the DLL's
    # manifest, which ISOLATION_AWARE_ENABLED activates around UI calls.
    target_link_options(flaresim_ps_plugin PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )

    set_target_properties(flaresim_ps_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".8bf"
        OUTPUT_NAME "FlareSimPS"
    )
endif()
