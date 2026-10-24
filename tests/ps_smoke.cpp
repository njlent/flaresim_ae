#include "builtin_lenses.h"
#include "frame_bridge.h"
#include "ps_image.h"
#include "ps_presets.h"
#include "ps_settings.h"
#include "render_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

bool near(double a, double b, double eps)
{
    return std::abs(a - b) <= eps;
}

FloatImageBuffer make_image(int width, int height, float r, float g, float b, float a)
{
    FloatImageBuffer image;
    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.width = width;
    image.height = height;
    image.r.assign(np, r);
    image.g.assign(np, g);
    image.b.assign(np, b);
    image.alpha.assign(np, a);
    return image;
}

void test_settings_round_trip()
{
    AeUiParameterState state = ps_default_ui_state();
    CHECK(!ps_lens_id(state).empty());

    const BuiltinLensDescriptor* lenses = builtin_lenses();
    const std::size_t lens_count = builtin_lens_count();
    CHECK(lens_count > 10);
    const std::string target_lens = lenses[lens_count / 2].id;
    CHECK(ps_set_lens_id(state, target_lens));

    state.fov_h_deg = 63.25f;
    state.flare_gain = 12345.5f;
    state.aperture_blades = 7;
    state.manual_source_enabled = true;
    state.manual_source_x = -0.375f;
    state.spectral_samples_index = 4;
    state.ghost_cleanup_mode_index = 3;
    state.view_mode_index = 2;
    state.threshold = 0.0001f;

    const std::string text = ps_serialize_settings(state);
    CHECK(text.rfind("flaresim-ps 1\n", 0) == 0);
    CHECK(text.find("ghost_cleanup=Sharp + Blur\n") != std::string::npos);
    CHECK(text.find("view=Flare Only\n") != std::string::npos);

    AeUiParameterState restored = ps_default_ui_state();
    CHECK(ps_deserialize_settings(text, restored));
    CHECK(ps_lens_id(restored) == target_lens);
    for (std::size_t i = 0; i < ps_param_count(); ++i) {
        const PsParamDesc& desc = ps_param_table()[i];
        const double expected = ps_get_param_value(desc, state);
        const double actual = ps_get_param_value(desc, restored);
        if (expected != actual) {
            std::fprintf(stderr, "round trip mismatch for %s: %g != %g\n", desc.key, expected, actual);
        }
        CHECK(expected == actual);
    }

    AeParameterState render_state {};
    CHECK(ps_build_parameter_state(restored, render_state));
    CHECK(std::strcmp(render_state.lens.builtin_id, target_lens.c_str()) == 0);
    CHECK(render_state.spectral_samples == 9);
    CHECK(render_state.ghost_cleanup_mode == GhostCleanupMode::SharpAdaptivePlusBlur);
    CHECK(render_state.view == AeOutputView::FlareOnly);
    CHECK(!render_state.pupil_jitter_auto_seed);
}

void test_settings_parsing_rules()
{
    AeUiParameterState state = ps_default_ui_state();
    CHECK(!ps_deserialize_settings("not settings\nflare_gain=1\n", state));
    CHECK(state.flare_gain == ps_default_ui_state().flare_gain);

    const std::string text =
        "flaresim-ps 1\n"
        "unknown_key=42\n"
        "ray_grid=-5\n"
        "downsample = 3 \n"
        "spectral_samples=2\n"
        "pupil_jitter=halton\n"
        "manual_source=TRUE\n"
        "lens=definitely-not-a-lens\n"
        "flare_gain=abc\n";
    CHECK(ps_deserialize_settings(text, state));
    CHECK(state.ray_grid == 1);
    CHECK(state.downsample == 3);
    CHECK(state.spectral_samples_index == 2);
    CHECK(state.pupil_jitter_mode_index == 3);
    CHECK(state.manual_source_enabled);
    CHECK(ps_lens_id(state) == ps_lens_id(ps_default_ui_state()));
    CHECK(state.flare_gain == ps_default_ui_state().flare_gain);

    AeParameterState render_state {};
    CHECK(ps_build_parameter_state(ps_default_ui_state(), render_state));
}

