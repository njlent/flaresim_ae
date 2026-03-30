#include "starburst.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace {

void fft1d(std::complex<float>* data, int n)
{
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / len;
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = data[i + j];
                const std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void fft2d(std::vector<std::complex<float>>& grid, int N)
{
    for (int row = 0; row < N; ++row) {
        fft1d(grid.data() + static_cast<size_t>(row) * static_cast<size_t>(N), N);
    }

    std::vector<std::complex<float>> col(N);
    for (int c = 0; c < N; ++c) {
        for (int r = 0; r < N; ++r) {
            col[r] = grid[static_cast<size_t>(r) * static_cast<size_t>(N) + static_cast<size_t>(c)];
        }
        fft1d(col.data(), N);
        for (int r = 0; r < N; ++r) {
            grid[static_cast<size_t>(r) * static_cast<size_t>(N) + static_cast<size_t>(c)] = col[r];
        }
    }
}

void build_aperture_mask(std::vector<float>& mask,
                         int N,
                         int aperture_blades,
                         float aperture_rotation_deg)
{
    mask.assign(static_cast<size_t>(N) * static_cast<size_t>(N), 0.0f);

    const float rotation = aperture_rotation_deg * 3.14159265358979323846f / 180.0f;
    const bool polygonal = aperture_blades >= 3;
    const float apothem = polygonal ? std::cos(3.14159265358979323846f / aperture_blades) : 1.0f;
    const float sector_angle = polygonal ? (2.0f * 3.14159265358979323846f / aperture_blades) : 1.0f;

    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            const float u = ((c + 0.5f) / N) * 2.0f - 1.0f;
            const float v = ((r + 0.5f) / N) * 2.0f - 1.0f;
            const float r2 = u * u + v * v;

            if (r2 > 1.0f) {
                continue;
            }

            if (polygonal) {
                float angle = std::atan2(v, u) - rotation;
                float sector = std::fmod(angle, sector_angle);
                if (sector < 0.0f) {
                    sector += sector_angle;
                }
                if (std::sqrt(r2) * std::cos(sector - sector_angle * 0.5f) > apothem) {
                    continue;
                }
            }

            mask[static_cast<size_t>(r) * static_cast<size_t>(N) + static_cast<size_t>(c)] = 1.0f;
        }
    }
}

} // namespace

void compute_starburst_psf(const StarburstConfig& config,
                           StarburstPSF& out_psf,
                           int fft_size)
{
    out_psf.N = fft_size;
    out_psf.data.resize(static_cast<size_t>(fft_size) * static_cast<size_t>(fft_size));

    std::vector<float> mask;
    build_aperture_mask(mask, fft_size, config.aperture_blades, config.aperture_rotation_deg);

    std::vector<std::complex<float>> grid(static_cast<size_t>(fft_size) * static_cast<size_t>(fft_size));
    for (size_t i = 0; i < grid.size(); ++i) {
        grid[i] = {mask[i], 0.0f};
    }

    fft2d(grid, fft_size);

    float peak = 0.0f;
    for (int r = 0; r < fft_size; ++r) {
        const int rs = (r + fft_size / 2) % fft_size;
        for (int c = 0; c < fft_size; ++c) {
            const auto& z = grid[static_cast<size_t>(rs) * static_cast<size_t>(fft_size) +
                                 static_cast<size_t>((c + fft_size / 2) % fft_size)];
            const float value = z.real() * z.real() + z.imag() * z.imag();
            out_psf.data[static_cast<size_t>(r) * static_cast<size_t>(fft_size) + static_cast<size_t>(c)] = value;
            peak = std::max(peak, value);
        }
    }

    if (peak > 0.0f) {
        const float inv_peak = 1.0f / peak;
        for (float& v : out_psf.data) {
            v *= inv_peak;
        }
    }
}

void render_starburst(const StarburstPSF& psf,
                      const StarburstConfig& config,
                      const std::vector<BrightPixel>& sources,
                      float tan_half_h,
                      float tan_half_v,
                      float* out_r,
                      float* out_g,
                      float* out_b,
                      int width,
                      int height,
                      int fmt_w,
                      int fmt_h,
                      int fmt_x0_in_buf,
                      int fmt_y0_in_buf)
{
    if (psf.empty() || config.gain <= 0.0f || sources.empty()) {
        return;
    }

    const int N = psf.N;
    const float half_N = N * 0.5f;
    const float diag = std::sqrt(static_cast<float>(width * width + height * height));
    const float r_ref = config.scale * diag;
    const float k_ref_lambda = 550.0f;
    const float scale_ch[3] = {
        config.wavelengths[0] / k_ref_lambda,
        config.wavelengths[1] / k_ref_lambda,
        config.wavelengths[2] / k_ref_lambda,
    };

    const float fmt_cx = fmt_x0_in_buf + fmt_w * 0.5f;
    const float fmt_cy = fmt_y0_in_buf + fmt_h * 0.5f;
    float* channels[3] = {out_r, out_g, out_b};

    for (const BrightPixel& source : sources) {
        const float src_px = fmt_cx + (std::tan(source.angle_x) / (2.0f * tan_half_h)) * fmt_w;
        const float src_py = fmt_cy + (std::tan(source.angle_y) / (2.0f * tan_half_v)) * fmt_h;

        const float r_max = r_ref * scale_ch[0];
        const int x0 = std::max(0, static_cast<int>(std::floor(src_px - r_max)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(src_px + r_max)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(src_py - r_max)));
        const int y1 = std::min(height, static_cast<int>(std::ceil(src_py + r_max)) + 1);
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }

        const float src_rgb[3] = {source.r, source.g, source.b};
        for (int ch = 0; ch < 3; ++ch) {
            const float inv_r = half_N / (r_ref * scale_ch[ch]);
            const float src_v = src_rgb[ch] * config.gain;
            float* out_channel = channels[ch];

            for (int oy = y0; oy < y1; ++oy) {
                const float dj_f = (oy - src_py) * inv_r + half_N;
                if (dj_f < 0.0f || dj_f >= static_cast<float>(N - 1)) {
                    continue;
                }
                const int dj = static_cast<int>(dj_f);
                const float fj = dj_f - dj;

                for (int ox = x0; ox < x1; ++ox) {
                    const float di_f = (ox - src_px) * inv_r + half_N;
                    if (di_f < 0.0f || di_f >= static_cast<float>(N - 1)) {
                        continue;
                    }
                    const int di = static_cast<int>(di_f);
                    const float fi = di_f - di;

                    const float* p = psf.data.data();
                    const float value =
                        p[static_cast<size_t>(dj) * static_cast<size_t>(N) + static_cast<size_t>(di)] * (1.0f - fi) * (1.0f - fj) +
                        p[static_cast<size_t>(dj) * static_cast<size_t>(N) + static_cast<size_t>(di + 1)] * fi * (1.0f - fj) +
                        p[static_cast<size_t>(dj + 1) * static_cast<size_t>(N) + static_cast<size_t>(di)] * (1.0f - fi) * fj +
                        p[static_cast<size_t>(dj + 1) * static_cast<size_t>(N) + static_cast<size_t>(di + 1)] * fi * fj;

                    out_channel[static_cast<size_t>(oy) * static_cast<size_t>(width) + static_cast<size_t>(ox)] += src_v * value;
                }
            }
        }
    }
}
