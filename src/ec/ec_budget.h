#pragma once

#include "ghost.h"
#include "lens.h"
#include "render_frame.h"

#include <vector>

// EffectCraft runs plug-ins in an interpreter with a fixed instruction ("fuel") budget per
// frame: 50M plus 20k per pixel. A frame that runs out is left unchanged, so the effect would
// silently do nothing. The governor estimates the cost of a frame before tracing it and, if
// needed, lowers quality until the estimate fits, in this order:
//   1. Sharp Adaptive / Sharp + Blur -> Legacy Blur, unless every source fits sharp at
//      kEcPreferredMinRayGrid
//   2. Cluster Radius, doubling up to width / 96: merges the source blocks of each highlight
//   3. Ray Grid, down to kEcPreferredMinRayGrid
//   4. Cluster Radius, doubling up to width / 8
//   5. Max Sources (the brightest are kept)
//   6. Ray Grid, down to kEcMinBudgetRayGrid
// When it ends on Legacy Blur it also turns on Halton pupil jitter and raises Ghost Blur, to
// fill the gaps between rays.
// The estimate is deterministic, so every render of a frame makes the same choice.

inline constexpr int kEcPreferredMinRayGrid = 16;
inline constexpr int kEcMinBudgetRayGrid = 8;

struct EcBudgetPlan
{
    double budget_fuel = 0.0;      // what EffectCraft allows for this frame
    double estimated_fuel = 0.0;   // estimate for the settings actually rendered
    double requested_fuel = 0.0;   // estimate for the settings as set
    int active_pairs = 0;
    int requested_sources = 0;
    int sources = 0;
    int requested_ray_grid = 0;
    int ray_grid = 0;
    int requested_cluster_radius = 0;
    int cluster_radius = 0;
    GhostCleanupMode requested_cleanup = GhostCleanupMode::LegacyBlur;
    GhostCleanupMode cleanup = GhostCleanupMode::LegacyBlur;
    float ghost_blur = 0.0f; // raised Ghost Blur, 0 = unchanged
    bool reduced = false;
};

double ec_frame_fuel_budget(int width, int height);

// Estimates the frame and, when fit is true, lowers settings.ghost_cleanup_mode, ray_grid,
// cluster_radius_px and max_sources so the estimate stays within the budget. detected_sources
// is FrameRenderOutputs::detected_sources for these settings (before clustering and Max
// Sources, manual source last).
EcBudgetPlan ec_fit_render_budget(const LensSystem& lens,
                                  FrameRenderSettings& settings,
                                  const FrameRenderPlan& plan,
                                  int width,
                                  int height,
                                  const std::vector<BrightPixel>& detected_sources,
                                  bool fit);
