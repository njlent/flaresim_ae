#include "plugin_entry.h"
#include "plugin_version.h"

#include <cstdio>

PF_Err PluginHandleAbout(PF_InData*, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    std::snprintf(out_data->return_msg,
                  sizeof(out_data->return_msg),
                  "FlareSimAE %d.%d.%d\rSDK-backed wrapper scaffold. Runtime lives in shared core.",
                  FLARESIM_AE_VERSION_MAJOR,
                  FLARESIM_AE_VERSION_MINOR,
                  FLARESIM_AE_VERSION_PATCH);
    return PF_Err_NONE;
}

PF_Err PluginHandleGlobalSetup(PF_InData*, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    out_data->my_version = PF_VERSION(FLARESIM_AE_VERSION_MAJOR,
                                      FLARESIM_AE_VERSION_MINOR,
                                      FLARESIM_AE_VERSION_PATCH,
                                      0,
                                      0);

    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE;
    return PF_Err_NONE;
}
