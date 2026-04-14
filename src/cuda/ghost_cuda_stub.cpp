#include "ghost_cuda.h"

void GpuBufferCache::release()
{
    launch_buckets.clear();
    lens_key = 0;
    spec_key = 0;
    setup_key = 0;
}

bool cuda_ghost_renderer_compiled()
{
    return false;
}

bool cuda_ghost_renderer_available(std::string* reason)
{
    if (reason) {
        *reason = "FlareSim was built without CUDA support.";
    }
    return false;
}

bool launch_ghost_cuda(
    const LensSystem&,
    const GhostRenderSetup&,
    const std::vector<BrightPixel>&,
    float,
    float,
    float*,
    float*,
    float*,
    int,
    int,
    const GhostConfig&,
    GpuBufferCache&,
    std::string* out_error)
{
    if (out_error) {
        *out_error = "FlareSim was built without CUDA support.";
    }
    return false;
}
