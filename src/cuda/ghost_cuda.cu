#include "ghost_cuda.h"

#include <cuda_runtime.h>

#include <algorithm>
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

__device__ __forceinline__ bool d_is_valid_pupil_sample(float u,
                                                        float v,
                                                        int aperture_blades,
                                                        float aperture_rotation_rad)
{
    const float r2 = u * u + v * v;
    if (r2 > 1.0f) {
        return false;
    }

    if (aperture_blades < 3) {
        return true;
    }

    const float sector_angle = 2.0f * 3.14159265358979323846f / aperture_blades;
    const float apothem = cosf(3.14159265358979323846f / aperture_blades);
    float angle = atan2f(v, u) - aperture_rotation_rad;
    float sector = fmodf(angle, sector_angle);
    if (sector < 0.0f) {
        sector += sector_angle;
    }

    return sqrtf(r2) * cosf(sector - sector_angle * 0.5f) <= apothem;
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

struct DFootprint
{
    float area_px2;
    float anisotropy;
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

__device__ __forceinline__ bool d_trace_ghost_sensor_position_px(const Surface* surfaces,
                                                                 int num_surfaces,
                                                                 float sensor_z,
                                                                 int bounce_a,
                                                                 int bounce_b,
                                                                 const DVec3& beam_dir,
                                                                 float u,
                                                                 float v,
                                                                 float wavelength_nm,
                                                                 float front_radius,
                                                                 float start_z,
                                                                 float sensor_half_w,
                                                                 float sensor_half_h,
                                                                 int width,
                                                                 int height,
                                                                 float& out_px,
                                                                 float& out_py,
                                                                 float* out_weight = nullptr)
{
    DRay ray;
    ray.origin = dv(u * front_radius, v * front_radius, start_z);
    ray.dir = beam_dir;

    const DTraceResult result = d_trace_ghost_ray(ray,
                                                  surfaces,
                                                  num_surfaces,
                                                  sensor_z,
                                                  bounce_a,
                                                  bounce_b,
                                                  wavelength_nm);
    if (!result.valid) {
        return false;
    }

    out_px = (result.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
    out_py = (result.position.y / (2.0f * sensor_half_h) + 0.5f) * height;
    if (out_weight) {
        *out_weight = result.weight;
    }
    return isfinite(out_px) && isfinite(out_py);
}

__device__ __forceinline__ bool d_choose_probe_offset(float u,
                                                      float v,
                                                      float step,
                                                      bool along_u,
                                                      int aperture_blades,
                                                      float aperture_rotation_rad,
                                                      float& out_probe_u,
                                                      float& out_probe_v,
                                                      float& out_delta)
{
    const float primary_u = along_u ? (u + step) : u;
    const float primary_v = along_u ? v : (v + step);
    if (d_is_valid_pupil_sample(primary_u, primary_v, aperture_blades, aperture_rotation_rad)) {
        out_probe_u = primary_u;
        out_probe_v = primary_v;
        out_delta = step;
        return true;
    }

    const float secondary_u = along_u ? (u - step) : u;
    const float secondary_v = along_u ? v : (v - step);
    if (d_is_valid_pupil_sample(secondary_u, secondary_v, aperture_blades, aperture_rotation_rad)) {
        out_probe_u = secondary_u;
        out_probe_v = secondary_v;
        out_delta = -step;
        return true;
    }

    return false;
}

__device__ __forceinline__ float d_select_ghost_footprint_radius(float fallback_radius_px,
                                                                 float footprint_area_px2,
                                                                 float anisotropy,
                                                                 float footprint_radius_bias,
                                                                 float footprint_clamp)
{
    float fallback = fminf(fmaxf(fallback_radius_px, 1.25f), 16.0f);
    if (!(footprint_area_px2 > 0.0f) || !isfinite(footprint_area_px2)) {
        return fallback;
    }

    const float bias = fminf(fmaxf(footprint_radius_bias, 0.25f), 2.0f);
    const float clamp_scale = fminf(fmaxf(footprint_clamp, 0.5f), 4.0f);
    float radius = sqrtf(footprint_area_px2) * 0.75f * bias;
    if (isfinite(anisotropy) && anisotropy > 2.5f) {
        radius *= 0.85f;
    }

    radius = fminf(fmaxf(radius, 1.15f), 16.0f);
    return fminf(fmaxf(fminf(radius, fallback * clamp_scale), 1.15f), 16.0f);
}

__device__ __forceinline__ float d_select_ghost_density_boost(float pair_area_boost,
                                                              float reference_footprint_area_px2,
                                                              float local_footprint_area_px2)
{
    const float pair_boost = fmaxf(pair_area_boost, 1.0f);
    if (!(reference_footprint_area_px2 > 0.0f) ||
        !(local_footprint_area_px2 > 0.0f) ||
        !isfinite(reference_footprint_area_px2) ||
        !isfinite(local_footprint_area_px2)) {
        return pair_boost;
    }

    const float area_ratio = local_footprint_area_px2 / reference_footprint_area_px2;
    const float local_scale = fminf(fmaxf(sqrtf(area_ratio), 0.5f), 2.5f);
    return pair_boost * local_scale;
}

__device__ __forceinline__ DFootprint d_estimate_ghost_sample_footprint(const Surface* surfaces,
                                                                         int num_surfaces,
                                                                         float sensor_z,
                                                                         int bounce_a,
                                                                         int bounce_b,
                                                                         const DVec3& beam_dir,
                                                                         float u,
                                                                         float v,
                                                                         float cell_size,
                                                                         float wavelength_nm,
                                                                         float front_radius,
                                                                         float start_z,
                                                                         float sensor_half_w,
                                                                         float sensor_half_h,
                                                                         int width,
                                                                         int height,
                                                                         int aperture_blades,
                                                                         float aperture_rotation_rad)
{
    DFootprint footprint {0.0f, 1.0f, false};

    float center_px = 0.0f;
    float center_py = 0.0f;
    if (!d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                          bounce_a, bounce_b, beam_dir,
                                          u, v, wavelength_nm,
                                          front_radius, start_z,
                                          sensor_half_w, sensor_half_h,
                                          width, height,
                                          center_px, center_py)) {
        return footprint;
    }

    const float probe_step = fmaxf(cell_size * 0.5f, 1.0e-3f);
    float probe_u_u = 0.0f;
    float probe_u_v = 0.0f;
    float delta_u = 0.0f;
    float probe_v_u = 0.0f;
    float probe_v_v = 0.0f;
    float delta_v = 0.0f;
    if (!d_choose_probe_offset(u, v, probe_step, true,
                               aperture_blades, aperture_rotation_rad,
                               probe_u_u, probe_u_v, delta_u) ||
        !d_choose_probe_offset(u, v, probe_step, false,
                               aperture_blades, aperture_rotation_rad,
                               probe_v_u, probe_v_v, delta_v)) {
        return footprint;
    }

    float probe_px_u = 0.0f;
    float probe_py_u = 0.0f;
    float probe_px_v = 0.0f;
    float probe_py_v = 0.0f;
    if (!d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                          bounce_a, bounce_b, beam_dir,
                                          probe_u_u, probe_u_v, wavelength_nm,
                                          front_radius, start_z,
                                          sensor_half_w, sensor_half_h,
                                          width, height,
                                          probe_px_u, probe_py_u) ||
        !d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                          bounce_a, bounce_b, beam_dir,
                                          probe_v_u, probe_v_v, wavelength_nm,
                                          front_radius, start_z,
                                          sensor_half_w, sensor_half_h,
                                          width, height,
                                          probe_px_v, probe_py_v)) {
        return footprint;
    }

    const float inv_delta_u = 1.0f / delta_u;
    const float inv_delta_v = 1.0f / delta_v;
    const float dpx_du = (probe_px_u - center_px) * inv_delta_u;
    const float dpy_du = (probe_py_u - center_py) * inv_delta_u;
    const float dpx_dv = (probe_px_v - center_px) * inv_delta_v;
    const float dpy_dv = (probe_py_v - center_py) * inv_delta_v;

    const float axis_u = sqrtf(dpx_du * dpx_du + dpy_du * dpy_du) * cell_size;
    const float axis_v = sqrtf(dpx_dv * dpx_dv + dpy_dv * dpy_dv) * cell_size;
    const float minor_axis = fmaxf(fminf(axis_u, axis_v), 1.0e-3f);
    const float major_axis = fmaxf(axis_u, axis_v);
    footprint.anisotropy = major_axis / minor_axis;
    footprint.area_px2 = fabsf(dpx_du * dpy_dv - dpy_du * dpx_dv) * cell_size * cell_size;
    footprint.valid = isfinite(footprint.area_px2) && footprint.area_px2 > 0.0f;
    return footprint;
}

