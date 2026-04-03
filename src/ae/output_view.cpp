#include "output_view.h"

#include <algorithm>
#include <cmath>

namespace {

RgbImageView composite_input_view(const RgbImageView& input, const FrameRenderOutputs& outputs)
{
    if (outputs.scene_r.empty() || outputs.scene_g.empty() || outputs.scene_b.empty()) {
        return input;
    }

    return {
        outputs.scene_r.data(),
        outputs.scene_g.data(),
        outputs.scene_b.data(),
        outputs.width,
        outputs.height,
    };
}

void clear_output(MutableRgbImageView output)
{
    const int np = output.width * output.height;
    for (int i = 0; i < np; ++i) {
        output.r[i] = 0.0f;
        output.g[i] = 0.0f;
        output.b[i] = 0.0f;
    }
}

void copy_input(const RgbImageView& input, MutableRgbImageView output)
{
    const int np = input.width * input.height;
    for (int i = 0; i < np; ++i) {
        output.r[i] = input.r[i];
        output.g[i] = input.g[i];
        output.b[i] = input.b[i];
    }
}

void add_buffers(const std::vector<float>& add_r,
                 const std::vector<float>& add_g,
                 const std::vector<float>& add_b,
                 MutableRgbImageView output)
{
    const int np = output.width * output.height;
    for (int i = 0; i < np; ++i) {
        output.r[i] += add_r[i];
        output.g[i] += add_g[i];
        output.b[i] += add_b[i];
    }
}

void draw_sources(const FrameRenderSettings& settings,
                  const FrameRenderOutputs& outputs,
                  MutableRgbImageView output,
                  bool diagnostics_mode)
{
    float fov_h = 0.0f;
    float fov_v = 0.0f;
    if (!compute_camera_fov(settings, output.width, output.height, fov_h, fov_v)) {
        return;
    }
    const float tan_half_h = std::tan(fov_h * 0.5f);
    const float tan_half_v = std::tan(fov_v * 0.5f);
    const int block = std::max(3, settings.downsample);

    for (const BrightPixel& source : outputs.sources) {
        const float ndc_x = std::tan(source.angle_x) / (2.0f * tan_half_h);
        const float ndc_y = std::tan(source.angle_y) / (2.0f * tan_half_v);
        const int px = (int)((ndc_x + 0.5f) * output.width);
        const int py = (int)((ndc_y + 0.5f) * output.height);

        const int x0 = std::max(0, px - block / 2);
        const int y0 = std::max(0, py - block / 2);
        const int x1 = std::min(output.width, x0 + block);
        const int y1 = std::min(output.height, y0 + block);

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const int i = y * output.width + x;
                if (diagnostics_mode) {
                    output.r[i] = std::max(output.r[i], source.r);
                    output.g[i] *= 0.25f;
                    output.b[i] *= 0.25f;
                } else {
                    output.r[i] = source.r;
                    output.g[i] = source.g;
                    output.b[i] = source.b;
                }
            }
        }
    }
}

} // namespace

FrameRenderPlan build_output_view_render_plan(AeOutputView view)
{
    switch (view) {
        case AeOutputView::Composite:
            return {
                .need_scene_output = true,
                .need_source_output = false,
                .need_flare = true,
                .need_bloom = true,
                .need_haze = true,
                .need_starburst = true,
            };

        case AeOutputView::FlareOnly:
            return {
                .need_scene_output = false,
                .need_source_output = false,
                .need_flare = true,
                .need_bloom = false,
                .need_haze = true,
                .need_starburst = true,
            };

        case AeOutputView::BloomOnly:
            return {
                .need_scene_output = false,
                .need_source_output = false,
                .need_flare = false,
                .need_bloom = true,
                .need_haze = false,
                .need_starburst = false,
            };

        case AeOutputView::Sources:
            return {
                .need_scene_output = false,
                .need_source_output = true,
                .need_flare = false,
                .need_bloom = false,
                .need_haze = false,
                .need_starburst = false,
            };

        case AeOutputView::Diagnostics:
            return {
                .need_scene_output = true,
                .need_source_output = true,
                .need_flare = false,
                .need_bloom = false,
                .need_haze = false,
                .need_starburst = false,
            };
    }

    return {};
}

bool compose_output_view(
    AeOutputView view,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    const FrameRenderOutputs& outputs,
    MutableRgbImageView output)
{
    if (!input.r || !input.g || !input.b ||
        !output.r || !output.g || !output.b ||
        input.width <= 0 || input.height <= 0 ||
        input.width != output.width || input.height != output.height ||
        outputs.width != output.width || outputs.height != output.height) {
        return false;
    }

    const RgbImageView composite_input = composite_input_view(input, outputs);

    switch (view) {
        case AeOutputView::Composite:
            copy_input(composite_input, output);
            add_buffers(outputs.flare_r, outputs.flare_g, outputs.flare_b, output);
            add_buffers(outputs.bloom_r, outputs.bloom_g, outputs.bloom_b, output);
            add_buffers(outputs.haze_r, outputs.haze_g, outputs.haze_b, output);
            add_buffers(outputs.starburst_r, outputs.starburst_g, outputs.starburst_b, output);
            return true;

        case AeOutputView::FlareOnly:
            clear_output(output);
            add_buffers(outputs.flare_r, outputs.flare_g, outputs.flare_b, output);
            add_buffers(outputs.haze_r, outputs.haze_g, outputs.haze_b, output);
            add_buffers(outputs.starburst_r, outputs.starburst_g, outputs.starburst_b, output);
            return true;

        case AeOutputView::BloomOnly:
            clear_output(output);
            add_buffers(outputs.bloom_r, outputs.bloom_g, outputs.bloom_b, output);
            return true;

        case AeOutputView::Sources:
            clear_output(output);
            draw_sources(settings, outputs, output, false);
            return true;

        case AeOutputView::Diagnostics:
            copy_input(composite_input, output);
            draw_sources(settings, outputs, output, true);
            return true;
    }

    return false;
}