template <typename T>
void check_pixel_round_trip(int depth)
{
    const int width = 5;
    const int height = 3;
    const int planes = 4;
    const int row_bytes = width * planes * static_cast<int>(sizeof(T)) + 12;
    std::vector<unsigned char> storage(static_cast<std::size_t>(row_bytes) * height, 0);

    auto sample = [&](int x, int y, int p) -> T* {
        return reinterpret_cast<T*>(storage.data() + static_cast<std::size_t>(y) * row_bytes) + x * planes + p;
    };

    const float max_value = depth == 8 ? 255.0f : depth == 16 ? 32768.0f : 1.0f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float a = (x + 1) / static_cast<float>(width);
            *sample(x, y, 0) = static_cast<T>(max_value * 0.5f);
            *sample(x, y, 1) = static_cast<T>(max_value * 0.25f);
            *sample(x, y, 2) = static_cast<T>(max_value);
            *sample(x, y, 3) = static_cast<T>(max_value * a);
        }
    }
    if (depth == 32) {
        *sample(2, 1, 0) = static_cast<T>(12.5f);
    }

    PsPixelBuffer buffer {storage.data(), row_bytes, depth, planes, width, height};
    PsChannelMap map {3};
    FloatImageBuffer image = make_image(width + 2, height + 1, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(ps_read_pixels(buffer, map, true, image, 1, 1));

    const std::size_t first = static_cast<std::size_t>(1) * image.width + 1;
    const double tolerance = depth == 8 ? 2.0 / 255.0 : 1.0e-4;
    CHECK(near(image.alpha[first], 1.0 / width, tolerance));
    CHECK(near(image.r[first], 0.5 / width, tolerance));
    CHECK(near(image.b[first], 1.0 / width, tolerance));

    std::vector<unsigned char> out_storage(storage.size(), 0);
    PsPixelBuffer out {out_storage.data(), row_bytes, depth, planes, width, height};
    CHECK(ps_write_pixels(image, 1, 1, map, true, out));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int p = 0; p < planes; ++p) {
                const auto* in_row = reinterpret_cast<const T*>(storage.data() + static_cast<std::size_t>(y) * row_bytes);
                const auto* out_row = reinterpret_cast<const T*>(out_storage.data() + static_cast<std::size_t>(y) * row_bytes);
                const double in_value = static_cast<double>(in_row[x * planes + p]);
                const double out_value = static_cast<double>(out_row[x * planes + p]);
                const double allowed = depth == 32 ? 1.0e-4 * std::max(1.0, std::abs(in_value)) : 2.0;
                CHECK(near(in_value, out_value, allowed));
            }
        }
    }
}

void test_pixel_round_trips()
{
    check_pixel_round_trip<std::uint8_t>(8);
    check_pixel_round_trip<std::uint16_t>(16);
    check_pixel_round_trip<float>(32);

    // 8/16-bit writes clamp, 32-bit writes keep over-range values.
    FloatImageBuffer hot = make_image(1, 1, 7.0f, -1.0f, 0.5f, 1.0f);
    std::uint16_t wide[3] {};
    PsPixelBuffer wide_buffer {wide, 6, 16, 3, 1, 1};
    CHECK(ps_write_pixels(hot, 0, 0, PsChannelMap {}, false, wide_buffer));
    CHECK(wide[0] == 32768 && wide[1] == 0 && wide[2] == 16384);

    float deep[3] {};
    PsPixelBuffer deep_buffer {deep, 12, 32, 3, 1, 1};
    CHECK(ps_write_pixels(hot, 0, 0, PsChannelMap {}, false, deep_buffer));
    CHECK(deep[0] == 7.0f && deep[2] == 0.5f);

    PsPixelBuffer bad_depth {deep, 12, 12, 3, 1, 1};
    CHECK(!ps_write_pixels(hot, 0, 0, PsChannelMap {}, false, bad_depth));
}

