#pragma once

#include "parameter_state.h"
#include "pixel_convert.h"

#include <string>
#include <vector>

struct FloatImageBuffer
{
    int width = 0;
    int height = 0;
    std::vector<float> alpha;
    std::vector<float> r;
    std::vector<float> g;
    std::vector<float> b;
};

bool unpack_image(const AePixel8Like* pixels, int width, int height, FloatImageBuffer& out_image);
bool unpack_image(const AePixel16Like* pixels, int width, int height, FloatImageBuffer& out_image);
bool unpack_image(const AePixel32Like* pixels, int width, int height, FloatImageBuffer& out_image);

bool pack_image(const FloatImageBuffer& image, AePixel8Like* out_pixels);
bool pack_image(const FloatImageBuffer& image, AePixel16Like* out_pixels);
bool pack_image(const FloatImageBuffer& image, AePixel32Like* out_pixels);

bool render_frame_to_float_image(
    const std::string& asset_root,
    const AeParameterState& state,
    const FloatImageBuffer& input,
    FloatImageBuffer& output,
    const FloatImageBuffer* detection_mask = nullptr
);

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel8Like* input_pixels,
    AePixel8Like* output_pixels,
    int width,
    int height,
    const AePixel8Like* mask_pixels = nullptr
);

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel16Like* input_pixels,
    AePixel16Like* output_pixels,
    int width,
    int height,
    const AePixel16Like* mask_pixels = nullptr
);

bool render_frame_to_pixels(
    const std::string& asset_root,
    const AeParameterState& state,
    const AePixel32Like* input_pixels,
    AePixel32Like* output_pixels,
    int width,
    int height,
    const AePixel32Like* mask_pixels = nullptr
);