struct GPUPair
{
    int surf_a;
    int surf_b;
    float area_boost;
    float splat_radius_px;
    float reference_footprint_area_px2;
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

struct GPUCell
{
    float u0;
    float v0;
    float u1;
    float v1;
    float uc;
    float vc;
};

static_assert(sizeof(GPUSample) == sizeof(GhostGridSample));
static_assert(sizeof(GPUCell) == sizeof(GhostGridCell));

struct DPixelPoint
{
    float x;
    float y;
};

struct GPUSpectralSampleDev
{
    float lambda;
    float rw;
    float gw;
    float bw;
};

constexpr int kBlockSize = 256;

__device__ __forceinline__ void d_atomic_splat(float* out,
                                               int width,
                                               int height,
                                               float px,
                                               float py,
                                               float value,
                                               float radius)
{
    if (value <= 1.0e-14f) {
        return;
    }

    if (radius <= 1.5f) {
        const int x0 = static_cast<int>(floorf(px - 0.5f));
        const int y0 = static_cast<int>(floorf(py - 0.5f));
        const float fx = (px - 0.5f) - static_cast<float>(x0);
        const float fy = (py - 0.5f) - static_cast<float>(y0);
        const float w00 = (1.0f - fx) * (1.0f - fy);
        const float w10 = fx * (1.0f - fy);
        const float w01 = (1.0f - fx) * fy;
        const float w11 = fx * fy;

        if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height) {
            atomicAdd(&out[y0 * width + x0], value * w00);
        }
        if (x0 + 1 >= 0 && x0 + 1 < width && y0 >= 0 && y0 < height) {
            atomicAdd(&out[y0 * width + (x0 + 1)], value * w10);
        }
        if (x0 >= 0 && x0 < width && y0 + 1 >= 0 && y0 + 1 < height) {
            atomicAdd(&out[(y0 + 1) * width + x0], value * w01);
        }
        if (x0 + 1 >= 0 && x0 + 1 < width && y0 + 1 >= 0 && y0 + 1 < height) {
            atomicAdd(&out[(y0 + 1) * width + (x0 + 1)], value * w11);
        }
        return;
    }

