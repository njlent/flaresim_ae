#include "plugin_entry.h"
#include "plugin_version.h"

extern "C" {

FLARESIM_AE_EXPORT PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr inPtr,
    PF_PluginDataCB2 inPluginDataCallBackPtr,
    SPBasicSuite*,
    const char*,
    const char*)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        FLARESIM_AE_EFFECT_NAME,
        FLARESIM_AE_EFFECT_MATCH_NAME,
        FLARESIM_AE_EFFECT_CATEGORY,
        AE_RESERVED_INFO,
        "EffectMain",
        FLARESIM_AE_SUPPORT_URL);
    return result;
}

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
        case PF_Cmd_PARAMS_SETUP:
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
