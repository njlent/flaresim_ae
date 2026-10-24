// Renders a raw float32 RGB image with flaresim-ps settings text through the
// same runtime path the Photoshop filter uses. The Photoshop host test
// compares Photoshop's output against this.
//
// usage: flaresim_ps_reference <in.f32> <out.f32> <width> <height> <settings.txt>

#include "frame_bridge.h"
#include "ps_settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 6) {
        std::fprintf(stderr, "usage: %s <in.f32> <out.f32> <width> <height> <settings.txt>\n", argv[0]);
        return 2;
    }
    const int width = std::atoi(argv[3]);
    const int height = std::atoi(argv[4]);
    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0) {
        return 2;
    }

    std::vector<float> pixels(np * 3u);
    {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(float)))) {
            std::fprintf(stderr, "could not read %s\n", argv[1]);
            return 1;
        }
    }

    std::ifstream settings_file(argv[5], std::ios::binary);
    std::ostringstream settings_text;
    settings_text << settings_file.rdbuf();
    AeUiParameterState ui = ps_default_ui_state();
    if (!ps_deserialize_settings(settings_text.str(), ui)) {
        std::fprintf(stderr, "bad settings text\n");
        return 1;
    }
    AeParameterState state {};
    if (!ps_build_parameter_state(ui, state)) {
        std::fprintf(stderr, "invalid settings\n");
        return 1;
    }

    FloatImageBuffer input;
    input.width = width;
    input.height = height;
    input.r.resize(np);
    input.g.resize(np);
    input.b.resize(np);
    input.alpha.assign(np, 1.0f);
    for (std::size_t i = 0; i < np; ++i) {
        input.r[i] = pixels[i * 3 + 0];
        input.g[i] = pixels[i * 3 + 1];
        input.b[i] = pixels[i * 3 + 2];
    }

    FloatImageBuffer output;
    if (!render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, output)) {
        std::fprintf(stderr, "render failed\n");
        return 1;
    }
    for (std::size_t i = 0; i < np; ++i) {
        pixels[i * 3 + 0] = output.r[i];
        pixels[i * 3 + 1] = output.g[i];
        pixels[i * 3 + 2] = output.b[i];
    }
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(float)));
    return out ? 0 : 1;
}
