# EffectCraft effect plug-in: a WebAssembly module (plug-in API v1) built with wasi-sdk.
# EffectCraft refuses modules with imports, so WASI calls are stubbed in wasi_stubs.cpp and
# the bundled lens prescriptions are compiled in.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FLARESIM_EC_LENS_BANK_INC "${CMAKE_CURRENT_BINARY_DIR}/generated/ec_lens_bank_generated.inc")
# The lens table is regenerated whenever lenses are added, so it stands in for the .lens files
# (some file names contain characters Ninja can't list as dependencies).
add_custom_command(
    OUTPUT "${FLARESIM_EC_LENS_BANK_INC}"
    COMMAND Python3::Interpreter -I "${PROJECT_SOURCE_DIR}/scripts/generate_ec_lens_bank.py" "${FLARESIM_EC_LENS_BANK_INC}"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/scripts/generate_ec_lens_bank.py"
        "${PROJECT_SOURCE_DIR}/src/ae/builtin_lenses_generated.inc"
    COMMENT "Embedding builtin lens prescriptions"
    VERBATIM
)

add_executable(flaresim_ec
    ec_plugin.cpp
    ec_budget.cpp
    ec_lens_bank.cpp
    wasi_stubs.cpp
    "${FLARESIM_EC_LENS_BANK_INC}"
    ${PROJECT_SOURCE_DIR}/src/ae/builtin_lenses.cpp
    ${PROJECT_SOURCE_DIR}/src/ae/output_view.cpp
    ${PROJECT_SOURCE_DIR}/src/ae/param_schema.cpp
    ${PROJECT_SOURCE_DIR}/src/ae/parameter_state.cpp
)

target_include_directories(flaresim_ec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${PROJECT_SOURCE_DIR}/src/ae
)

target_link_libraries(flaresim_ec PRIVATE flaresim_runtime)

set_target_properties(flaresim_ec PROPERTIES
    OUTPUT_NAME "flaresim_ec"
    SUFFIX ".wasm"
)

target_link_options(flaresim_ec PRIVATE
    -nostartfiles
    -Wl,--no-entry
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,-z,stack-size=8388608
    -Wl,--initial-memory=33554432
    -Wl,--max-memory=1073741824
)
