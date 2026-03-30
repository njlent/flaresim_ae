#include "asset_root.h"
#include "bloom.h"
#include "builtin_lenses.h"
#include "frame_bridge.h"
#include "ghost_cuda.h"
#include "ghost.h"
#include "lens.h"
#include "lens_resolution.h"
#include "output_view.h"
#include "param_schema.h"
#include "parameter_state.h"
#include "pixel_convert.h"
#include "render_frame.h"
#include "source_extract.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string repo_path(const char* rel)
{
    return std::string(FLARESIM_REPO_ROOT) + "/" + rel;
}

void test_lens_load()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    const bool ok = lens.load(path.c_str());
    assert(ok);
    assert(lens.num_surfaces() > 0);
    const auto pairs = enumerate_ghost_pairs(lens);
    assert((int)pairs.size() == (lens.num_surfaces() * (lens.num_surfaces() - 1)) / 2);
}

void test_source_extract()
{
    std::vector<float> r(16, 0.0f);
    std::vector<float> g(16, 0.0f);
    std::vector<float> b(16, 0.0f);
    r[5] = 4.0f;
    g[5] = 2.0f;
    b[5] = 1.0f;

    const RgbImageView img {r.data(), g.data(), b.data(), 4, 4};
    const auto sources = extract_bright_pixels(img, 1.0f, 1, 1.0f, 1.0f);
    assert(sources.size() == 1);
    assert(sources[0].r > 0.0f);
    assert(std::abs(sources[0].angle_x) < 1.0f);
}

void test_source_limit()
{
    std::vector<BrightPixel> sources = {
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 1.0f, .g = 1.0f, .b = 1.0f},
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 2.0f, .g = 2.0f, .b = 2.0f},
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 10.0f, .g = 10.0f, .b = 10.0f},
    };

    limit_bright_pixels(sources, 2);

    assert(sources.size() == 2);
    assert(sources[0].r >= sources[1].r);
    assert(sources[0].r == 10.0f);
    assert(sources[1].r == 2.0f);

    limit_bright_pixels(sources, 0);
    assert(sources.size() == 2);
}

void test_bloom()
{
    std::vector<float> src_r(64, 0.0f);
    std::vector<float> src_g(64, 0.0f);
    std::vector<float> src_b(64, 0.0f);
    src_r[27] = 8.0f;
    src_g[27] = 8.0f;
    src_b[27] = 8.0f;

    std::vector<float> out_r(64, 0.0f);
    std::vector<float> out_g(64, 0.0f);
    std::vector<float> out_b(64, 0.0f);

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};
    const MutableRgbImageView output {out_r.data(), out_g.data(), out_b.data(), 8, 8};
    const BloomConfig config {
        .threshold = 1.0f,
        .strength = 1.0f,
        .radius = 0.08f,
        .passes = 1,
        .octaves = 1,
        .chromatic = false,
    };

    generate_bloom(input, output, config);

    float sum = 0.0f;
    for (float v : out_r) {
        sum += v;
    }
    assert(sum > 0.0f);
}

void test_render_frame()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.0f);
    std::vector<float> src_g(64, 0.0f);
    std::vector<float> src_b(64, 0.0f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 100.0f;
    settings.bloom.threshold = 1.0f;
    settings.bloom.strength = 0.5f;
    settings.bloom.radius = 0.08f;
    settings.bloom.passes = 1;
    settings.bloom.octaves = 1;
    settings.bloom.chromatic = false;

    FrameRenderOutputs outputs;
    assert(render_frame(lens, input, settings, outputs));
    assert(outputs.sources.size() == 1);
    assert(outputs.ghost_backend == GhostRenderBackend::CPU ||
           outputs.ghost_backend == GhostRenderBackend::CUDA);
    assert(std::string(ghost_render_backend_name(outputs.ghost_backend)).size() > 0);

    float flare_sum = 0.0f;
    float bloom_sum = 0.0f;
    for (float v : outputs.flare_r) {
        flare_sum += v;
    }
    for (float v : outputs.bloom_r) {
        bloom_sum += v;
    }
    assert(flare_sum > 0.0f);
    assert(bloom_sum > 0.0f);
}

void test_cuda_backend_api()
{
    assert(std::string(ghost_render_backend_name(GhostRenderBackend::CPU)) == "CPU");
    assert(std::string(ghost_render_backend_name(GhostRenderBackend::CUDA)) == "CUDA");

    std::string reason;
    const bool available = cuda_ghost_renderer_available(&reason);
    if (!cuda_ghost_renderer_compiled()) {
        assert(!available);
        assert(!reason.empty());
    }
}

