#include "builtin_lenses.h"
#include "plugin_entry.h"
#include "param_schema.h"

#include <string>
#include <vector>

namespace {

constexpr float kManualFloatMax = 1.0e9f;
constexpr A_long kManualIntMax = 1000000;

} // namespace

PF_Err PluginHandleParamSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    const AeUiParameterState defaults {};
    const std::string legacy_lens_popup = build_lens_preset_popup_string();
    const std::string manufacturer_popup = build_lens_manufacturer_popup_string();
    const std::string view_popup = build_output_view_popup_string();

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_USE_VALUE_FOR_OLD_PROJECTS;
    def.ui_flags = PF_PUI_INVISIBLE;
    PF_ADD_POPUP("Legacy Lens",
                 static_cast<A_short>(builtin_lens_count_for_manufacturer(0)),
                 static_cast<A_short>(default_legacy_lens_popup_index()),
                 legacy_lens_popup.data(),
                 PARAM_ID_LEGACY_LENS_PRESET);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPICX("Lens",
                  PF_ParamFlag_NONE,
                  PARAM_ID_LENS_SECTION_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Manufacturer",
                  static_cast<A_short>(builtin_lens_manufacturer_count()),
                  static_cast<A_short>(default_lens_manufacturer_popup_index()),
                  manufacturer_popup.data(),
                  PF_ParamFlag_NONE,
                  PARAM_ID_LENS_MANUFACTURER);

    std::vector<std::string> manufacturer_lens_popups(builtin_lens_manufacturer_count());
    for (std::size_t manufacturer_index = 0;
         manufacturer_index < builtin_lens_manufacturer_count();
         ++manufacturer_index) {
        const auto* manufacturer = builtin_lens_manufacturer(manufacturer_index);
        if (!manufacturer) {
            continue;
        }

        manufacturer_lens_popups[manufacturer_index] =
            build_lens_popup_string_for_manufacturer(static_cast<int>(manufacturer_index));

        const PF_ParamFlags group_flags =
            manufacturer_index == 0 ? PF_ParamFlag_NONE : PF_ParamFlag_START_COLLAPSED;

        AEFX_CLR_STRUCT(def);
        PF_ADD_TOPICX(manufacturer->label,
                      group_flags,
                      lens_group_start_param_id(static_cast<int>(manufacturer_index)));

        const int lens_default =
            static_cast<int>(manufacturer_index) + 1 == default_lens_manufacturer_popup_index()
                ? default_lens_model_popup_index()
                : 1;

        AEFX_CLR_STRUCT(def);
        PF_ADD_POPUPX("Lens",
                      static_cast<A_short>(manufacturer->lens_count),
                      static_cast<A_short>(lens_default),
                      manufacturer_lens_popups[manufacturer_index].data(),
                      PF_ParamFlag_NONE,
                      lens_popup_param_id(static_cast<int>(manufacturer_index)));

        AEFX_CLR_STRUCT(def);
        PF_END_TOPIC(lens_group_end_param_id(static_cast<int>(manufacturer_index)));
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_ID_LENS_SECTION_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flare Gain",
                         0.0f,
                         kManualFloatMax,
                         0.0f,
                         5000.0f,
                         defaults.flare_gain,
                         1,
                         PF_ValueDisplayFlag_NONE,
                         PF_ParamFlag_NONE,
                         PARAM_ID_FLARE_GAIN);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Threshold",
                         0.0f,
                         kManualFloatMax,
                         0.0f,
                         64.0f,
                         defaults.threshold,
                         2,
                         PF_ValueDisplayFlag_NONE,
                         PF_ParamFlag_NONE,
                         PARAM_ID_THRESHOLD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Ray Grid",
                  1,
                  kManualIntMax,
                  1,
                  512,
                  defaults.ray_grid,
                  PARAM_ID_RAY_GRID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Downsample",
                  1,
                  kManualIntMax,
                  1,
                  12,
                  defaults.downsample,
                  PARAM_ID_DOWNSAMPLE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("View",
                  static_cast<A_short>(output_view_popup_count()),
                  static_cast<A_short>(defaults.view_mode_index),
                  view_popup.data(),
                  PF_ParamFlag_NONE,
                  PARAM_ID_VIEW_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Mask Layer", PF_LayerDefault_NONE, PARAM_ID_MASK_LAYER);

    out_data->num_params = parameter_count();
    return err;
}
