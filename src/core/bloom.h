#pragma once

#include "image.h"

struct BloomConfig
{
    float threshold = 1.0f;
    float strength = 0.0f;
    float radius = 0.04f;
    int passes = 3;
    int octaves = 5;
    bool chromatic = true;
};

void generate_bloom(
    const RgbImageView& input,
    MutableRgbImageView output,
    const BloomConfig& config
);
