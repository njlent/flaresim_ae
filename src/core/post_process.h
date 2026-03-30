#pragma once

void box_blur_rgb(
    float* r,
    float* g,
    float* b,
    int width,
    int height,
    int radius,
    int passes
);