void test_preview_proxy()
{
    FloatImageBuffer full = make_image(64, 48, 0.25f, 0.25f, 0.25f, 1.0f);
    const std::size_t hot = static_cast<std::size_t>(21) * 64 + 37;
    full.r[hot] = 1000.0f;
    full.g[hot] = 900.0f;
    full.b[hot] = 800.0f;

    FloatImageBuffer proxy;
    CHECK(ps_build_preview_proxy(full, 4, 8.0f, proxy));
    CHECK(proxy.width == 16 && proxy.height == 12);
    const std::size_t proxy_hot = static_cast<std::size_t>(21 / 4) * 16 + 37 / 4;
    CHECK(proxy.r[proxy_hot] == 1000.0f);
    CHECK(near(proxy.r[0], 0.25, 1.0e-6));

    AeParameterState state {};
    state.downsample = 2;
    state.flare_gain = 100.0f;
    state.cluster_radius_px = 9;
    ps_scale_state_for_proxy(state, 4);
    CHECK(state.downsample == 1);
    CHECK(near(state.flare_gain, 25.0, 1.0e-3));
    CHECK(state.cluster_radius_px == 2);

    // A small radius stays on in the preview instead of rounding to 0.
    AeParameterState small_radius {};
    small_radius.cluster_radius_px = 1;
    ps_scale_state_for_proxy(small_radius, 4);
    CHECK(small_radius.cluster_radius_px == 1);

    AeParameterState coarse {};
    coarse.downsample = 8;
    coarse.flare_gain = 50.0f;
    ps_scale_state_for_proxy(coarse, 2);
    CHECK(coarse.downsample == 4);
    CHECK(near(coarse.flare_gain, 12.5, 1.0e-4));

    std::vector<std::uint32_t> bitmap;
    ps_make_display_bitmap(full, true, 33, 17, bitmap);
    CHECK(bitmap.size() == 33u * 17u);
    CHECK((bitmap[0] >> 24) == 0xFF);
}

void test_render_32bit()
{
    AeUiParameterState ui = ps_default_ui_state();
    ui.ray_grid = 24;
    ui.downsample = 2;
    ui.max_sources = 16;
    AeParameterState state {};
    CHECK(ps_build_parameter_state(ui, state));

    FloatImageBuffer input = make_image(160, 96, 0.05f, 0.05f, 0.05f, 1.0f);
    const std::size_t hot = static_cast<std::size_t>(30) * 160 + 110;
    input.r[hot] = 400.0f;
    input.g[hot] = 380.0f;
    input.b[hot] = 350.0f;

    FloatImageBuffer output;
    CHECK(render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, output));
    CHECK(output.width == input.width && output.height == input.height);

    double added = 0.0;
    for (std::size_t i = 0; i < input.r.size(); ++i) {
        if (i != hot) {
            added += (output.r[i] - input.r[i]) + (output.g[i] - input.g[i]) + (output.b[i] - input.b[i]);
        }
    }
    CHECK(added > 0.0);
    CHECK(output.r[hot] >= 400.0f);

    std::vector<float> deep(static_cast<std::size_t>(output.width) * output.height * 3u);
    PsPixelBuffer buffer {deep.data(), output.width * 3 * 4, 32, 3, output.width, output.height};
    CHECK(ps_write_pixels(output, 0, 0, PsChannelMap {}, false, buffer));
    CHECK(deep[hot * 3] >= 400.0f);

    // A proxy render must stay close in total added energy to the full render.
    FloatImageBuffer proxy;
    CHECK(ps_build_preview_proxy(input, 2, state.threshold, proxy));
    AeParameterState proxy_state = state;
    ps_scale_state_for_proxy(proxy_state, 2);
    FloatImageBuffer proxy_output;
    CHECK(render_frame_to_float_image(FLARESIM_REPO_ROOT, proxy_state, proxy, proxy_output));
    double proxy_added = 0.0;
    for (std::size_t i = 0; i < proxy.r.size(); ++i) {
        proxy_added += (proxy_output.r[i] - proxy.r[i]) + (proxy_output.g[i] - proxy.g[i]) +
                       (proxy_output.b[i] - proxy.b[i]);
    }
    proxy_added *= 4.0;
    std::printf("full-res added energy %.4g, proxy (x4 area) %.4g\n", added, proxy_added);
    CHECK(proxy_added > 0.75 * added && proxy_added < 1.25 * added);
}

