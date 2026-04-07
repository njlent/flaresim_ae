# Ghost Reconstruction Breakdown

## Goal

Reduce ghost artifacts and preserve sharper flare structure without brute-forcing `ray_grid` across every pair.

This plan is intentionally incremental:

1. land pair-planning + adaptive sampling first
2. improve deposition quality with footprint-aware splats
3. upgrade the primitive from point splats to pupil-cell rasterization
4. add a focused refinement pass only where the coarse pass is weak
5. replace blunt pair-wide normalization with local Jacobian-aware density compensation

The target is the current ghost pipeline in:

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`
- `src/runtime/render_frame.cpp`

## Why This Instead Of DLSS / Generic Denoising

The current artifacts are not primarily Monte Carlo image noise.

The dominant failure modes are:

- sparse sampling across the entrance pupil
- holes between splats on large or warped ghost shapes
- blur-heavy cleanup that trades continuity for sharpness
- pair-wide normalization that cannot react to local magnification changes

That makes this a transport/reconstruction problem, not an image denoiser problem.

## Current Pipeline Weak Points

Today the ghost pass does this:

1. enumerate ghost pairs
2. prefilter dim pairs
3. estimate pair spread / area boost
4. trace one global pupil grid per pair and source
5. deposit point contributions with bilinear or tent splats
6. optionally blur the whole ghost buffer

Weaknesses:

- one global `ray_grid` over-samples easy pairs and under-samples hard ones
- splat radius is inferred after the fact instead of from the traced mapping
- point splats do not preserve continuous ghost footprints well when the pupil mapping is stretched or curved
- pair-wide area normalization is too blunt for highly non-uniform ghosts

## Slice 1: Adaptive Pair Sampling

### Goal

Spend rays where the pair actually needs them.

### Plan

Build a `PairSamplingPlan` per active pair after the existing pair prefilter.

Per pair, estimate:

- `extent_px`: ghost footprint extent on sensor
- `distortion_score`: how nonlinear the pupil-to-sensor mapping is from a small probe set
- `grid`: chosen sampling grid bucket for the pair
- `splat_radius_px`: initial fallback radius if later passes still need point/tent splats

First heuristic:

- start from the existing configured `ray_grid`
- allow buckets at `base / 2`, `base`, `base * 2`
- promote large or warped pairs
- demote compact, stable pairs

### Code Targets

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`

### Expected Result

- more quality for large/defocused pairs at the same frame time
- less wasted work on compact/focused pairs

## Slice 2: Footprint-Aware Splats

### Goal

Replace isotropic cleanup guesses with deposition based on the local traced mapping.

### Plan

For each traced sample, estimate a local Jacobian by tracing nearby pupil offsets:

- sample `(u, v)`
- sample `(u + du, v)`
- sample `(u, v + dv)`

Use the resulting 2x2 mapping to derive:

- local area
- principal axes
- orientation

Then replace the current point/tent deposition with an ellipse footprint when valid.

Fallback rules:

- invalid/degenerate Jacobian -> existing tent splat
- extreme aspect ratio or giant footprint -> clamp and fall back conservatively

### Code Targets

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`

### Expected Result

- fewer holes inside ghosts
- less need for post blur
- sharper structure at the same sample count

## Slice 3: Pupil-Cell Rasterization

### Goal

Move from isolated point reconstruction to a primitive that better matches caustic concentration.

### Plan

Treat each pupil cell as a patch instead of a collection of unrelated points.

For each cell:

1. trace the 4 cell corners
2. map them to the sensor
3. reject folded/invalid cells
4. rasterize the resulting quad (or two triangles) onto the sensor
5. deposit cell energy based on average throughput and pupil-cell area

This is the closest analogue in this codebase to a photon-tracing-style caustic reconstruction:

- concentrate transport into continuous projected regions
- preserve sharp boundaries better than post-filtered point splats

### Code Targets

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`

### Expected Result

- smoother continuous ghosts
- much better behavior on large defocused pairs
- fewer streaks and isolated bright points

## Slice 4: Focused Refinement Pass

### Goal

Add extra work only where the coarse pass is visibly weak.

### Plan

After the coarse pass, derive an artifact/refinement mask per pair or tile using signals like:

- low sample density in a high-energy region
- high footprint variance
- large warp or fold risk
- abrupt local energy discontinuity

Then subdivide only flagged pupil cells and re-run those regions at higher local resolution.

This must remain bounded:

- max refinement depth
- max refined cells per pair
- deterministic ordering

### Code Targets

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`
- `src/runtime/render_frame.cpp`

### Expected Result

- artifact cleanup concentrated on hotspots
- better quality scaling than raising `ray_grid` globally

## Slice 5: Jacobian-Aware Density Compensation

### Goal

Replace pair-wide area boosting with local density-aware energy placement.

### Plan

Use the Jacobian determinant of the pupil-to-sensor map as the primary local density signal.

Interpretation:

- small projected area -> concentrated energy
- large projected area -> spread energy over more pixels

Keep the current pair-level area boost as a fallback and safety clamp, but stop relying on it as the main reconstruction tool.

### Code Targets

- `src/core/ghost.cpp`
- `src/cuda/ghost_cuda.cu`

### Expected Result

- more stable brightness on defocused ghosts
- fewer cases where cleanup blur is hiding an energy-placement problem

## Rollout Order

Recommended order:

1. adaptive pair sampling
2. footprint-aware splats
3. local density compensation
4. pupil-cell rasterization
5. focused refinement

Reason:

- `1` and `2` are the lowest-risk improvements to the existing pipeline
- `5` depends on trustworthy local mapping data from `2`
- `4` changes the deposition primitive and should come after the heuristics are proven
- `5` in the list above is algorithmically earlier than `4`, but the actual engineering order is better as `3` before `4`
- refinement should land last so it is improving the right coarse primitive

## Test Strategy

Add smoke coverage for:

- adaptive bucket selection across different pairs
- flare energy conservation within tolerance versus legacy path
- peak sharpness improvement for `SharpAdaptive`
- reduced holes/zero-hit pixels inside a ghost bounding box
- cache correctness when only post-blur changes
- CUDA/CPU parity within a practical tolerance band

Golden scenes:

- on-axis compact bright point
- large defocused pair
- off-axis bright point near frame edge

## Non-Goals

Not part of this plan:

- DLSS / Ray Reconstruction integration
- generic image denoiser integration
- user-facing controls for every internal heuristic in the first pass

The first landing should improve quality under the existing `Ghost Cleanup` modes before expanding the UI surface.
