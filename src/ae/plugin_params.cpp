#include "builtin_lenses.h"
#include "plugin_entry.h"
#include "param_schema.h"

PF_Err PluginHandleParamSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);

    const AeUiParameterState defaults {};
    std::string lens_popup = build_lens_preset_popup_string();
    std::string view_popup = build_output_view_popup_string();

    PF_ADD_POPUPX("Lens",
                  static_cast<A_short>(builtin_lens_count()),
                  static_cast<A_short>(defaults.lens_preset_index),
                  lens_popup.data(),
                  PF_ParamFlag_NONE,
                  PARAM_LENS_PRESET);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flare Gain",
                         0.0f,
                         100000.0f,
                         0.0f,
                         5000.0f,
                         defaults.flare_gain,
                         1,
                         PF_ValueDisplayFlag_NONE,
                         PF_ParamFlag_NONE,
                         PARAM_FLARE_GAIN);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Threshold",
                         0.0f,
                         1000.0f,
                         0.0f,
                         32.0f,
                         defaults.threshold,
                         2,
                         PF_ValueDisplayFlag_NONE,
                         PF_ParamFlag_NONE,
                         PARAM_THRESHOLD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Ray Grid",
                  1,
                  512,
                  1,
                  128,
                  defaults.ray_grid,
                  PARAM_RAY_GRID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Downsample",
                  1,
                  16,
                  1,
                  8,
                  defaults.downsample,
                  PARAM_DOWNSAMPLE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("View",
                  static_cast<A_short>(output_view_popup_count()),
                  static_cast<A_short>(defaults.view_mode_index),
                  view_popup.data(),
                  PF_ParamFlag_NONE,
                  PARAM_VIEW_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Mask Layer", PF_LayerDefault_NONE, PARAM_MASK_LAYER);

    out_data->num_params = PARAM_COUNT;
    return err;
}