    const int ix0 = max(static_cast<int>(floorf(px - radius)), 0);
    const int ix1 = min(static_cast<int>(ceilf(px + radius)), width - 1);
    const int iy0 = max(static_cast<int>(floorf(py - radius)), 0);
    const int iy1 = min(static_cast<int>(ceilf(py + radius)), height - 1);
    const float inv_r = 1.0f / radius;
    const float norm = value / (radius * radius);

    for (int y = iy0; y <= iy1; ++y) {
        const float wy = fmaxf(1.0f - fabsf((y + 0.5f) - py) * inv_r, 0.0f);
        if (wy <= 0.0f) {
            continue;
        }
        const float row_scale = wy * norm;
        const int row = y * width;
        for (int x = ix0; x <= ix1; ++x) {
            const float wx = fmaxf(1.0f - fabsf((x + 0.5f) - px) * inv_r, 0.0f);
            if (wx > 0.0f) {
                atomicAdd(&out[row + x], row_scale * wx);
            }
        }
    }
}

__device__ __forceinline__ float d_signed_triangle_twice_area(const DPixelPoint& a,
                                                              const DPixelPoint& b,
                                                              const DPixelPoint& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

__device__ __forceinline__ void d_rasterize_triangle_linear(float* out,
                                                            int width,
                                                            int height,
                                                            const DPixelPoint& a,
                                                            const DPixelPoint& b,
                                                            const DPixelPoint& c,
                                                            float va,
                                                            float vb,
                                                            float vc,
                                                            float density_scale)
{
    const float twice_area_signed = d_signed_triangle_twice_area(a, b, c);
    const float twice_area = fabsf(twice_area_signed);
    const float area = 0.5f * twice_area;
    if (!(area > 1.0e-4f) || !isfinite(area) || density_scale <= 1.0e-14f) {
        return;
    }

    const int x0 = max(static_cast<int>(floorf(fminf(fminf(a.x, b.x), c.x) - 0.5f)), 0);
    const int x1 = min(static_cast<int>(ceilf(fmaxf(fmaxf(a.x, b.x), c.x) + 0.5f)), width - 1);
    const int y0 = max(static_cast<int>(floorf(fminf(fminf(a.y, b.y), c.y) - 0.5f)), 0);
    const int y1 = min(static_cast<int>(ceilf(fmaxf(fmaxf(a.y, b.y), c.y) + 0.5f)), height - 1);
    if (x0 > x1 || y0 > y1) {
        return;
    }

    const float sign = twice_area_signed >= 0.0f ? 1.0f : -1.0f;
    const float inv_twice_area = 1.0f / twice_area;

    for (int y = y0; y <= y1; ++y) {
        const float py = y + 0.5f;
        const int row = y * width;
        for (int x = x0; x <= x1; ++x) {
            const float px = x + 0.5f;
            const float w0 = sign * ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) * inv_twice_area;
            const float w1 = sign * ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) * inv_twice_area;
            const float w2 = sign * ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) * inv_twice_area;
            if (w0 >= -1.0e-4f && w1 >= -1.0e-4f && w2 >= -1.0e-4f) {
                const float density = fmaxf(0.0f, va * w2 + vb * w0 + vc * w1);
                atomicAdd(&out[row + x], density * density_scale);
            }
        }
    }
}

__device__ __forceinline__ void d_rasterize_quad_linear(float* out,
                                                        int width,
                                                        int height,
                                                        const DPixelPoint& p00,
                                                        const DPixelPoint& p10,
                                                        const DPixelPoint& p11,
                                                        const DPixelPoint& p01,
                                                        float v00,
                                                        float v10,
                                                        float v11,
                                                        float v01,
                                                        float total_value)
{
    const float area0 = 0.5f * fabsf(d_signed_triangle_twice_area(p00, p10, p11));
    const float area1 = 0.5f * fabsf(d_signed_triangle_twice_area(p00, p11, p01));
    const float total_area = area0 + area1;
    if (!(total_area > 1.0e-4f) || !isfinite(total_area) || total_value <= 1.0e-14f) {
        const float center_x = 0.25f * (p00.x + p10.x + p11.x + p01.x);
        const float center_y = 0.25f * (p00.y + p10.y + p11.y + p01.y);
        d_atomic_splat(out, width, height, center_x, center_y, total_value, 1.5f);
        return;
    }

    const float weighted_integral =
        area0 * (v00 + v10 + v11) / 3.0f +
        area1 * (v00 + v11 + v01) / 3.0f;
    if (!(weighted_integral > 1.0e-6f) || !isfinite(weighted_integral)) {
        const float center_x = 0.25f * (p00.x + p10.x + p11.x + p01.x);
        const float center_y = 0.25f * (p00.y + p10.y + p11.y + p01.y);
        d_atomic_splat(out, width, height, center_x, center_y, total_value, 1.5f);
        return;
    }

    const float density_scale = total_value / weighted_integral;
    d_rasterize_triangle_linear(out, width, height, p00, p10, p11, v00, v10, v11, density_scale);
    d_rasterize_triangle_linear(out, width, height, p00, p11, p01, v00, v11, v01, density_scale);
}

