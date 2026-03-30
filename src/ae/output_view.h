#pragma once

#include "image.h"
#include "parameter_state.h"
#include "render_frame.h"

bool compose_output_view(
    AeOutputView view,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    const FrameRenderOutputs& outputs,
    MutableRgbImageView output
);
