#pragma once

// Displays and presentation timing.
//
// `predicted_presentation_time()` is the single most important method in the
// platform layer (docs/02-architecture.md §3.1): it answers "when will the
// frame I build right now actually be scanned out?" and the compositor ticks
// animation at exactly that moment. Every platform that can provide the real
// thing does (CVDisplayLink target time, Choreographer frame deadline,
// DXGI frame statistics); a backend that cannot must extrapolate from its
// measured vsync cadence and say so — the degradation is visible rather than
// silent, which is the point of the contract.

#include <cstdint>

#include "calcium/core/export.hpp"
#include "calcium/core/time.hpp"

namespace ca::platform {

// The backend's presentation-timing model; Display delegates to it so the
// cadence extrapolation (or hardware feedback) lives with the backend that
// measures it. Never named in a public header beyond this opaque pointer.
struct DisplayTiming;

/// A display (or the display a window sits on), with its timing properties.
class Display {
public:
    /// The display's refresh cadence in frames per second.
    [[nodiscard]] float refresh_rate_hz() const noexcept { return refresh_rate_hz_; }

    /// The nominal vsync interval. When the display reports a dynamic refresh
    /// rate (ProMotion-style), this is the *current* interval.
    [[nodiscard]] core::Duration vsync_interval() const noexcept {
        return core::Duration::from_hertz(refresh_rate_hz_);
    }

    /// Points-to-pixels scale factor (1.0, 2.0, 3.0 …).
    [[nodiscard]] float scale_factor() const noexcept { return scale_factor_; }

    /// The monotonic timestamp at which a frame composited *now* will be
    /// scanned out. This is the value the compositor hands to Twell.
    /// Falls back to `Timestamp::now()` if no backend provides timing.
    /// (Out-of-line: the timing model lives behind the opaque pointer.)
    core::Timestamp predicted_presentation_time() const noexcept;

    /// False when the backend extrapolates `predicted_presentation_time` from
    /// measured vsync cadence rather than hardware presentation feedback.
    /// The compositor may log this once at startup, so the degradation is
    /// visible in diagnostics rather than silently accepted.
    [[nodiscard]] bool provides_hardware_presentation_prediction() const noexcept {
        return provides_hardware_prediction_;
    }

    /// The platform backend's identity for this display (a display index or a
    /// native display handle). Opaque; useful for comparing displays.
    [[nodiscard]] std::uint64_t native_id() const noexcept { return native_id_; }

private:
    // Display instances are obtained from Application/Window, never constructed
    // directly.
    friend class Application;
    friend class Window;
    Display(std::uint64_t native_id, float refresh_rate_hz, float scale_factor,
            bool provides_hardware_prediction,
            const DisplayTiming* timing) noexcept
        : native_id_(native_id),
          refresh_rate_hz_(refresh_rate_hz),
          scale_factor_(scale_factor),
          provides_hardware_prediction_(provides_hardware_prediction),
          timing_(timing) {}

    std::uint64_t native_id_ = 0;
    float refresh_rate_hz_ = 60.0f;
    float scale_factor_ = 1.0f;
    bool provides_hardware_prediction_ = false;
    const DisplayTiming* timing_ = nullptr;
};

} // namespace ca::platform
