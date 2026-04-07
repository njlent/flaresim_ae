#pragma once

struct RgbImageView
{
    const float* r = nullptr;
    const float* g = nullptr;
    const float* b = nullptr;
    int width = 0;
    int height = 0;
};

struct MonoImageView
{
    const float* value = nullptr;
    int width = 0;
    int height = 0;
};

struct MutableRgbImageView
{
    float* r = nullptr;
    float* g = nullptr;
    float* b = nullptr;
    int width = 0;
    int height = 0;
};
