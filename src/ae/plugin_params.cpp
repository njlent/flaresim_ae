#include "plugin_entry.h"

namespace {

enum ParamIndex
{
    PARAM_INPUT = 0,
    PARAM_LENS_PRESET,
    PARAM_FLARE_GAIN,
    PARAM_THRESHOLD,
    PARAM_RAY_GRID,
    PARAM_DOWNSAMPLE,
    PARAM_VIEW_MODE,
    PARAM_MASK_LAYER,
    PARAM_COUNT
};

} // namespace

PF_Err PluginHandleParamSetup(PF_InData*, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    out_data->num_params = PARAM_COUNT;

    // TODO: add real AE params once the Adobe SDK is present locally.
    // Keep this aligned with:
    // - builtin_lenses.*
    // - parameter_state.*
    // - render_frame.*

    return PF_Err_NONE;
}