void test_ae_adapter_bits()
{
    assert(builtin_lens_count() >= 1375);
    assert(builtin_lens_manufacturer_count() >= 50);
    const auto* lens = find_builtin_lens("double-gauss");
    assert(lens);
    assert(lens->manufacturer_index == 0);
    assert(std::string(lens->relative_path).find("doublegauss.lens") != std::string::npos);

    AeParameterState state {};
    state.fov_h_deg = 42.0f;
    state.threshold = 2.5f;
    state.downsample = 2;
    state.ray_grid = 8;
    state.flare_gain = 250.0f;
    state.bloom.strength = 0.75f;

    const auto settings = build_frame_render_settings(state);
    assert(std::abs(settings.fov_h_deg - 42.0f) < 1e-6f);
    assert(std::abs(settings.threshold - 2.5f) < 1e-6f);
    assert(settings.downsample == 2);
    assert(settings.ray_grid == 8);
    assert(settings.max_sources == 64);
    assert(std::abs(settings.flare_gain - 250.0f) < 1e-6f);
    assert(std::abs(settings.bloom.strength - 0.75f) < 1e-6f);

    state.ray_grid = 2048;
    state.max_sources = 4096;
    const auto unclamped = build_frame_render_settings(state);
    assert(unclamped.ray_grid == 2048);
    assert(unclamped.max_sources == 4096);

    state.max_sources = 0;
    const auto unlimited = build_frame_render_settings(state);
    assert(unlimited.max_sources == 0);

    std::string asset_root;
    assert(find_flaresim_asset_root(repo_path("src/ae"), asset_root));
    assert(asset_root == std::string(FLARESIM_REPO_ROOT));
    assert(is_flaresim_asset_root(asset_root));

    std::string resolved;
    assert(resolve_lens_path(state.lens, asset_root, resolved));
    assert(resolved.find("doublegauss.lens") != std::string::npos);

    LensSystem loaded;
    assert(load_selected_lens(state.lens, asset_root, loaded));
    assert(loaded.num_surfaces() > 0);
}

void test_output_views()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.1f);
    std::vector<float> src_g(64, 0.1f);
    std::vector<float> src_b(64, 0.1f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 100.0f;
    settings.bloom.threshold = 1.0f;
    settings.bloom.strength = 0.5f;
    settings.bloom.radius = 0.08f;
    settings.bloom.passes = 1;
    settings.bloom.octaves = 1;
    settings.bloom.chromatic = false;

    FrameRenderOutputs outputs;
    assert(render_frame(lens, input, settings, outputs));

    std::vector<float> out_r(64, 0.0f);
    std::vector<float> out_g(64, 0.0f);
    std::vector<float> out_b(64, 0.0f);
    MutableRgbImageView output {out_r.data(), out_g.data(), out_b.data(), 8, 8};

    assert(compose_output_view(AeOutputView::FlareOnly, input, settings, outputs, output));
    float flare_sum = 0.0f;
    for (float v : out_r) flare_sum += v;
    assert(flare_sum > 0.0f);

    std::fill(out_r.begin(), out_r.end(), 0.0f);
    std::fill(out_g.begin(), out_g.end(), 0.0f);
    std::fill(out_b.begin(), out_b.end(), 0.0f);
    assert(compose_output_view(AeOutputView::BloomOnly, input, settings, outputs, output));
    float bloom_sum = 0.0f;
    for (float v : out_r) bloom_sum += v;
    assert(bloom_sum > 0.0f);

    std::fill(out_r.begin(), out_r.end(), 0.0f);
    std::fill(out_g.begin(), out_g.end(), 0.0f);
    std::fill(out_b.begin(), out_b.end(), 0.0f);
    assert(compose_output_view(AeOutputView::Sources, input, settings, outputs, output));
    float source_sum = 0.0f;
    for (float v : out_r) source_sum += v;
    assert(source_sum > 0.0f);
}

void test_pixel_convert()
{
    const FloatPixel hdr {1.0f, 1.5f, 0.5f, 2.25f};

    const auto p8 = pack_pixel8(hdr);
    const auto p16 = pack_pixel16(hdr);
    const auto p32 = pack_pixel32(hdr);

    assert(p8.red == 255);
    assert(p16.red == 32768);
    assert(std::abs(p32.red - 1.5f) < 1e-6f);
    assert(std::abs(p32.blue - 2.25f) < 1e-6f);

    const auto f8 = unpack_pixel(p8);
    const auto f16 = unpack_pixel(p16);
    const auto f32 = unpack_pixel(p32);

    assert(f8.red <= 1.0f);
    assert(f16.red <= 1.0f);
    assert(std::abs(f32.red - 1.5f) < 1e-6f);
    assert(std::abs(f32.blue - 2.25f) < 1e-6f);
}

