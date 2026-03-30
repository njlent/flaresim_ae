#include "plugin_entry.h"

extern "C" {

FLARESIM_AE_EXPORT PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra)
{
    switch (cmd) {
        case PF_Cmd_ABOUT:
            return PluginHandleAbout(in_data, out_data, params, output);
        case PF_Cmd_GLOBAL_SETUP:
            return PluginHandleGlobalSetup(in_data, out_data, params, output);
        case PF_Cmd_PARAM_SETUP:
            return PluginHandleParamSetup(in_data, out_data, params, output);
        case PF_Cmd_SEQUENCE_SETUP:
            return PluginHandleSequenceSetup(in_data, out_data, params, output);
        case PF_Cmd_SEQUENCE_RESETUP:
            return PluginHandleSequenceResetup(in_data, out_data, params, output);
        case PF_Cmd_SEQUENCE_FLATTEN:
            return PluginHandleSequenceFlatten(in_data, out_data, params, output);
        case PF_Cmd_SEQUENCE_SETDOWN:
            return PluginHandleSequenceSetdown(in_data, out_data, params, output);
        case PF_Cmd_SMART_PRE_RENDER:
            return PluginHandleSmartPreRender(in_data, out_data, extra);
        case PF_Cmd_SMART_RENDER:
            return PluginHandleSmartRender(in_data, out_data, extra);
        case PF_Cmd_RENDER:
            return PluginHandleLegacyRender(in_data, out_data, params, output);
        default:
            return PF_Err_NONE;
    }
}

} // extern "C"
