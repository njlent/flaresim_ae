#pragma once

#include <cstdint>

struct AePixel8Like
{
    std::uint8_t alpha = 255;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

struct AePixel16Like
{
    std::uint16_t alpha = 32768;
    std::uint16_t red = 0;
    std::uint16_t green = 0;
    std::uint16_t blue = 0;
};

struct AePixel32Like
{
    float alpha = 1.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct FloatPixel
{
    float alpha = 1.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

constexpr float kAeMaxChan8 = 255.0f;
constexpr float kAeMaxChan16 = 32768.0f;

FloatPixel unpack_pixel(const AePixel8Like& pixel);
FloatPixel unpack_pixel(const AePixel16Like& pixel);
FloatPixel unpack_pixel(const AePixel32Like& pixel);

AePixel8Like pack_pixel8(const FloatPixel& pixel);
AePixel16Like pack_pixel16(const FloatPixel& pixel);
AePixel32Like pack_pixel32(const FloatPixel& pixel);
