#include "render_frame.h"

#include "source_extract.h"

#include <cmath>

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs)
{
    if (!input.r || !input.g || !input.b || input.width <= 0 || input.height <= 0) {
        return false;
    }
    if (lens.num_surfaces() <= 0 || lens.focal_length <= 0.0f) {
        return false;
    }

    outputs.width = input.width;
    outputs.height = input.height;

    const size_t np = (size_t)input.width * input.height;
    outputs.flare_r.assign(np, 0.0f);
    outputs.flare_g.assign(np, 0.0f);
    outputs.flare_b.assign(np, 0.0f);
    outputs.bloom_r.assign(np, 0.0f);
    outputs.bloom_g.assign(np, 0.0f);
    outputs.bloom_b.assign(np, 0.0f);
    outputs.ghost_backend = GhostRenderBackend::CPU;

    const float fov_h = settings.fov_h_deg * 3.14159265358979323846f / 180.0f;
    const float aspect = (float)input.width / (float)input.height;
    const float fov_v = 2.0f * std::atan(std::tan(fov_h * 0.5f) / aspect);

    outputs.sources = extract_bright_pixels(
        input,
        settings.threshold,
        settings.downsample,
        fov_h,
        fov_v);

    limit_bright_pixels(outputs.sources, static_cast<size_t>(settings.max_sources));

    if (!outputs.sources.empty()) {
        GhostConfig ghost {};
        ghost.ray_grid = settings.ray_grid;
        ghost.min_intensity = settings.min_ghost;
        ghost.gain = settings.flare_gain;
        ghost.ghost_normalize = settings.ghost_normalize;
        ghost.max_area_boost = settings.max_area_boost;

        render_ghosts(
            lens,
            outputs.sources,
            fov_h,
            fov_v,
            outputs.flare_r.data(),
            outputs.flare_g.data(),
            outputs.flare_b.data(),
            input.width,
            input.height,
            ghost,
            &outputs.ghost_backend);
    }

    if (settings.bloom.strength > 0.0f) {
        const MutableRgbImageView bloom_out {
            outputs.bloom_r.data(),
            outputs.bloom_g.data(),
            outputs.bloom_b.data(),
            input.width,
            input.height,
        };
        generate_bloom(input, bloom_out, settings.bloom);
    }

    return true;
}
