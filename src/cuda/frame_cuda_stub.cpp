#include "frame_cuda.h"

void GpuFrameRenderCache::release()
{
    ghost_cache.release();
    d_scene_bgra = nullptr;
    scene_floats = 0;
    d_bloom_r = nullptr;
    d_bloom_g = nullptr;
    d_bloom_b = nullptr;
    bloom_floats = 0;
    d_haze_r = nullptr;
    d_haze_g = nullptr;
    d_haze_b = nullptr;
    haze_floats = 0;
    d_starburst_r = nullptr;
    d_starburst_g = nullptr;
    d_starburst_b = nullptr;
    starburst_floats = 0;
    d_work0_r = nullptr;
    d_work0_g = nullptr;
    d_work0_b = nullptr;
    work0_floats = 0;
    d_work1_r = nullptr;
    d_work1_g = nullptr;
    d_work1_b = nullptr;
    work1_floats = 0;
    d_candidates = nullptr;
    candidate_capacity = 0;
    d_sources = nullptr;
    source_capacity = 0;
    d_starburst_psf = nullptr;
    starburst_psf_floats = 0;
    starburst_psf_size = 0;
    ghost_setup_key = 0;
    has_ghost_setup = false;
    ghost_setup = {};
    starburst_psf_key = 0;
    starburst_psf = {};
}

bool render_frame_cuda_bgra128(const LensSystem&,
                               const FrameRenderSettings&,
                               AeOutputView,
                               const float*,
                               float*,
                               int,
                               int,
                               int,
                               int,
                               const float*,
                               int,
                               GpuFrameRenderCache&,
                               GhostRenderBackend*,
                               std::string* out_error)
{
    if (out_error) {
        *out_error = "FlareSim was built without CUDA support.";
    }
    return false;
}