__device__ __forceinline__ void d_apply_cell_coverage_bias(DPixelPoint& p00,
                                                           DPixelPoint& p10,
                                                           DPixelPoint& p11,
                                                           DPixelPoint& p01,
                                                           float cell_coverage_bias)
{
    const float bias = fminf(fmaxf(cell_coverage_bias, 0.5f), 2.5f);
    if (fabsf(bias - 1.0f) <= 1.0e-6f) {
        return;
    }

    const DPixelPoint center {
        0.25f * (p00.x + p10.x + p11.x + p01.x),
        0.25f * (p00.y + p10.y + p11.y + p01.y),
    };
    auto scale_point = [&](DPixelPoint& point) {
        point.x = center.x + (point.x - center.x) * bias;
        point.y = center.y + (point.y - center.y) * bias;
    };
    scale_point(p00);
    scale_point(p10);
    scale_point(p11);
    scale_point(p01);
}

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
                             float cell_size,
                             int aperture_blades,
                             float aperture_rotation_rad,
                             float footprint_radius_bias,
                             float footprint_clamp,
                             const GPUSpectralSampleDev* spectral_samples,
                             int num_spectral_samples)
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
    if (!d_is_valid_pupil_sample(grid_sample.u, grid_sample.v, aperture_blades, aperture_rotation_rad)) {
        return;
    }

    const DVec3 beam_dir = dv_normalize(dv(tanf(source.angle_x), tanf(source.angle_y), 1.0f));

    DRay ray;
    ray.origin = dv(grid_sample.u * front_radius, grid_sample.v * front_radius, start_z);
    ray.dir = beam_dir;
    float splat_radius_px = pair.splat_radius_px;
    float density_boost = pair.area_boost;
    const DFootprint footprint = d_estimate_ghost_sample_footprint(surfaces,
                                                                   num_surfaces,
                                                                   sensor_z,
                                                                   pair.surf_a,
                                                                   pair.surf_b,
                                                                   beam_dir,
                                                                   grid_sample.u,
                                                                   grid_sample.v,
                                                                   cell_size,
                                                                   spectral_samples[1].lambda,
                                                                   front_radius,
                                                                   start_z,
                                                                   sensor_half_w,
                                                                   sensor_half_h,
                                                                   width,
                                                                   height,
                                                                   aperture_blades,
                                                                   aperture_rotation_rad);
    if (footprint.valid) {
        density_boost = d_select_ghost_density_boost(pair.area_boost,
                                                     pair.reference_footprint_area_px2,
                                                     footprint.area_px2);
        splat_radius_px = d_select_ghost_footprint_radius(pair.splat_radius_px,
                                                          footprint.area_px2,
                                                          footprint.anisotropy,
                                                          footprint_radius_bias,
                                                          footprint_clamp);
    }

    for (int sample_index = 0; sample_index < num_spectral_samples; ++sample_index) {
        const GPUSpectralSampleDev& sample = spectral_samples[sample_index];
        const DTraceResult result = d_trace_ghost_ray(
            ray,
            surfaces,
            num_surfaces,
            sensor_z,
            pair.surf_a,
            pair.surf_b,
            sample.lambda);

        if (!result.valid || !isfinite(result.position.x) || !isfinite(result.position.y)) {
            continue;
        }

        const float contribution = result.weight * ray_weight * gain * density_boost;
        if (contribution < 1e-12f) {
            continue;
        }

        const float px = (result.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
        const float py = (result.position.y / (2.0f * sensor_half_h) + 0.5f) * height;
        if (!isfinite(px) || !isfinite(py)) {
            continue;
        }

        const float cr = source.r * sample.rw * contribution;
        const float cg = source.g * sample.gw * contribution;
        const float cb = source.b * sample.bw * contribution;
        d_atomic_splat(out_r, width, height, px, py, cr, splat_radius_px);
        d_atomic_splat(out_g, width, height, px, py, cg, splat_radius_px);
        d_atomic_splat(out_b, width, height, px, py, cb, splat_radius_px);
    }
}

