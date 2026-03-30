#include "plugin_entry.h"
#include "param_schema.h"

PF_Err PluginHandleParamSetup(PF_InData*, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    out_data->num_params = PARAM_COUNT;

    // TODO: add real AE params once the Adobe SDK is present locally.
    // Shared labels + popup ordering already live in:
    // - build_lens_preset_popup_string()
    // - build_output_view_popup_string()
    // - apply_ui_parameter_state()

    return PF_Err_NONE;
}
