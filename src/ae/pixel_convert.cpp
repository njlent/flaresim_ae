#include "pixel_convert.h"

#include <algorithm>
#include <cmath>

namespace {

std::uint8_t pack_u8(float value)
{
    const float scaled = std::round(std::clamp(value, 0.0f, 1.0f) * kAeMaxChan8);
    return static_cast<std::uint8_t>(scaled);
}

std::uint16_t pack_u16(float value)
{
    const float scaled = std::round(std::clamp(value, 0.0f, 1.0f) * kAeMaxChan16);
    return static_cast<std::uint16_t>(scaled);
}

} // namespace

FloatPixel unpack_pixel(const AePixel8Like& pixel)
{
    return {
        pixel.alpha / kAeMaxChan8,
        pixel.red / kAeMaxChan8,
        pixel.green / kAeMaxChan8,
        pixel.blue / kAeMaxChan8,
    };
}

FloatPixel unpack_pixel(const AePixel16Like& pixel)
{
    return {
        pixel.alpha / kAeMaxChan16,
        pixel.red / kAeMaxChan16,
        pixel.green / kAeMaxChan16,
        pixel.blue / kAeMaxChan16,
    };
}

FloatPixel unpack_pixel(const AePixel32Like& pixel)
{
    return {pixel.alpha, pixel.red, pixel.green, pixel.blue};
}

AePixel8Like pack_pixel8(const FloatPixel& pixel)
{
    return {
        pack_u8(pixel.alpha),
        pack_u8(pixel.red),
        pack_u8(pixel.green),
        pack_u8(pixel.blue),
    };
}

AePixel16Like pack_pixel16(const FloatPixel& pixel)
{
    return {
        pack_u16(pixel.alpha),
        pack_u16(pixel.red),
        pack_u16(pixel.green),
        pack_u16(pixel.blue),
    };
}

AePixel32Like pack_pixel32(const FloatPixel& pixel)
{
    return {pixel.alpha, pixel.red, pixel.green, pixel.blue};
}
