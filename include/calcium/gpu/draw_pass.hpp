#pragma once

#include "calcium/core/export.hpp"

// Draw passes: the Level-1 surface the display-list rasterizer emits into
// (docs/02-architecture.md §6, docs/04-public-api.md §4).
//
// M1 shipped the clear pass: acquire, clear, present. M2 adds the draw pass —
// the same acquire/present lifecycle plus primitive fills in device space.
// The interface deliberately stays small: rects and convex polygons in device
// coordinates, straight-alpha sRGB colors, and a clip stack. Everything that
// is display-list semantics — transforms, paint composition, tessellation —
// lives in the rasterizer (src/graphics/rasterizer.cpp), not here. Backends
// implement primitives, never semantics; that is what keeps backend
// replacement honest (P6): a D3D12 or ca::raster backend implements the same
// handful of virtuals.
//
// Coordinates are device pixels. Straight-alpha sRGB in, premultiplied
// blending out (SDL3's SDL_BLENDMODE_BLEND does the multiply at blend time).

#include <cstdint>
#include <span>

#include "calcium/geometry/point.hpp"
#include "calcium/geometry/rect.hpp"
#include "calcium/gpu/render_pass.hpp"

namespace ca::gpu {

class DrawPass : public RenderPass {
public:
    /// Fills the whole back buffer with `color` (straight-alpha sRGB).
    virtual void clear(const float color[4]) = 0;

    /// Intersects the render clip with `rect` (device space). Clips nest:
    /// each `push_clip` is undone by the matching `pop_clip`, which restores
    /// the previous clip.
    virtual void push_clip(geometry::Rect rect) = 0;
    virtual void pop_clip() = 0;

    /// Fills an axis-aligned rectangle (device space).
    virtual void fill_rect(geometry::Rect rect, const float color[4]) = 0;

    /// Fills a simple polygon (device space, ordered, at least 3 points)
    /// that is star-shaped with respect to its own centroid — the fan
    /// originates at the centroid, so concave outlines (the squircle
    /// corner curves meet the edges perpendicularly, making them concave
    /// by design) fill correctly.
    virtual void fill_polygon(std::span<const geometry::Point> polygon,
                              const float color[4]) = 0;
};

} // namespace ca::gpu
