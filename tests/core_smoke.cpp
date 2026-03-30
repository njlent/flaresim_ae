#include "ghost.h"
#include "lens.h"
#include "trace.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

std::filesystem::path source_root()
{
    return std::filesystem::path(FLARESIM_AE_SOURCE_DIR);
}

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "smoke failure: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main()
{
    const auto lens_path = source_root() / "assets/lenses/space55/doublegauss.lens";

    LensSystem lens;
    require(lens.load(lens_path.string().c_str()), "expected bundled lens to load");
    require(lens.num_surfaces() > 0, "expected lens surfaces");
    require(lens.focal_length > 0.0f, "expected positive focal length");

    const auto pairs = enumerate_ghost_pairs(lens);
    require(!pairs.empty(), "expected at least one ghost pair");

    BrightPixel source{};
    source.angle_x = 0.0f;
    source.angle_y = 0.0f;
    source.r = 10.0f;
    source.g = 10.0f;
    source.b = 10.0f;

    std::vector<BrightPixel> sources{source};
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    std::vector<float> out_r(kWidth * kHeight, 0.0f);
    std::vector<float> out_g(kWidth * kHeight, 0.0f);
    std::vector<float> out_b(kWidth * kHeight, 0.0f);

    GhostConfig config;
    config.ray_grid = 8;
    config.gain = 1000.0f;

    const float fov_h = 60.0f * static_cast<float>(M_PI) / 180.0f;
    const float fov_v = 2.0f * std::atan(std::tan(fov_h * 0.5f) / (16.0f / 9.0f));

    render_ghosts(lens, sources, fov_h, fov_v, out_r.data(), out_g.data(), out_b.data(),
                  kWidth, kHeight, config);

    float total_energy = 0.0f;
    for (float v : out_r) total_energy += v;
    for (float v : out_g) total_energy += v;
    for (float v : out_b) total_energy += v;

    require(std::isfinite(total_energy), "expected finite output energy");
    require(total_energy > 0.0f, "expected non-zero ghost energy");

    std::cout << "loaded lens: " << lens.name << '\n';
    std::cout << "ghost pairs: " << pairs.size() << '\n';
    std::cout << "total energy: " << total_energy << '\n';
    return 0;
}
