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
    // - translate AE params into AeParameterState
    // - resolve plugin asset root / bundled lens directory
    // - checkout AE source/output worlds
    // - call render_frame_to_pixels() for PF_Pixel8 / PF_Pixel16 / PF_PixelFloat
    return PF_Err_NONE;
}

PF_Err PluginHandleLegacyRender(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*)
{
    return PF_Err_NONE;
}
