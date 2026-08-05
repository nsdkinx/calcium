// The presentation-timing model (internal — the public Display delegates to it
// through an opaque pointer; the concrete implementations live in backends).

#pragma once

#include "calcium/core/time.hpp"

namespace ca::platform {

/// The backend's presentation-timing model. Display borrows it; the backend
/// owns it for the backend's lifetime.
struct DisplayTiming {
    virtual ~DisplayTiming() = default;

    /// When a frame composited now will scan out.
    [[nodiscard]] virtual core::Timestamp predicted_presentation_time() const = 0;

    /// The display's current refresh cadence in Hz.
    [[nodiscard]] virtual float refresh_rate_hz() const = 0;

    /// The display's points-to-pixels scale.
    [[nodiscard]] virtual float scale_factor() const = 0;

    /// False when prediction is extrapolation from measured cadence.
    [[nodiscard]] virtual bool provides_hardware_prediction() const = 0;
};

} // namespace ca::platform
