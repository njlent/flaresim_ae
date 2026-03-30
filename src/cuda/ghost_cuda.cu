#include "ghost_cuda.h"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct DVec3
{
    float x;
    float y;
    float z;
};

__device__ __forceinline__ DVec3 dv(float x, float y, float z)
{
    return {x, y, z};
}

__device__ __forceinline__ DVec3 dv_sub(DVec3 a, DVec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

__device__ __forceinline__ DVec3 dv_neg(DVec3 a)
{
    return {-a.x, -a.y, -a.z};
}

__device__ __forceinline__ float dv_dot(DVec3 a, DVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __forceinline__ DVec3 dv_normalize(DVec3 v)
{
    const float inv = rsqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x * inv, v.y * inv, v.z * inv};
}

struct DRay
{
    DVec3 origin;
    DVec3 dir;
};

__device__ __forceinline__ float d_dispersion_ior(float n_d, float v_d, float lambda_nm)
{
    if (v_d < 0.1f || n_d <= 1.0001f) {
        return n_d;
    }

    const float lF = 486.13f;
    const float lC = 656.27f;
    const float ld = 587.56f;

    const float dn = (n_d - 1.0f) / v_d;
    const float inv_lF2 = 1.0f / (lF * lF);
    const float inv_lC2 = 1.0f / (lC * lC);
    const float inv_ld2 = 1.0f / (ld * ld);
    const float B = dn / (inv_lF2 - inv_lC2);
    const float A = n_d - B * inv_ld2;
    return A + B / (lambda_nm * lambda_nm);
}

__device__ __forceinline__ float d_ior_at(const Surface& surface, float lambda_nm)
{
    return d_dispersion_ior(surface.ior, surface.abbe_v, lambda_nm);
}

__device__ __forceinline__ float d_ior_before(const Surface* surfaces, int idx, float lambda_nm)
{
    return (idx <= 0) ? 1.0f : d_ior_at(surfaces[idx - 1], lambda_nm);
}

__device__ __forceinline__ float d_fresnel_reflectance(float cos_i, float n1, float n2)
{
    cos_i = fabsf(cos_i);
    const float eta = n1 / n2;
    const float sin2_t = eta * eta * (1.0f - cos_i * cos_i);
    if (sin2_t >= 1.0f) {
        return 1.0f;
    }

    const float cos_t = sqrtf(1.0f - sin2_t);
    const float rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
    const float rp = (n2 * cos_i - n1 * cos_t) / (n2 * cos_i + n1 * cos_t);
    return 0.5f * (rs * rs + rp * rp);
}

__device__ __forceinline__ float d_coating_reflectance(float cos_i,
                                                       float n1,
                                                       float n2,
                                                       float coating_n,
                                                       float thickness_nm,
                                                       float lambda_nm)
{
    const float sin2_c =
        (n1 / coating_n) * (n1 / coating_n) * (1.0f - cos_i * cos_i);
    if (sin2_c >= 1.0f) {
        return d_fresnel_reflectance(cos_i, n1, n2);
    }

    const float cos_c = sqrtf(1.0f - sin2_c);
    const float delta =
        2.0f * 3.14159265358979323846f * coating_n * thickness_nm * cos_c / lambda_nm;
    const float r01 =
        (n1 * cos_i - coating_n * cos_c) / (n1 * cos_i + coating_n * cos_c);

    const float sin2_2 =
        (coating_n / n2) * (coating_n / n2) * (1.0f - cos_c * cos_c);
    if (sin2_2 >= 1.0f) {
        return d_fresnel_reflectance(cos_i, n1, n2);
    }

    const float cos_2 = sqrtf(1.0f - sin2_2);
    const float r12 =
        (coating_n * cos_c - n2 * cos_2) / (coating_n * cos_c + n2 * cos_2);

    const float cos_2d = cosf(2.0f * delta);
    const float num = r01 * r01 + r12 * r12 + 2.0f * r01 * r12 * cos_2d;
    const float den = 1.0f + r01 * r01 * r12 * r12 + 2.0f * r01 * r12 * cos_2d;
    const float R = num / den;
    return fminf(fmaxf(R, 0.0f), 1.0f);
}

__device__ __forceinline__ float d_surface_reflectance(float cos_i,
                                                       float n1,
                                                       float n2,
                                                       int coating_layers,
                                                       float lambda_nm)
{
    if (coating_layers <= 0) {
        return d_fresnel_reflectance(cos_i, n1, n2);
    }

    const float mgf2_n = 1.38f;
    const float design_lambda = 550.0f;
    const float qw_thickness = design_lambda / (4.0f * mgf2_n);
    float R = d_coating_reflectance(cos_i, n1, n2, mgf2_n, qw_thickness, lambda_nm);

    for (int i = 1; i < coating_layers; ++i) {
        R *= 0.25f;
    }

    return fminf(fmaxf(R, 0.0f), 1.0f);
}

__device__ __forceinline__ bool d_intersect_surface(const DRay& ray,
                                                    const Surface& surface,
                                                    DVec3& hit_pos,
                                                    DVec3& normal)
{
    if (fabsf(surface.radius) < 1e-6f) {
        if (fabsf(ray.dir.z) < 1e-12f) {
            return false;
        }

        const float t = (surface.z - ray.origin.z) / ray.dir.z;
        if (!(t > 1e-6f)) {
            return false;
        }

        hit_pos = dv(ray.origin.x + ray.dir.x * t,
                     ray.origin.y + ray.dir.y * t,
                     ray.origin.z + ray.dir.z * t);

        if (!(hit_pos.x * hit_pos.x + hit_pos.y * hit_pos.y <=
              surface.semi_aperture * surface.semi_aperture)) {
            return false;
        }

        normal = dv(0.0f, 0.0f, (ray.dir.z > 0.0f) ? -1.0f : 1.0f);
        return true;
    }

    const float radius = surface.radius;
    const DVec3 center = dv(0.0f, 0.0f, surface.z + radius);
    const DVec3 oc = dv_sub(ray.origin, center);

    const float a = dv_dot(ray.dir, ray.dir);
    const float b = 2.0f * dv_dot(oc, ray.dir);
    const float c = dv_dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;

    if (disc < 0.0f) {
        return false;
    }

    const float sqrt_disc = sqrtf(disc);
    const float inv_2a = 0.5f / a;
    const float t1 = (-b - sqrt_disc) * inv_2a;
    const float t2 = (-b + sqrt_disc) * inv_2a;

    float t = 0.0f;
    if (t1 > 1e-6f && t2 > 1e-6f) {
        const float z1 = ray.origin.z + t1 * ray.dir.z;
        const float z2 = ray.origin.z + t2 * ray.dir.z;
        t = (fabsf(z1 - surface.z) < fabsf(z2 - surface.z)) ? t1 : t2;
    } else if (t1 > 1e-6f) {
        t = t1;
    } else if (t2 > 1e-6f) {
        t = t2;
    } else {
        return false;
    }

    hit_pos = dv(ray.origin.x + ray.dir.x * t,
                 ray.origin.y + ray.dir.y * t,
                 ray.origin.z + ray.dir.z * t);

    if (!(hit_pos.x * hit_pos.x + hit_pos.y * hit_pos.y <=
          surface.semi_aperture * surface.semi_aperture)) {
        return false;
    }

    const float inv_r = 1.0f / fabsf(radius);
    normal = dv((hit_pos.x - center.x) * inv_r,
                (hit_pos.y - center.y) * inv_r,
                (hit_pos.z - center.z) * inv_r);
    if (dv_dot(normal, ray.dir) > 0.0f) {
        normal = dv_neg(normal);
    }

    return true;
}

__device__ __forceinline__ bool d_refract(const DVec3& dir,
                                          const DVec3& normal,
                                          float n_ratio,
                                          DVec3& out_dir)
{
    const float cos_i = -dv_dot(normal, dir);
    const float sin2_t = n_ratio * n_ratio * (1.0f - cos_i * cos_i);
    if (sin2_t >= 1.0f) {
        return false;
    }

    const float cos_t = sqrtf(1.0f - sin2_t);
    const float k = n_ratio * cos_i - cos_t;
    const float ox = dir.x * n_ratio + normal.x * k;
    const float oy = dir.y * n_ratio + normal.y * k;
    const float oz = dir.z * n_ratio + normal.z * k;
    const float sq = ox * ox + oy * oy + oz * oz;
    if (sq < 1e-18f || !isfinite(sq)) {
        return false;
    }

    const float inv = rsqrtf(sq);
    out_dir = dv(ox * inv, oy * inv, oz * inv);
    return true;
}

__device__ __forceinline__ bool d_reflect(const DVec3& dir,
                                          const DVec3& normal,
                                          DVec3& out_dir)
{
    const float d2 = 2.0f * dv_dot(dir, normal);
    const float ox = dir.x - normal.x * d2;
    const float oy = dir.y - normal.y * d2;
    const float oz = dir.z - normal.z * d2;
    const float sq = ox * ox + oy * oy + oz * oz;
    if (sq < 1e-18f || !isfinite(sq)) {
        return false;
    }

    const float inv = rsqrtf(sq);
    out_dir = dv(ox * inv, oy * inv, oz * inv);
    return true;
}

struct DTraceResult
{
    DVec3 position;
    float weight;
    bool valid;
};

__device__ __forceinline__ DTraceResult d_trace_ghost_ray(const DRay& ray_in,
                                                          const Surface* surfaces,
                                                          int num_surfaces,
                                                          float sensor_z,
                                                          int bounce_a,
                                                          int bounce_b,
                                                          float lambda_nm)
{
    DRay ray = ray_in;
    float current_ior = 1.0f;
    float weight = 1.0f;

    for (int s = 0; s <= bounce_b; ++s) {
        DVec3 hit;
        DVec3 normal;
        if (!d_intersect_surface(ray, surfaces[s], hit, normal)) {
            return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
        }

        ray.origin = hit;
        const float n1 = current_ior;
        const float n2 = d_ior_at(surfaces[s], lambda_nm);
        const float cos_i = fabsf(dv_dot(normal, ray.dir));
        const float R = d_surface_reflectance(cos_i, n1, n2, surfaces[s].coating, lambda_nm);

        if (s == bounce_b) {
            DVec3 reflected;
            if (!d_reflect(ray.dir, normal, reflected)) {
                return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
            }

            ray.dir = reflected;
            weight *= R;
        } else {
            DVec3 refracted;
            if (!d_refract(ray.dir, normal, n1 / n2, refracted)) {
                return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
            }

            ray.dir = refracted;
            weight *= (1.0f - R);
            current_ior = n2;
        }
    }

    for (int s = bounce_b - 1; s >= bounce_a; --s) {
        DVec3 hit;
        DVec3 normal;
        if (!d_intersect_surface(ray, surfaces[s], hit, normal)) {
            return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
        }

        ray.origin = hit;
        const float n1 = current_ior;
        const float n2 = d_ior_before(surfaces, s, lambda_nm);
        const float cos_i = fabsf(dv_dot(normal, ray.dir));
        const float R = d_surface_reflectance(cos_i, n1, n2, surfaces[s].coating, lambda_nm);

        if (s == bounce_a) {
            DVec3 reflected;
            if (!d_reflect(ray.dir, normal, reflected)) {
                return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
            }

            ray.dir = reflected;
            weight *= R;
            current_ior = d_ior_at(surfaces[bounce_a], lambda_nm);
        } else {
            DVec3 refracted;
            if (!d_refract(ray.dir, normal, n1 / n2, refracted)) {
                return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
            }

            ray.dir = refracted;
            weight *= (1.0f - R);
            current_ior = n2;
        }
    }

    for (int s = bounce_a + 1; s < num_surfaces; ++s) {
        DVec3 hit;
        DVec3 normal;
        if (!d_intersect_surface(ray, surfaces[s], hit, normal)) {
            return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
        }

        ray.origin = hit;
        const float n1 = current_ior;
        const float n2 = d_ior_at(surfaces[s], lambda_nm);
        const float cos_i = fabsf(dv_dot(normal, ray.dir));
        const float R = d_surface_reflectance(cos_i, n1, n2, surfaces[s].coating, lambda_nm);

        DVec3 refracted;
        if (!d_refract(ray.dir, normal, n1 / n2, refracted)) {
            return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
        }

        ray.dir = refracted;
        weight *= (1.0f - R);
        current_ior = n2;
    }

    if (fabsf(ray.dir.z) < 1e-12f) {
        return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
    }

    const float t = (sensor_z - ray.origin.z) / ray.dir.z;
    if (!(t > 0.0f)) {
        return {dv(0.0f, 0.0f, 0.0f), 0.0f, false};
    }

    const DVec3 pos = dv(ray.origin.x + ray.dir.x * t,
                         ray.origin.y + ray.dir.y * t,
                         ray.origin.z + ray.dir.z * t);
    return {pos, weight, true};
}

struct GPUPair
{
    int surf_a;
    int surf_b;
    float area_boost;
};

struct GPUSource
{
    float angle_x;
    float angle_y;
    float r;
    float g;
    float b;
};

struct GPUSample
{
    float u;
    float v;
};

constexpr int kBlockSize = 256;

__global__ void ghost_kernel(const Surface* surfaces,
                             int num_surfaces,
                             float sensor_z,
                             const GPUPair* pairs,
                             int num_sources,
                             const GPUSource* sources,
                             const GPUSample* grid_samples,
                             int num_grid_samples,
                             float front_radius,
                             float start_z,
                             float sensor_half_w,
                             float sensor_half_h,
                             int width,
                             int height,
                             float* out_r,
                             float* out_g,
                             float* out_b,
                             float gain,
                             float ray_weight,
                             const float* wavelengths)
{
    const int pair_source_index = static_cast<int>(blockIdx.x);
    const int pair_index = pair_source_index / num_sources;
    const int source_index = pair_source_index % num_sources;
    const int grid_index = static_cast<int>(blockIdx.y) * kBlockSize + static_cast<int>(threadIdx.x);

    if (grid_index >= num_grid_samples) {
        return;
    }

    const GPUPair& pair = pairs[pair_index];
    if (pair.surf_a < 0 || pair.surf_b <= pair.surf_a || pair.surf_b >= num_surfaces) {
        return;
    }

    const GPUSource& source = sources[source_index];
    const GPUSample& grid_sample = grid_samples[grid_index];

    const DVec3 beam_dir = dv_normalize(dv(tanf(source.angle_x), tanf(source.angle_y), 1.0f));

    DRay ray;
    ray.origin = dv(grid_sample.u * front_radius, grid_sample.v * front_radius, start_z);
    ray.dir = beam_dir;

    for (int channel = 0; channel < 3; ++channel) {
        const DTraceResult result = d_trace_ghost_ray(
            ray,
            surfaces,
            num_surfaces,
            sensor_z,
            pair.surf_a,
            pair.surf_b,
            wavelengths[channel]);

        if (!result.valid || !isfinite(result.position.x) || !isfinite(result.position.y)) {
            continue;
        }

        const float source_intensity = (channel == 0) ? source.r : (channel == 1) ? source.g : source.b;
        const float contribution = source_intensity * result.weight * ray_weight * gain * pair.area_boost;
        if (contribution < 1e-12f) {
            continue;
        }

        const float px = (result.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
        const float py = (result.position.y / (2.0f * sensor_half_h) + 0.5f) * height;
        if (!isfinite(px) || !isfinite(py)) {
            continue;
        }

        const int x0 = static_cast<int>(floorf(px - 0.5f));
        const int y0 = static_cast<int>(floorf(py - 0.5f));
        const float fx = (px - 0.5f) - static_cast<float>(x0);
        const float fy = (py - 0.5f) - static_cast<float>(y0);
        const float w00 = (1.0f - fx) * (1.0f - fy);
        const float w10 = fx * (1.0f - fy);
        const float w01 = (1.0f - fx) * fy;
        const float w11 = fx * fy;

        float* channel_out = (channel == 0) ? out_r : (channel == 1) ? out_g : out_b;

        if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height) {
            atomicAdd(&channel_out[y0 * width + x0], contribution * w00);
        }
        if (x0 + 1 >= 0 && x0 + 1 < width && y0 >= 0 && y0 < height) {
            atomicAdd(&channel_out[y0 * width + (x0 + 1)], contribution * w10);
        }
        if (x0 >= 0 && x0 < width && y0 + 1 >= 0 && y0 + 1 < height) {
            atomicAdd(&channel_out[(y0 + 1) * width + x0], contribution * w01);
        }
        if (x0 + 1 >= 0 && x0 + 1 < width && y0 + 1 >= 0 && y0 + 1 < height) {
            atomicAdd(&channel_out[(y0 + 1) * width + (x0 + 1)], contribution * w11);
        }
    }
}

void report_cuda_error(cudaError_t error, const char* site, std::string* out_error)
{
    std::fprintf(stderr, "FlareSim CUDA error at %s: %s\n", site, cudaGetErrorString(error));
    if (out_error && out_error->empty()) {
        *out_error = std::string("CUDA error at ") + site + ": " + cudaGetErrorString(error);
    }
}

} // namespace