__global__ void ghost_cell_kernel(const Surface* surfaces,
                                  int num_surfaces,
                                  float sensor_z,
                                  const GPUPair* pairs,
                                  int num_sources,
                                  const GPUSource* sources,
                                  const GPUCell* grid_cells,
                                  int num_grid_cells,
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
                                  float cell_weight,
                                  int aperture_blades,
                                  float aperture_rotation_rad,
                                  float cell_edge_inset,
                                  float cell_coverage_bias,
                                  const GPUSpectralSampleDev* spectral_samples,
                                  int num_spectral_samples)
{
    const int pair_source_index = static_cast<int>(blockIdx.x);
    const int pair_index = pair_source_index / num_sources;
    const int source_index = pair_source_index % num_sources;
    const int cell_index = static_cast<int>(blockIdx.y) * kBlockSize + static_cast<int>(threadIdx.x);

    if (cell_index >= num_grid_cells) {
        return;
    }

    const GPUPair& pair = pairs[pair_index];
    if (pair.surf_a < 0 || pair.surf_b <= pair.surf_a || pair.surf_b >= num_surfaces) {
        return;
    }

    const GPUSource& source = sources[source_index];
    const GPUCell& cell = grid_cells[cell_index];
    const DVec3 beam_dir = dv_normalize(dv(tanf(source.angle_x), tanf(source.angle_y), 1.0f));
    const float inset = fminf(fmaxf(cell_edge_inset, 0.0f), 0.45f);
    const float trace_scale = 1.0f - inset;
    const float inset_u00 = cell.uc + (cell.u0 - cell.uc) * trace_scale;
    const float inset_v00 = cell.vc + (cell.v0 - cell.vc) * trace_scale;
    const float inset_u10 = cell.uc + (cell.u1 - cell.uc) * trace_scale;
    const float inset_v10 = cell.vc + (cell.v0 - cell.vc) * trace_scale;
    const float inset_u11 = cell.uc + (cell.u1 - cell.uc) * trace_scale;
    const float inset_v11 = cell.vc + (cell.v1 - cell.vc) * trace_scale;
    const float inset_u01 = cell.uc + (cell.u0 - cell.uc) * trace_scale;
    const float inset_v01 = cell.vc + (cell.v1 - cell.vc) * trace_scale;
    const float cell_u00 = d_is_valid_pupil_sample(cell.u0, cell.v0, aperture_blades, aperture_rotation_rad) ? cell.u0 : inset_u00;
    const float cell_v00 = d_is_valid_pupil_sample(cell.u0, cell.v0, aperture_blades, aperture_rotation_rad) ? cell.v0 : inset_v00;
    const float cell_u10 = d_is_valid_pupil_sample(cell.u1, cell.v0, aperture_blades, aperture_rotation_rad) ? cell.u1 : inset_u10;
    const float cell_v10 = d_is_valid_pupil_sample(cell.u1, cell.v0, aperture_blades, aperture_rotation_rad) ? cell.v0 : inset_v10;
    const float cell_u11 = d_is_valid_pupil_sample(cell.u1, cell.v1, aperture_blades, aperture_rotation_rad) ? cell.u1 : inset_u11;
    const float cell_v11 = d_is_valid_pupil_sample(cell.u1, cell.v1, aperture_blades, aperture_rotation_rad) ? cell.v1 : inset_v11;
    const float cell_u01 = d_is_valid_pupil_sample(cell.u0, cell.v1, aperture_blades, aperture_rotation_rad) ? cell.u0 : inset_u01;
    const float cell_v01 = d_is_valid_pupil_sample(cell.u0, cell.v1, aperture_blades, aperture_rotation_rad) ? cell.v1 : inset_v01;

    for (int sample_index = 0; sample_index < num_spectral_samples; ++sample_index) {
        const GPUSpectralSampleDev& sample = spectral_samples[sample_index];
        float p00x = 0.0f, p00y = 0.0f;
        float p10x = 0.0f, p10y = 0.0f;
        float p11x = 0.0f, p11y = 0.0f;
        float p01x = 0.0f, p01y = 0.0f;
        float w00 = 0.0f;
        float w10 = 0.0f;
        float w11 = 0.0f;
        float w01 = 0.0f;
        if (!d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                              pair.surf_a, pair.surf_b, beam_dir,
                                              cell_u00, cell_v00, sample.lambda,
                                              front_radius, start_z,
                                              sensor_half_w, sensor_half_h,
                                              width, height, p00x, p00y, &w00) ||
            !d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                              pair.surf_a, pair.surf_b, beam_dir,
                                              cell_u10, cell_v10, sample.lambda,
                                              front_radius, start_z,
                                              sensor_half_w, sensor_half_h,
                                              width, height, p10x, p10y, &w10) ||
            !d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                              pair.surf_a, pair.surf_b, beam_dir,
                                              cell_u11, cell_v11, sample.lambda,
                                              front_radius, start_z,
                                              sensor_half_w, sensor_half_h,
                                              width, height, p11x, p11y, &w11) ||
            !d_trace_ghost_sensor_position_px(surfaces, num_surfaces, sensor_z,
                                              pair.surf_a, pair.surf_b, beam_dir,
                                              cell_u01, cell_v01, sample.lambda,
                                              front_radius, start_z,
                                              sensor_half_w, sensor_half_h,
                                              width, height, p01x, p01y, &w01)) {
            continue;
        }

        DPixelPoint p00 {p00x, p00y};
        DPixelPoint p10 {p10x, p10y};
        DPixelPoint p11 {p11x, p11y};
        DPixelPoint p01 {p01x, p01y};
        d_apply_cell_coverage_bias(p00, p10, p11, p01, cell_coverage_bias);
        const float quad_area =
            0.5f * fabsf(d_signed_triangle_twice_area(p00, p10, p11)) +
            0.5f * fabsf(d_signed_triangle_twice_area(p00, p11, p01));
        if (!(quad_area > 1.0e-4f) || !isfinite(quad_area)) {
            continue;
        }
        const float density_boost = d_select_ghost_density_boost(pair.area_boost,
                                                                 pair.reference_footprint_area_px2,
                                                                 quad_area);
        const float weighted_integral =
            0.5f * fabsf(d_signed_triangle_twice_area(p00, p10, p11)) * (w00 + w10 + w11) / 3.0f +
            0.5f * fabsf(d_signed_triangle_twice_area(p00, p11, p01)) * (w00 + w11 + w01) / 3.0f;
        if (!(weighted_integral > 1.0e-6f) || !isfinite(weighted_integral)) {
            continue;
        }
        const float avg_weight = weighted_integral / quad_area;
        const float contribution = avg_weight * cell_weight * gain * density_boost;
        if (contribution < 1.0e-12f) {
            continue;
        }

        const float cr = source.r * sample.rw * contribution;
        const float cg = source.g * sample.gw * contribution;
        const float cb = source.b * sample.bw * contribution;
        d_rasterize_quad_linear(out_r, width, height, p00, p10, p11, p01, w00, w10, w11, w01, cr);
        d_rasterize_quad_linear(out_g, width, height, p00, p10, p11, p01, w00, w10, w11, w01, cg);
        d_rasterize_quad_linear(out_b, width, height, p00, p10, p11, p01, w00, w10, w11, w01, cb);
    }
}

