#include "frame_bridge.h"

#include "lens_resolution.h"
#include "output_view.h"

#include <filesystem>

namespace {

namespace fs = std::filesystem;

struct ThreadRenderContext
{
    FrameRenderCache cache;
    LensSystem lens;
    std::string asset_root;
    std::string lens_path;
    fs::file_time_type lens_mtime {};
    bool lens_loaded = false;
    bool lens_mtime_valid = false;
};

bool resolve_cached_lens(const std::string& asset_root,
                         const AeLensSelection& selection,
                         ThreadRenderContext& context,
                         const LensSystem*& out_lens)
{
    std::string lens_path;
    if (!resolve_lens_path(selection, asset_root, lens_path)) {
        return false;
    }

    std::error_code ec;
    const fs::file_time_type current_mtime = fs::last_write_time(fs::path(lens_path), ec);
    const bool mtime_valid = !ec;

    bool need_reload = !context.lens_loaded ||
                       context.asset_root != asset_root ||
                       context.lens_path != lens_path ||
                       context.lens_mtime_valid != mtime_valid;
    if (!need_reload && mtime_valid) {
        need_reload = context.lens_mtime != current_mtime;
    }

    if (need_reload) {
        if (!load_selected_lens(selection, asset_root, context.lens, &lens_path)) {
            return false;
        }
        context.asset_root = asset_root;
        context.lens_path = std::move(lens_path);
        context.lens_loaded = true;
        context.lens_mtime_valid = mtime_valid;
        if (mtime_valid) {
            context.lens_mtime = current_mtime;
        }
    }

    out_lens = &context.lens;
    return true;
}

bool allocate_image(int width, int height, FloatImageBuffer& image)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const size_t np = static_cast<size_t>(width) * static_cast<size_t>(height);
    image.width = width;
    image.height = height;
    image.alpha.assign(np, 1.0f);
    image.r.assign(np, 0.0f);
    image.g.assign(np, 0.0f);
    image.b.assign(np, 0.0f);
    return true;
}

bool validate_image(const FloatImageBuffer& image)
{
    if (image.width <= 0 || image.height <= 0) {
        return false;
    }

    const size_t np = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    return image.alpha.size() == np &&
           image.r.size() == np &&
           image.g.size() == np &&
           image.b.size() == np;
}

void expand_alpha_from_rgb(FloatImageBuffer& image)
{
    if (!validate_image(image)) {
        return;
    }

    const size_t np = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < np; ++i) {
        const float rendered_alpha =
            std::clamp(std::max({image.r[i], image.g[i], image.b[i], 0.0f}), 0.0f, 1.0f);
        image.alpha[i] = std::max(image.alpha[i], rendered_alpha);
    }
}

RgbImageView make_rgb_view(const FloatImageBuffer& image)
{
    return {
        image.r.data(),
        image.g.data(),
        image.b.data(),
        image.width,
        image.height,
    };
}

MutableRgbImageView make_mutable_rgb_view(FloatImageBuffer& image)
{
    return {
        image.r.data(),
        image.g.data(),
        image.b.data(),
        image.width,
        image.height,
    };
}

void build_detection_mask_values(const FloatImageBuffer& image, std::vector<float>& out_values)
{
    out_values.clear();
    if (!validate_image(image)) {
        return;
    }

    const size_t np = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    out_values.resize(np, 0.0f);
    for (size_t i = 0; i < np; ++i) {
        const float alpha = std::clamp(image.alpha[i], 0.0f, 1.0f);
        const float visible =
            std::clamp(std::max({image.r[i], image.g[i], image.b[i], 0.0f}), 0.0f, 1.0f);
        out_values[i] = visible * alpha;
    }
}

template <typename PixelT>
bool unpack_image_impl(const PixelT* pixels, int width, int height, FloatImageBuffer& out_image)
{
    if (!pixels || !allocate_image(width, height, out_image)) {
        return false;
    }

    const size_t np = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < np; ++i) {
        const FloatPixel unpacked = unpack_pixel(pixels[i]);
        out_image.alpha[i] = unpacked.alpha;
        out_image.r[i] = unpacked.red;
        out_image.g[i] = unpacked.green;
        out_image.b[i] = unpacked.blue;
    }
    return true;
}

template <typename PixelT, typename PackFn>
bool pack_image_impl(const FloatImageBuffer& image, PixelT* out_pixels, PackFn pack_pixel)
{
    if (!out_pixels || !validate_image(image)) {
        return false;
    }

    const size_t np = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < np; ++i) {
        out_pixels[i] = pack_pixel(FloatPixel {
            image.alpha[i],
            image.r[i],
            image.g[i],
            image.b[i],
        });
    }
    return true;
}

