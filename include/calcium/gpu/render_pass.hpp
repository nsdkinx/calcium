#pragma once

#include "calcium/core/export.hpp"

// Render passes.
//
// M1's pass is the clear pass: acquire the back buffer, clear it to a color,
// present. The full `RenderCommandEncoder` (draws, pipelines, textures) lands
// with the display-list rasterizer in M2; `end_and_present` is the boundary
// that survives, so the compositor's frame loop does not change shape.

#include "calcium/core/time.hpp"

namespace ca::gpu {

class RenderPass {
public:
    virtual ~RenderPass() = default;

    /// Submits the frame and returns after the GPU work is queued. When the
    /// swapchain is vsync-paced, this also blocks until the previous frame has
    /// been consumed — which is what paces the compositor to the display.
    virtual void end_and_present() = 0;

    /// The monotonic timestamp at which the frame's back buffer was acquired —
    /// the moment the pacing wait released. The compositor measures the
    /// frame's *work* as `submitted_at − acquired_at` and the display cadence
    /// as the delta between consecutive `acquired_at` values.
    [[nodiscard]] virtual core::Timestamp acquired_at() const noexcept = 0;

    /// The monotonic timestamp at which the frame was submitted to the GPU.
    /// The compositor records it in the frame-timing log.
    [[nodiscard]] virtual core::Timestamp submitted_at() const noexcept = 0;
};

} // namespace ca::gpu