void report_cuda_error(cudaError_t error, const char* site, std::string* out_error)
{
    std::fprintf(stderr, "FlareSim CUDA error at %s: %s\n", site, cudaGetErrorString(error));
    if (out_error && out_error->empty()) {
        *out_error = std::string("CUDA error at ") + site + ": " + cudaGetErrorString(error);
    }
}

[[maybe_unused]] std::vector<GPUSample> build_gpu_grid_samples(int ray_grid,
                                                               int aperture_blades,
                                                               float aperture_rotation_deg)
{
    std::vector<GPUSample> grid_samples;
    grid_samples.reserve(static_cast<std::size_t>(ray_grid) * static_cast<std::size_t>(ray_grid));

    const bool polygonal = aperture_blades >= 3;
    const float rotation = aperture_rotation_deg * 3.14159265358979323846f / 180.0f;
    const float sector_angle = polygonal
        ? (2.0f * 3.14159265358979323846f / aperture_blades)
        : 1.0f;
    const float apothem = polygonal
        ? std::cos(3.14159265358979323846f / aperture_blades)
        : 1.0f;

    for (int gy = 0; gy < ray_grid; ++gy) {
        for (int gx = 0; gx < ray_grid; ++gx) {
            const float u = ((gx + 0.5f) / ray_grid) * 2.0f - 1.0f;
            const float v = ((gy + 0.5f) / ray_grid) * 2.0f - 1.0f;
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
            grid_samples.push_back({u, v});
        }
    }

    return grid_samples;
}

