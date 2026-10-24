#pragma once

#include "frame_bridge.h"
#include "parameter_state.h"

#include <cstdint>
#include <vector>

// Photoshop hands filters interleaved samples ("rows of columns of planes").
// 8-bit is 0..255, 16-bit is 0..32768, 32-bit is linear float with over-range
// values allowed.
struct PsPixelBuffer
{
    void* data = nullptr;
    std::int32_t row_bytes = 0;
    int depth = 8;
    int planes = 3;
    int width = 0;
    int height = 0;
};

struct PsChannelMap
{
    int alpha_plane = -1;
};

// Copies a PS band into dst at (dst_x, dst_y). With premultiply the colour is
// multiplied by the transparency plane, so the renderer never sees colour
// hiding under fully transparent pixels.
bool ps_read_pixels(const PsPixelBuffer& src,
                    const PsChannelMap& map,
                    bool premultiply,
                    FloatImageBuffer& dst,
                    int dst_x,
                    int dst_y);

// Writes the region of src starting at (src_x, src_y) into a PS band. With
// unpremultiply the colour is divided by src.alpha, which must hold the alpha
// that ends up stored in the document.
bool ps_write_pixels(const FloatImageBuffer& src,
                     int src_x,
                     int src_y,
                     const PsChannelMap& map,
                     bool unpremultiply,
                     PsPixelBuffer& dst);

// Builds a 1/factor preview proxy. Blocks are box-averaged, except blocks
// whose peak luminance exceeds peak_threshold, which keep their peak pixel so
// point highlights still register as flare sources at reduced size.
bool ps_build_preview_proxy(const FloatImageBuffer& full,
                            int factor,
                            float peak_threshold,
                            FloatImageBuffer& out_proxy);

// Rescales pixel-space settings so a 1/factor proxy render matches the full
// resolution render: the source stride shrinks with the image and flare gain
// is compensated the same way Preview Mode does (gain * (new/old stride)^2).
void ps_scale_state_for_proxy(AeParameterState& state, int factor);

// Area-resamples to dst_width x dst_height BGRA8 for GDI. Colour is treated as
// premultiplied and composited over a checkerboard. linear_to_srgb encodes 32-bit
// linear documents for display; space is the colour space they are in and is
// converted to Rec.709 primaries first.
void ps_make_display_bitmap(const FloatImageBuffer& image,
                            bool linear_to_srgb,
                            int dst_width,
                            int dst_height,
                            std::vector<std::uint32_t>& out_bgra,
                            WorkingColorSpace space = WorkingColorSpace::LinearRec709);
