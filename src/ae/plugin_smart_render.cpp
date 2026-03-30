#include "plugin_entry.h"

PF_Err PluginHandleSmartPreRender(PF_InData*, PF_OutData*, void*)
{
    // TODO:
    // - checkout source + optional mask layer
    // - compute result/max rects
    // - stash minimal pre-render state
    return PF_Err_NONE;
}

PF_Err PluginHandleSmartRender(PF_InData*, PF_OutData*, void*)
{
    // TODO:
    // - translate AE params into AeParameterState / FrameRenderSettings
    // - unpack AE pixels to float planes
    // - call render_frame()
    // - composite selected output view back into AE buffers
    return PF_Err_NONE;
}

PF_Err PluginHandleLegacyRender(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*)
{
    return PF_Err_NONE;
}