FloatImageBuffer make_many_sources_scene()
{
    FloatImageBuffer input = make_image(192, 108, 0.04f, 0.05f, 0.08f, 1.0f);
    for (int k = 0; k < 40; ++k) {
        const int x = 8 + (k * 37) % 176;
        const int y = 6 + (k * 23) % 96;
        const std::size_t i = static_cast<std::size_t>(y) * 192 + x;
        input.r[i] = 50.0f + 10.0f * k;
        input.g[i] = 45.0f + 9.0f * k;
        input.b[i] = 40.0f + 8.0f * k;
    }
    return input;
}

AeParameterState many_sources_state()
{
    AeUiParameterState ui = ps_default_ui_state();
    ui.ray_grid = 16;
    ui.downsample = 1;
    ui.max_sources = 0;
    AeParameterState state {};
    CHECK(ps_build_parameter_state(ui, state));
    return state;
}

// Each call runs on its own thread so the runtime's per-thread caches cannot
// hand back an earlier result.
template <typename Fn>
void on_fresh_thread(Fn&& fn)
{
    std::thread(std::forward<Fn>(fn)).join();
}

double max_abs_diff(const FloatImageBuffer& a, const FloatImageBuffer& b, double& out_peak)
{
    double diff = 0.0;
    out_peak = 0.0;
    for (std::size_t i = 0; i < a.r.size(); ++i) {
        diff = std::max({diff, std::abs(double(a.r[i]) - b.r[i]), std::abs(double(a.g[i]) - b.g[i]),
                         std::abs(double(a.b[i]) - b.b[i])});
        out_peak = std::max({out_peak, double(a.r[i]), double(a.g[i]), double(a.b[i])});
    }
    return diff;
}

void test_progress_and_cancel()
{
    const FloatImageBuffer input = make_many_sources_scene();
    const AeParameterState state = many_sources_state();

    FloatImageBuffer plain;
    bool plain_ok = false;
    on_fresh_thread([&] { plain_ok = render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, plain); });
    CHECK(plain_ok);

    // With a progress scope the sources are traced in chunks; the image must not change.
    FloatImageBuffer chunked;
    bool chunked_ok = false;
    std::vector<float> fractions;
    std::size_t sources = 0;
    on_fresh_thread([&] {
        ScopedRenderProgress progress([&](float fraction) {
            fractions.push_back(fraction);
            return true;
        });
        chunked_ok = render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, chunked);
        sources = last_render_source_count();
    });
    CHECK(chunked_ok);
    CHECK(sources >= 16);
    // At least one chunk boundary before the last chunk, i.e. the render was split.
    const auto split = std::count_if(fractions.begin(), fractions.end(), [](float f) { return f > 0.051f && f < 0.899f; });
    CHECK(split >= 1);
    CHECK(!fractions.empty() && near(fractions.back(), 1.0, 1e-6));
    for (std::size_t i = 1; i < fractions.size(); ++i) {
        CHECK(fractions[i] >= fractions[i - 1]);
    }
    double peak = 0.0;
    const double diff = max_abs_diff(plain, chunked, peak);
    std::printf("chunked vs single launch: max diff %.3g of peak %.4g over %zu progress steps\n", diff, peak,
                fractions.size());
    CHECK(diff <= 1e-4 * std::max(peak, 1.0));

    // Cancelling stops the render, and the next render on that thread is
    // complete rather than built from the cancelled one's leftovers.
    on_fresh_thread([&] {
        bool cancelled_ok = true;
        int calls = 0;
        {
            ScopedRenderProgress progress([&](float fraction) {
                ++calls;
                return fraction < 0.2f;
            });
            FloatImageBuffer partial;
            cancelled_ok = render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, partial);
        }
        CHECK(!cancelled_ok);
        CHECK(calls >= 2);

        FloatImageBuffer after;
        CHECK(render_frame_to_float_image(FLARESIM_REPO_ROOT, state, input, after));
        double after_peak = 0.0;
        CHECK(max_abs_diff(plain, after, after_peak) <= 1e-4 * std::max(after_peak, 1.0));
    });
}

