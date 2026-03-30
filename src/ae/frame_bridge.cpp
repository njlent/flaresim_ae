#include "frame_bridge.h"

#include "lens_resolution.h"
#include "output_view.h"

namespace {

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
bool render_frame_to_pixels_impl(const std::string& repo_root,
                                 const AeParameterState& state,
                                 const PixelT* input_pixels,
                                 PixelT* output_pixels,
                                 int width,
                                 int height)
{
    FloatImageBuffer input;
    if (!unpack_image_impl(input_pixels, width, height, input)) {
        return false;
    }

    FloatImageBuffer output;
    if (!render_frame_to_float_image(repo_root, state, input, output)) {
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
    const std::string& repo_root,
    const AeParameterState& state,
    const FloatImageBuffer& input,
    FloatImageBuffer& output)
{
    if (!validate_image(input) || repo_root.empty()) {
        return false;
    }

    LensSystem lens;
    if (!load_selected_lens(state.lens, repo_root, lens)) {
        return false;
    }

    if (!allocate_image(input.width, input.height, output)) {
        return false;
    }

    output.alpha = input.alpha;

    const FrameRenderSettings settings = build_frame_render_settings(state);
    const RgbImageView input_view = make_rgb_view(input);

    FrameRenderOutputs outputs;
    if (!render_frame(lens, input_view, settings, outputs)) {
        return false;
    }

    return compose_output_view(
        state.view,
        input_view,
        settings,
        outputs,
        make_mutable_rgb_view(output));
}

bool render_frame_to_pixels(
    const std::string& repo_root,
    const AeParameterState& state,
    const AePixel8Like* input_pixels,
    AePixel8Like* output_pixels,
    int width,
    int height)
{
    return render_frame_to_pixels_impl(repo_root, state, input_pixels, output_pixels, width, height);
}

bool render_frame_to_pixels(
    const std::string& repo_root,
    const AeParameterState& state,
    const AePixel16Like* input_pixels,
    AePixel16Like* output_pixels,
    int width,
    int height)
{
    return render_frame_to_pixels_impl(repo_root, state, input_pixels, output_pixels, width, height);
}

bool render_frame_to_pixels(
    const std::string& repo_root,
    const AeParameterState& state,
    const AePixel32Like* input_pixels,
    AePixel32Like* output_pixels,
    int width,
    int height)
{
    return render_frame_to_pixels_impl(repo_root, state, input_pixels, output_pixels, width, height);
}
