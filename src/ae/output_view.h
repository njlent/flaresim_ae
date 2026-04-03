#pragma once

#include "image.h"
#include "parameter_state.h"
#include "render_frame.h"

FrameRenderPlan build_output_view_render_plan(AeOutputView view);

bool compose_output_view(
    AeOutputView view,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    const FrameRenderOutputs& outputs,
    MutableRgbImageView output
);