[[maybe_unused]] std::vector<GPUCell> build_gpu_grid_cells(int ray_grid,
                                                           int aperture_blades,
                                                           float aperture_rotation_deg,
                                                           float cell_edge_inset)
{
    std::vector<GPUCell> grid_cells;
    grid_cells.reserve(static_cast<std::size_t>(ray_grid) * static_cast<std::size_t>(ray_grid));
    const float inset = std::clamp(cell_edge_inset, 0.0f, 0.45f);
    const float trace_scale = 1.0f - inset;
    const bool polygonal = aperture_blades >= 3;
    const float rotation = aperture_rotation_deg * 3.14159265358979323846f / 180.0f;
    const float sector_angle = polygonal
        ? (2.0f * 3.14159265358979323846f / aperture_blades)
        : 1.0f;
    const float apothem = polygonal
        ? std::cos(3.14159265358979323846f / aperture_blades)
        : 1.0f;
    auto valid = [&](float u, float v) {
        const float r2 = u * u + v * v;
        if (r2 > 1.0f) {
            return false;
        }
        if (!polygonal) {
            return true;
        }

        float angle = std::atan2(v, u) - rotation;
        float sector = std::fmod(angle, sector_angle);
        if (sector < 0.0f) {
            sector += sector_angle;
        }
        return std::sqrt(r2) * std::cos(sector - sector_angle * 0.5f) <= apothem;
    };

    for (int gy = 0; gy < ray_grid; ++gy) {
        const float v0 = (gy / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
        const float v1 = ((gy + 1) / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
        for (int gx = 0; gx < ray_grid; ++gx) {
            const float u0 = (gx / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
            const float u1 = ((gx + 1) / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
            const float uc = 0.5f * (u0 + u1);
            const float vc = 0.5f * (v0 + v1);
            const float tu0 = uc + (u0 - uc) * trace_scale;
            const float tv0 = vc + (v0 - vc) * trace_scale;
            const float tu1 = uc + (u1 - uc) * trace_scale;
            const float tv1 = vc + (v1 - vc) * trace_scale;
            if (!valid(tu0, tv0) || !valid(tu1, tv0) || !valid(tu1, tv1) || !valid(tu0, tv1) || !valid(uc, vc)) {
                continue;
            }

            grid_cells.push_back({u0, v0, u1, v1, uc, vc});
        }
    }

    return grid_cells;
}

} // namespace

void GpuBufferCache::release()
{
    cudaFree(d_surfs);
    cudaFree(d_pairs);
    cudaFree(d_src);
    cudaFree(d_grid);
    cudaFree(d_spec);
    cudaFree(d_out_r);
    cudaFree(d_out_g);
    cudaFree(d_out_b);

    d_surfs = nullptr;
    d_pairs = nullptr;
    d_src = nullptr;
    d_grid = nullptr;
    d_spec = nullptr;
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
                       const GhostRenderSetup& setup,
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

    const auto& active_pair_plans = setup.active_pair_plans;
    if (active_pair_plans.empty() || sources.empty()) {
        return true;
    }

    if (!cuda_ghost_renderer_available(out_error)) {
        return false;
    }

    const int num_surfaces = lens.num_surfaces();
    if (num_surfaces <= 0) {
        return true;
    }

    std::vector<GPUSpectralSampleDev> spectral_samples;
    {
        const int num_spectral_samples = std::max(3, config.spectral_samples);
        spectral_samples.resize(num_spectral_samples);

        auto cie_r = [](float lambda) {
            const float a = (lambda - 600.0f) / 70.0f;
            const float b = (lambda - 450.0f) / 30.0f;
            return std::max(0.0f, 0.63f * std::exp(-0.5f * a * a) +
                                       0.22f * std::exp(-0.5f * b * b));
        };
        auto cie_g = [](float lambda) {
            const float a = (lambda - 545.0f) / 55.0f;
            return std::max(0.0f, std::exp(-0.5f * a * a));
        };
        auto cie_b = [](float lambda) {
            const float a = (lambda - 445.0f) / 45.0f;
            return std::max(0.0f, std::exp(-0.5f * a * a));
        };

        if (num_spectral_samples == 3) {
            spectral_samples[0] = {config.wavelengths[0], 1.0f, 0.0f, 0.0f};
            spectral_samples[1] = {config.wavelengths[1], 0.0f, 1.0f, 0.0f};
            spectral_samples[2] = {config.wavelengths[2], 0.0f, 0.0f, 1.0f};
        } else {
            float sum_r = 0.0f;
            float sum_g = 0.0f;
            float sum_b = 0.0f;
            for (int i = 0; i < num_spectral_samples; ++i) {
                const float lambda = 400.0f + (300.0f * i) / (num_spectral_samples - 1);
                spectral_samples[i].lambda = lambda;
                spectral_samples[i].rw = cie_r(lambda);
                spectral_samples[i].gw = cie_g(lambda);
                spectral_samples[i].bw = cie_b(lambda);
                sum_r += spectral_samples[i].rw;
                sum_g += spectral_samples[i].gw;
                sum_b += spectral_samples[i].bw;
            }
            for (auto& sample : spectral_samples) {
                if (sum_r > 1.0e-9f) sample.rw /= sum_r;
                if (sum_g > 1.0e-9f) sample.gw /= sum_g;
                if (sum_b > 1.0e-9f) sample.bw /= sum_b;
            }
        }
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
    if (!ensure_buffer(cache.d_src, cache.src_bytes, gpu_sources.size() * sizeof(GPUSource), "d_src")) {
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
    GPU_CHECK(cudaMemcpy(cache.d_src,
                         gpu_sources.data(),
                         gpu_sources.size() * sizeof(GPUSource),
                         cudaMemcpyHostToDevice));

    GPU_CHECK(cudaMemset(cache.d_out_r, 0, num_pixels * sizeof(float)));
    GPU_CHECK(cudaMemset(cache.d_out_g, 0, num_pixels * sizeof(float)));
    GPU_CHECK(cudaMemset(cache.d_out_b, 0, num_pixels * sizeof(float)));

    if (!ensure_buffer(cache.d_spec,
                       cache.spec_bytes,
                       spectral_samples.size() * sizeof(GPUSpectralSampleDev),
                       "d_spec")) {
        return false;
    }
    GPU_CHECK(cudaMemcpy(cache.d_spec,
                         spectral_samples.data(),
                         spectral_samples.size() * sizeof(GPUSpectralSampleDev),
                         cudaMemcpyHostToDevice));

    bool launched_kernel = false;
    for (const GhostGridBucket& bucket : setup.grid_buckets) {
        const int bucket_grid = bucket.ray_grid;
        std::vector<GPUPair> gpu_splat_pairs;
        std::vector<GPUPair> gpu_cell_pairs;
        gpu_splat_pairs.reserve(active_pair_plans.size());
        gpu_cell_pairs.reserve(active_pair_plans.size());
        for (const GhostPairPlan& pair_plan : active_pair_plans) {
            if (pair_plan.ray_grid != bucket_grid) {
                continue;
            }
            GPUPair gpu_pair {
                pair_plan.pair.surf_a,
                pair_plan.pair.surf_b,
                pair_plan.area_boost,
                pair_plan.splat_radius_px,
                pair_plan.reference_footprint_area_px2,
            };
            if (pair_plan.use_cell_rasterization) {
                gpu_cell_pairs.push_back(gpu_pair);
            } else {
                gpu_splat_pairs.push_back(gpu_pair);
            }
        }

        if (!gpu_splat_pairs.empty()) {
            const int num_grid_samples = static_cast<int>(bucket.samples.size());
            if (num_grid_samples > 0) {
                if (!ensure_buffer(cache.d_pairs, cache.pairs_bytes, gpu_splat_pairs.size() * sizeof(GPUPair), "d_pairs")) {
                    return false;
                }
                if (!ensure_buffer(cache.d_grid, cache.grid_bytes, bucket.samples.size() * sizeof(GhostGridSample), "d_grid")) {
                    return false;
                }

                GPU_CHECK(cudaMemcpy(cache.d_pairs,
                                     gpu_splat_pairs.data(),
                                     gpu_splat_pairs.size() * sizeof(GPUPair),
                                     cudaMemcpyHostToDevice));
                GPU_CHECK(cudaMemcpy(cache.d_grid,
                                     bucket.samples.data(),
                                     bucket.samples.size() * sizeof(GhostGridSample),
                                     cudaMemcpyHostToDevice));

                const dim3 block(kBlockSize, 1, 1);
                const dim3 grid(static_cast<unsigned>(gpu_splat_pairs.size() * gpu_sources.size()),
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
                    2.0f / static_cast<float>(bucket_grid),
                    config.aperture_blades,
                    config.aperture_rotation_deg * 3.14159265358979323846f / 180.0f,
                    config.footprint_radius_bias,
                    config.footprint_clamp,
                    static_cast<GPUSpectralSampleDev*>(cache.d_spec),
                    static_cast<int>(spectral_samples.size()));

                cudaError_t error = cudaGetLastError();
                if (error != cudaSuccess) {
                    report_cuda_error(error, "ghost_kernel", out_error);
                    return false;
                }
                launched_kernel = true;
            }
        }

        if (!gpu_cell_pairs.empty()) {
            const int num_grid_cells = static_cast<int>(bucket.cells.size());
            if (num_grid_cells > 0) {
                if (!ensure_buffer(cache.d_pairs, cache.pairs_bytes, gpu_cell_pairs.size() * sizeof(GPUPair), "d_pairs")) {
                    return false;
                }
                if (!ensure_buffer(cache.d_grid, cache.grid_bytes, bucket.cells.size() * sizeof(GhostGridCell), "d_grid")) {
                    return false;
                }

                GPU_CHECK(cudaMemcpy(cache.d_pairs,
                                     gpu_cell_pairs.data(),
                                     gpu_cell_pairs.size() * sizeof(GPUPair),
                                     cudaMemcpyHostToDevice));
                GPU_CHECK(cudaMemcpy(cache.d_grid,
                                     bucket.cells.data(),
                                     bucket.cells.size() * sizeof(GhostGridCell),
                                     cudaMemcpyHostToDevice));

                const dim3 block(kBlockSize, 1, 1);
                const dim3 grid(static_cast<unsigned>(gpu_cell_pairs.size() * gpu_sources.size()),
                                static_cast<unsigned>((num_grid_cells + kBlockSize - 1) / kBlockSize),
                                1);

                ghost_cell_kernel<<<grid, block>>>(
                    static_cast<Surface*>(cache.d_surfs),
                    num_surfaces,
                    lens.sensor_z,
                    static_cast<GPUPair*>(cache.d_pairs),
                    static_cast<int>(gpu_sources.size()),
                    static_cast<GPUSource*>(cache.d_src),
                    static_cast<GPUCell*>(cache.d_grid),
                    num_grid_cells,
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
                    1.0f / static_cast<float>(num_grid_cells),
                    config.aperture_blades,
                    config.aperture_rotation_deg * 3.14159265358979323846f / 180.0f,
                    config.cell_edge_inset,
                    config.cell_coverage_bias,
                    static_cast<GPUSpectralSampleDev*>(cache.d_spec),
                    static_cast<int>(spectral_samples.size()));

                cudaError_t error = cudaGetLastError();
                if (error != cudaSuccess) {
                    report_cuda_error(error, "ghost_cell_kernel", out_error);
                    return false;
                }
                launched_kernel = true;
            }
        }
    }

    if (launched_kernel) {
        GPU_CHECK(cudaDeviceSynchronize());
    }

    GPU_CHECK(cudaMemcpy(out_r, cache.d_out_r, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));
    GPU_CHECK(cudaMemcpy(out_g, cache.d_out_g, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));
    GPU_CHECK(cudaMemcpy(out_b, cache.d_out_b, num_pixels * sizeof(float), cudaMemcpyDeviceToHost));

#undef GPU_CHECK

    return true;
}