template <typename PixelT>
bool render_frame_to_pixels_impl(const std::string& asset_root,
                                 const AeParameterState& state,
                                 const PixelT* input_pixels,
                                 PixelT* output_pixels,
                                 int width,
                                 int height,
                                 const PixelT* mask_pixels)
{
    FloatImageBuffer input;
    if (!unpack_image_impl(input_pixels, width, height, input)) {
        return false;
    }

    FloatImageBuffer mask;
    const FloatImageBuffer* detection_mask = nullptr;
    if (mask_pixels) {
        if (!unpack_image_impl(mask_pixels, width, height, mask)) {
            return false;
        }
        detection_mask = &mask;
    }

    FloatImageBuffer output;
    if (!render_frame_to_float_image(asset_root, state, input, output, detection_mask)) {
        return false;
    }

    return pack_image(output, output_pixels);
}

} // namespace

bool unpack_image(const AePixel8Like* pixels, int width, int height, FloatImageBuffer& out_image)
{
    return unpack_image_impl(pixels, width, height, out_image);
}

bool unpack_image(const AePixel16Like* pixels, int width, int height, FloatImageBuffer& out_image)
{
    return unpack_image_impl(pixels, width, height, out_image);
}

bool unpack_image(const AePixel32Like* pixels, int width, int height, FloatImageBuffer& out_image)
{
    return unpack_image_impl(pixels, width, height, out_image);
}

bool pack_image(const FloatImageBuffer& image, AePixel8Like* out_pixels)
{
    return pack_image_impl(image, out_pixels, pack_pixel8);
}

bool pack_image(const FloatImageBuffer& image, AePixel16Like* out_pixels)
{
    return pack_image_impl(image, out_pixels, pack_pixel16);
}

bool pack_image(const FloatImageBuffer& image, AePixel32Like* out_pixels)
{
    return pack_image_impl(image, out_pixels, pack_pixel32);
}

bool render_frame_to_float_image(
    const std::string& asset_root,
    const AeParameterState& state,
    const FloatImageBuffer& input,
    FloatImageBuffer& output,
    const FloatImageBuffer* detection_mask)
{
    if (!validate_image(input) || asset_root.empty()) {
        return false;
    }

    thread_local ThreadRenderContext context;

    const LensSystem* lens = nullptr;
    if (!resolve_cached_lens(asset_root, state.lens, context, lens) || !lens) {
        return false;
    }

    if (!allocate_image(input.width, input.height, output)) {
        return false;
    }

    output.alpha = input.alpha;

    const FrameRenderSettings settings = build_frame_render_settings(state);
    const FrameRenderPlan plan = build_output_view_render_plan(state.view);
    const RgbImageView input_view = make_rgb_view(input);
    const bool use_detection_mask = detection_mask && validate_image(*detection_mask) &&
                                    detection_mask->width == input.width &&
                                    detection_mask->height == input.height;
    std::vector<float> detection_mask_values;
    if (use_detection_mask) {
        build_detection_mask_values(*detection_mask, detection_mask_values);
    }
    const MonoImageView detection_mask_view = {
        detection_mask_values.data(),
        input.width,
        input.height,
    };

    FrameRenderOutputs outputs;
    if (!render_frame(*lens,
                      input_view,
                      settings,
                      outputs,
                      plan,
                      &context.cache,
                      use_detection_mask && !detection_mask_values.empty() ? &detection_mask_view : nullptr)) {
        return false;
    }

    if (!compose_output_view(
            state.view,
            input_view,
            settings,
            outputs,
            make_mutable_rgb_view(output))) {
        return false;
    }

    expand_alpha_from_rgb(output);
    return true;
}

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel8Like* input_pixels,
    AePixel8Like* output_pixels,
    int width,
    int height,
    const AePixel8Like* mask_pixels)
{
    return render_frame_to_pixels_impl(
        asset_root, state, input_pixels, output_pixels, width, height, mask_pixels);
}

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel16Like* input_pixels,
    AePixel16Like* output_pixels,
    int width,
    int height,
    const AePixel16Like* mask_pixels)
{
    return render_frame_to_pixels_impl(
        asset_root, state, input_pixels, output_pixels, width, height, mask_pixels);
}

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel32Like* input_pixels,
    AePixel32Like* output_pixels,
    int width,
    int height,
    const AePixel32Like* mask_pixels)
{
    return render_frame_to_pixels_impl(
        asset_root, state, input_pixels, output_pixels, width, height, mask_pixels);
}