void GpuBufferCache::release()
{
    cudaFree(d_surfs);
    cudaFree(d_pairs);
    cudaFree(d_src);
    cudaFree(d_grid);
    cudaFree(d_out_r);
    cudaFree(d_out_g);
    cudaFree(d_out_b);

    d_surfs = nullptr;
    d_pairs = nullptr;
    d_src = nullptr;
    d_grid = nullptr;
    d_out_r = nullptr;
    d_out_g = nullptr;
    d_out_b = nullptr;

    surfs_bytes = 0;
    pairs_bytes = 0;
    src_bytes = 0;
    grid_bytes = 0;
    out_floats = 0;
}

bool cuda_ghost_renderer_compiled()
{
    return true;
}

bool cuda_ghost_renderer_available(std::string* reason)
{
    int device_count = 0;
    const cudaError_t error = cudaGetDeviceCount(&device_count);

    if (error == cudaSuccess && device_count > 0) {
        if (reason) {
            reason->clear();
        }
        return true;
    }

    if (reason) {
        if (error == cudaErrorInsufficientDriver) {
            *reason = "The NVIDIA driver is too old for the CUDA toolkit used to build FlareSim.";
        } else if (error == cudaErrorNoDevice || device_count == 0) {
            *reason = "No compatible NVIDIA CUDA GPU was detected.";
        } else {
            *reason = std::string("CUDA initialisation failed: ") + cudaGetErrorString(error);
        }
    }

    return false;
}