void test_frame_bridge()
{
    std::string asset_root;
    assert(find_flaresim_asset_root(FLARESIM_REPO_ROOT, asset_root));

    std::vector<AePixel8Like> src8(64);
    std::vector<AePixel8Like> dst8(64);
    std::vector<AePixel16Like> src16(64);
    std::vector<AePixel16Like> dst16(64);
    std::vector<AePixel32Like> src32(64);
    std::vector<AePixel32Like> dst32(64);

    src8[27] = pack_pixel8(FloatPixel {1.0f, 0.9f, 0.7f, 0.4f});
    src16[27] = pack_pixel16(FloatPixel {1.0f, 0.9f, 0.7f, 0.4f});
    src32[27] = pack_pixel32(FloatPixel {1.0f, 12.0f, 8.0f, 4.0f});

    AeParameterState state {};
    state.threshold = 0.25f;
    state.downsample = 1;
    state.ray_grid = 4;
    state.flare_gain = 100.0f;
    state.bloom.threshold = 0.25f;
    state.bloom.strength = 0.5f;
    state.bloom.radius = 0.08f;
    state.bloom.passes = 1;
    state.bloom.octaves = 1;
    state.bloom.chromatic = false;

    assert(render_frame_to_pixels(asset_root, state, src8.data(), dst8.data(), 8, 8));
    assert(render_frame_to_pixels(asset_root, state, src16.data(), dst16.data(), 8, 8));
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));

    FloatImageBuffer out8;
    FloatImageBuffer out16;
    FloatImageBuffer out32;
    assert(unpack_image(dst8.data(), 8, 8, out8));
    assert(unpack_image(dst16.data(), 8, 8, out16));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float sum8 = 0.0f;
    float sum16 = 0.0f;
    float sum32 = 0.0f;
    for (float v : out8.r) sum8 += v;
    for (float v : out16.r) sum16 += v;
    for (float v : out32.r) sum32 += v;

    assert(sum8 > 0.0f);
    assert(sum16 > 0.0f);
    assert(sum32 > 0.0f);
    assert(std::abs(out32.alpha[27] - 1.0f) < 1e-6f);

    state.view = AeOutputView::FlareOnly;
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float max32 = 0.0f;
    for (float v : out32.r) {
        max32 = std::max(max32, v);
    }
    assert(max32 > 1.0f);
}

void test_param_schema()
{
    assert(parameter_count() > 100);
    assert(lens_section_start_param() == PARAM_LENS_SECTION_START);
    assert(lens_section_end_param() + 1 == flare_section_start_param());
    assert(flare_section_start_param() + 1 == flare_gain_param());
    assert(flare_section_end_param() + 1 == view_mode_param());
    assert(mask_layer_param() + 1 == parameter_count());

    const std::string legacy_lens_popup = build_lens_preset_popup_string();
    const std::string manufacturer_popup = build_lens_manufacturer_popup_string();
    const std::string grouped_lens_popup = build_lens_popup_string_for_manufacturer(0);
    const std::string view_popup = build_output_view_popup_string();
    assert(legacy_lens_popup.find("Double Gauss") != std::string::npos);
    assert(manufacturer_popup.find("Canon") != std::string::npos);
    assert(grouped_lens_popup.find("Double Gauss") != std::string::npos);
    assert(view_popup.find("Flare Only") != std::string::npos);

    const char* canon_lens_id = "canon-1-4x-tc-canon-extender-ef1-4x-iii";
    AeUiParameterState ui {};
    ui.legacy_lens_preset_index = lens_popup_index_for_builtin("double-gauss");
    ui.lens_manufacturer_index = lens_manufacturer_popup_index_for_builtin(canon_lens_id);
    ui.lens_model_index = lens_model_popup_index_for_builtin(canon_lens_id);
    ui.view_mode_index = output_view_popup_index(AeOutputView::Diagnostics);
    ui.flare_gain = 250.0f;
    ui.threshold = 1.5f;
    ui.ray_grid = 8;
    ui.downsample = 2;

    AeParameterState state {};
    assert(apply_ui_parameter_state(ui, state));
    assert(std::string(state.lens.builtin_id) == canon_lens_id);
    assert(state.view == AeOutputView::Diagnostics);
    assert(std::abs(state.flare_gain - 250.0f) < 1e-6f);
    assert(std::abs(state.threshold - 1.5f) < 1e-6f);
    assert(state.ray_grid == 8);
    assert(state.downsample == 2);

    AeUiParameterState legacy_ui {};
    legacy_ui.legacy_lens_preset_index = lens_popup_index_for_builtin("cooke-triplet");
    legacy_ui.lens_manufacturer_index = default_lens_manufacturer_popup_index();
    legacy_ui.lens_model_index = default_lens_model_popup_index();
    legacy_ui.view_mode_index = output_view_popup_index(AeOutputView::Composite);

    AeParameterState legacy_state {};
    assert(apply_ui_parameter_state(legacy_ui, legacy_state));
    assert(std::string(legacy_state.lens.builtin_id) == "cooke-triplet");
}

} // namespace

int main()
{
    test_lens_load();
    test_source_extract();
    test_source_limit();
    test_bloom();
    test_render_frame();
    test_cuda_backend_api();
    test_ae_adapter_bits();
    test_output_views();
    test_pixel_convert();
    test_frame_bridge();
    test_param_schema();
    std::cout << "flaresim_core_smoke: ok\n";
    return 0;
}
