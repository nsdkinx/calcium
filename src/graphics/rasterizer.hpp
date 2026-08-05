#pragma once

// The display-list rasterizer (internal — the machinery behind
// docs/02-architecture.md §6.1).
//
// Walks a sealed DisplayList and emits into a gpu::DrawPass. Owns every piece
// of presentation state the IR implies: the CTM stack, the device-space clip
// stack, and culling. The gpu interface stays primitive (rects, polygons,
// clips) so a second backend (D3D12, and later ca::raster) implements the
// same handful of virtuals and reuses this geometry logic verbatim — this is
// the shape the docs' `graphics::backend::Rasterizer` boundary takes.
//
// Tessellation note: rounded rects are tessellated here into convex polygons
// (circular corners as quarter-arc fans, `continuous` corners by sampling the
// G2 cubic spec in rounded_rectangle.hpp), so no backend ever reimplements
// corner geometry. Coverage antialiasing is deliberately absent in M2 — the
// conformance suite and its rasterization spec land with the rasterizer
// milestone.

#include "calcium/graphics/display_list.hpp"
#include "calcium/gpu/draw_pass.hpp"

namespace ca::graphics::rasterizer {

/// Draws `display_list` into `pass` rooted at `root_transform` (the layer's
/// presentation transform) with `alpha` (the layer's presentation opacity)
/// multiplying every paint. Compositor thread.
void draw(const DisplayList& display_list, gpu::DrawPass& pass,
          const geometry::AffineTransform& root_transform, float alpha);

/// Fills a rounded rectangle directly — the layer-background path. The
/// compositor calls this for compositor-resolvable layer attributes
/// (docs/02 §2.2) without going through a display list. `color` is
/// straight-alpha sRGB; `alpha` multiplies it (the layer's presentation
/// opacity).
void fill_rounded_rectangle(gpu::DrawPass& pass,
                            const geometry::RoundedRectangle& rounded_rectangle,
                            const geometry::AffineTransform& transform,
                            const float color[4], float alpha);

} // namespace ca::graphics::rasterizer