bool launch_ghost_cuda(const LensSystem& lens,
                       const std::vector<GhostPair>& active_pairs,
                       const std::vector<float>& pair_area_boosts,
                       const std::vector<BrightPixel>& sources,
                       float sensor_half_w,
                       float sensor_half_h,
                       float* out_r,
                       float* out_g,
                       float* out_b,
                       int width,
                       int height,
                       const GhostConfig& config,
                       GpuBufferCache& cache,
                       std::string* out_error)
{
    if (!out_r || !out_g || !out_b || width <= 0 || height <= 0) {
        if (out_error) {
            *out_error = "Invalid output buffers for CUDA ghost render.";
        }
        return false;
    }

    if (active_pairs.empty() || sources.empty()) {
        return true;
    }

    if (!cuda_ghost_renderer_available(out_error)) {
        return false;
    }

    const int num_surfaces = lens.num_surfaces();
    if (num_surfaces <= 0) {
        return true;
    }

    std::vector<GPUSample> grid_samples;
    grid_samples.reserve(static_cast<std::size_t>(config.ray_grid) * static_cast<std::size_t>(config.ray_grid));
    for (int gy = 0; gy < config.ray_grid; ++gy) {
        for (int gx = 0; gx < config.ray_grid; ++gx) {
            const float u = ((gx + 0.5f) / config.ray_grid) * 2.0f - 1.0f;
            const float v = ((gy + 0.5f) / config.ray_grid) * 2.0f - 1.0f;
            if (u * u + v * v <= 1.0f) {
                grid_samples.push_back({u, v});
            }
        }
    }

    const int num_grid_samples = static_cast<int>(grid_samples.size());
    if (num_grid_samples == 0) {
        return true;
    }

    const float wavelengths[3] = {
        config.wavelengths[0],
        config.wavelengths[1],
        config.wavelengths[2],
    };

    std::vector<GPUPair> gpu_pairs(active_pairs.size());
    for (std::size_t i = 0; i < active_pairs.size(); ++i) {
        gpu_pairs[i] = {
            active_pairs[i].surf_a,
            active_pairs[i].surf_b,
            pair_area_boosts[i],
        };
    }

    std::vector<GPUSource> gpu_sources(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        gpu_sources[i] = {
            sources[i].angle_x,
            sources[i].angle_y,
            sources[i].r,
            sources[i].g,
            sources[i].b,
        };
    }

    const std::size_t num_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    auto ensure_buffer = [&](void*& ptr, std::size_t& cap, std::size_t need, const char* label) -> bool {
        if (need <= cap) {
            return true;
        }

        cudaFree(ptr);
        ptr = nullptr;
        cap = 0;

        const cudaError_t error = cudaMalloc(&ptr, need);
        if (error != cudaSuccess) {
            report_cuda_error(error, label, out_error);
            return false;
        }

        cap = need;
        return true;
    };

    if (!ensure_buffer(cache.d_surfs, cache.surfs_bytes, static_cast<std::size_t>(num_surfaces) * sizeof(Surface), "d_surfs")) {
        return false;
    }
    if (!ensure_buffer(cache.d_pairs, cache.pairs_bytes, gpu_pairs.size() * sizeof(GPUPair), "d_pairs")) {
        return false;
    }
    if (!ensure_buffer(cache.d_src, cache.src_bytes, gpu_sources.size() * sizeof(GPUSource), "d_src")) {
        return false;
    }
    if (!ensure_buffer(cache.d_grid, cache.grid_bytes, grid_samples.size() * sizeof(GPUSample), "d_grid")) {
        return false;
    }

    if (num_pixels > cache.out_floats) {
        cudaFree(cache.d_out_r);
        cudaFree(cache.d_out_g);
        cudaFree(cache.d_out_b);
        cache.d_out_r = nullptr;
        cache.d_out_g = nullptr;
        cache.d_out_b = nullptr;
        cache.out_floats = 0;

        cudaError_t error = cudaMalloc(&cache.d_out_r, num_pixels * sizeof(float));
        if (error != cudaSuccess) {
            report_cuda_error(error, "d_out_r", out_error);
            return false;
        }

        error = cudaMalloc(&cache.d_out_g, num_pixels * sizeof(float));
        if (error != cudaSuccess) {
            report_cuda_error(error, "d_out_g", out_error);
            return false;
        }

        error = cudaMalloc(&cache.d_out_b, num_pixels * sizeof(float));
        if (error != cudaSuccess) {
            report_cuda_error(error, "d_out_b", out_error);
            return false;
        }

        cache.out_floats = num_pixels;
    }

#define GPU_CHECK(call)                                                                 \
    do {                                                                                \
        const cudaError_t error__ = (call);                                             \
        if (error__ != cudaSuccess) {                                                   \
            report_cuda_error(error__, #call, out_error);                               \
            return false;                                                               \
        }                                                                               \
    } while (0)

    GPU_CHECK(cudaMemcpy(cache.d_surfs,
                         lens.surfaces.data(),
                         static_cast<std::size_t>(num_surfaces) * sizeof(Surface),
                         cudaMemcpyHostToDevice));
    GPU_CHECK(cudaMemcpy(cache.d_pairs,
                         gpu_pairs.data(),
                         gpu_pairs.size() * sizeof(GPUPair),
                         cudaMemcpyHostToDevice));
    GPU_CHECK(cudaMemcpy(cache.d_src,
                         gpu_sources.data(),
                         gpu_sources.size() * sizeof(GPUSource),
                         cudaMemcpyHostToDevice));
    GPU_CHECK(cudaMemcpy(cache.d_grid,
                         grid_samples.data(),
                         grid_samples.size() * sizeof(GPUSample),
                         cudaMemcpyHostToDevice));

    GPU_CHECK(cudaMemset(cache.d_out_r, 0, num_pixels * sizeof(float)));
    GPU_CHECK(cudaMemset(cache.d_out_g, 0, num_pixels * sizeof(float)));
    GPU_CHECK(cudaMemset(cache.d_out_b, 0, num_pixels * sizeof(float)));

    float* d_wavelengths = nullptr;
    GPU_CHECK(cudaMalloc(&d_wavelengths, sizeof(wavelengths)));
    GPU_CHECK(cudaMemcpy(d_wavelengths, wavelengths, sizeof(wavelengths), cudaMemcpyHostToDevice));

    const dim3 block(kBlockSize, 1, 1);
    const dim3 grid(static_cast<unsigned>(gpu_pairs.size() * gpu_sources.size()),
                    static_cast<unsigned>((num_grid_samples + kBlockSize - 1) / kBlockSize),
                    1);

    ghost_kernel<<<grid, block>>>(
        static_cast<Surface*>(cache.d_surfs),
        num_surfaces,
        lens.sensor_z,
        static_cast<GPUPair*>(cache.d_pairs),
        static_cast<int>(gpu_sources.size()),
        static_cast<GPUSource*>(cache.d_src),
        static_cast<GPUSample*>(cache.d_grid),
        num_grid_samples,
        lens.surfaces[0].semi_aperture,
        lens.surfaces[0].z - 20.0f,
        sensor_half_w,
        sensor_half_h,
        width,
        height,
        cache.d_out_r,
        cache.d_out_g,
        cache.d_out_b,
        config.gain,
        1.0f / static_cast<float>(num_grid_samples),
        d_wavelengths);

    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess) {
        cudaFree(d_wavelengths);
        report_cuda_error(error, "ghost_kernel", out_error);
        return false;
    }

    error = cudaDeviceSynchronize();
    if (error != cudaSuccess) {
        cudaFree(d_wavelengths);
        report_cuda_error(error, "cudaDeviceSynchronize", out_error);
        return false;
    }

    GPU_CHECK(cudaMemcpy(out_r, cache.d_out_r, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));
    GPU_CHECK(cudaMemcpy(out_g, cache.d_out_g, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));
    GPU_CHECK(cudaMemcpy(out_b, cache.d_out_b, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_wavelengths);

#undef GPU_CHECK

    return true;
}