void test_presets()
{
    CHECK(ps_valid_preset_name("Sun flare 2"));
    CHECK(ps_valid_preset_name("Sonne \xC3\x9C"));
    CHECK(!ps_valid_preset_name(""));
    CHECK(!ps_valid_preset_name("a/b"));
    CHECK(!ps_valid_preset_name("a\\b"));
    CHECK(!ps_valid_preset_name("what?"));
    CHECK(!ps_valid_preset_name("dot."));
    CHECK(!ps_valid_preset_name(" lead"));
    CHECK(!ps_valid_preset_name(std::string(101, 'x')));

    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / ("flaresim_ps_smoke_presets_" + std::to_string(std::rand()));
    std::error_code ec;
    fs::remove_all(folder, ec);

    CHECK(ps_list_presets(folder).empty());
    CHECK(ps_find_matching_preset(folder, ps_default_ui_state()).empty());

    AeUiParameterState warm = ps_default_ui_state();
    warm.flare_gain = 1234.5f;
    warm.cluster_radius_px = 24;
    CHECK(ps_set_lens_id(warm, "double-gauss"));
    AeUiParameterState cool = ps_default_ui_state();
    cool.threshold = 2.5f;
    cool.manual_source_enabled = true;

    CHECK(ps_save_preset(folder, "beta", warm));
    CHECK(ps_save_preset(folder, "Alpha", cool));
    CHECK(ps_save_preset(folder, "Sonne \xC3\x9C", cool));
    CHECK(!ps_save_preset(folder, "bad/name", cool));

    const std::vector<std::string> names = ps_list_presets(folder);
    CHECK(names.size() == 3);
    CHECK(names.size() == 3 && names[0] == "Alpha" && names[1] == "beta" && names[2] == "Sonne \xC3\x9C");

    AeUiParameterState loaded = ps_default_ui_state();
    CHECK(ps_load_preset(folder, "beta", loaded));
    CHECK(ps_serialize_settings(loaded) == ps_serialize_settings(warm));
    CHECK(ps_preset_exists(folder, "Sonne \xC3\x9C"));
    CHECK(!ps_load_preset(folder, "missing", loaded));

    // Matching goes by settings, so the first equal preset in list order wins.
    CHECK(ps_find_matching_preset(folder, warm) == "beta");
    CHECK(ps_find_matching_preset(folder, cool) == "Alpha");
    CHECK(ps_find_matching_preset(folder, ps_default_ui_state()).empty());

    // Saving over an existing preset replaces it.
    CHECK(ps_save_preset(folder, "beta", cool));
    CHECK(ps_load_preset(folder, "beta", loaded));
    CHECK(near(loaded.threshold, 2.5, 1e-6));

    CHECK(ps_delete_preset(folder, "Alpha"));
    CHECK(!ps_delete_preset(folder, "Alpha"));
    CHECK(ps_list_presets(folder).size() == 2);

    fs::remove_all(folder, ec);
}

} // namespace

int main()
{
    test_settings_round_trip();
    test_settings_parsing_rules();
    test_pixel_round_trips();
    test_preview_proxy();
    test_render_32bit();
    test_progress_and_cancel();
    test_presets();

    if (g_failures != 0) {
        std::fprintf(stderr, "flaresim_ps_smoke: %d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("flaresim_ps_smoke: ok\n");
    return EXIT_SUCCESS;
}
