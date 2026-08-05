#pragma once

#include "calcium/core/export.hpp"

// Swapchains.
//
// A swapchain is a sequence of back buffers the compositor cycles through and
// presents to a window. It is owned by the compositor and used exclusively on
// the compositor thread (docs/02-architecture.md §2.3).

#include "calcium/geometry/point.hpp"

namespace ca::gpu {

class Swapchain {
public:
    virtual ~Swapchain() = default;

    /// The current back-buffer size in points (changes with window resizes).
    [[nodiscard]] virtual geometry::Size size() const = 0;

    /// Resizes the back buffers. Called by the compositor when the window
    /// reports a resize; a no-op when the size is unchanged.
    virtual void resize(geometry::Size size) = 0;

    /// The number of frames in flight (back buffers). The compositor uses it
    /// for presentation-time prediction: a frame presented now scans out
    /// `frames_in_flight + 1` vsyncs from now.
    [[nodiscard]] virtual std::uint32_t frames_in_flight() const noexcept = 0;
};

} // namespace ca::gpu
