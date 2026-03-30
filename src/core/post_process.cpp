#include "post_process.h"

#include <algorithm>
#include <vector>

namespace {

void box_blur_channel(float* buf,
                      int width,
                      int height,
                      int radius,
                      int passes,
                      std::vector<float>& tmp)
{
    if (!buf || width <= 0 || height <= 0 || radius < 1 || passes < 1) {
        return;
    }

    const size_t np = static_cast<size_t>(width) * static_cast<size_t>(height);
    tmp.resize(np);

    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < height; ++y) {
            const int row = y * width;
            float sum = 0.0f;

            for (int k = -radius; k <= radius; ++k) {
                const int x = std::clamp(k, 0, width - 1);
                sum += buf[row + x];
            }

            tmp[row] = sum / static_cast<float>((radius * 2) + 1);

            for (int x = 1; x < width; ++x) {
                const int add = std::min(x + radius, width - 1);
                const int rem = std::clamp(x - radius - 1, 0, width - 1);
                sum += buf[row + add] - buf[row + rem];
                tmp[row + x] = sum / static_cast<float>((radius * 2) + 1);
            }
        }

        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;

            for (int k = -radius; k <= radius; ++k) {
                const int y = std::clamp(k, 0, height - 1);
                sum += tmp[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
            }

            buf[x] = sum / static_cast<float>((radius * 2) + 1);

            for (int y = 1; y < height; ++y) {
                const int add = std::min(y + radius, height - 1);
                const int rem = std::clamp(y - radius - 1, 0, height - 1);
                sum += tmp[static_cast<size_t>(add) * static_cast<size_t>(width) + static_cast<size_t>(x)] -
                       tmp[static_cast<size_t>(rem) * static_cast<size_t>(width) + static_cast<size_t>(x)];
                buf[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                    sum / static_cast<float>((radius * 2) + 1);
            }
        }
    }
}

} // namespace

void box_blur_rgb(float* r,
                  float* g,
                  float* b,
                  int width,
                  int height,
                  int radius,
                  int passes)
{
    std::vector<float> tmp;
    box_blur_channel(r, width, height, radius, passes, tmp);
    box_blur_channel(g, width, height, radius, passes, tmp);
    box_blur_channel(b, width, height, radius, passes, tmp);
}
