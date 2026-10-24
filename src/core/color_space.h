#pragma once

#include "image.h"

#include <cstddef>
#include <string_view>

// Scene-linear RGB spaces the host image can be in. The renderer works in
// linear Rec.709 internally: its per-channel spectral bands and luminance
// weights are Rec.709 ones. Other spaces are converted on the way in and
// the result is converted back, so a flare looks the same in every space.
enum class WorkingColorSpace
{
    LinearRec709,
    ACEScg,
    ACES2065_1,
    LinearRec2020,
    LinearDisplayP3,
};

struct ColorMatrix
{
    float m[9];
};

const ColorMatrix& working_to_rec709_matrix(WorkingColorSpace space);
const ColorMatrix& rec709_to_working_matrix(WorkingColorSpace space);

inline void apply_color_matrix(const ColorMatrix& matrix, float& r, float& g, float& b)
{
    const float* m = matrix.m;
    const float nr = m[0] * r + m[1] * g + m[2] * b;
    const float ng = m[3] * r + m[4] * g + m[5] * b;
    const float nb = m[6] * r + m[7] * g + m[8] * b;
    r = nr;
    g = ng;
    b = nb;
}

void apply_color_matrix(const ColorMatrix& matrix, MutableRgbImageView image);

// Matches an ICC profile to a space by its colorants (rXYZ/gXYZ/bXYZ, which
// ICC stores adapted to D50). Returns false for profiles that are not RGB
// matrix profiles or do not match a known space.
bool working_space_from_icc_profile(const void* data, std::size_t size, WorkingColorSpace& out_space);

// Matches an OCIO color space or profile name such as "ACEScg",
// "ACES - ACES2065-1" or "Linear Rec.2020".
bool working_space_from_name(std::string_view name, WorkingColorSpace& out_space);

const char* working_space_label(WorkingColorSpace space);
