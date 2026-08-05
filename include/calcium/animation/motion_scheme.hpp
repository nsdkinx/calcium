#pragma once

// Motion schemes.
//
// The named motions resolve to springs through the active scheme; the scheme
// is part of the theme (docs/04-public-api.md §5, Theme.motion) and the place
// platform defaults and reduced-motion preferences land. The default table is
// tuned to iOS characteristics (docs/05-animation-and-twell.md §5):

//   motion      stiffness  damping   ζ     response   use
//   standard        250      31.6   1.00   ~0.40 s   everything by default
//   emphasized      200      24.0   0.85   ~0.50 s   hero transitions, sheets
//   snappy          480      44.0   1.00   ~0.28 s   toggles, buttons, selection
//   gentle          120      21.9   1.00   ~0.60 s   large surfaces, backgrounds
//   playful         300      18.0   0.52   ~0.55 s   notifications, pull-to-refresh

#include "calcium/animation/motion.hpp"
#include "calcium/animation/spring_configuration.hpp"

namespace ca::animation {

struct MotionScheme {
    SpringConfiguration standard;
    SpringConfiguration emphasized;
    SpringConfiguration snappy;
    SpringConfiguration gentle;
    SpringConfiguration playful;

    double scroll_deceleration_rate = 0.998;
    double fast_scroll_deceleration_rate = 0.99;
    double rubber_band_tension = 0.55;
    double flick_velocity_threshold = 250.0;  // pt/s

    /// Resolves a named motion to its spring, or returns the motion unchanged
    /// for explicit springs, decays, and immediates.
    [[nodiscard]] Motion resolve(Motion motion) const;

    /// The iOS-like table above.
    [[nodiscard]] static MotionScheme apple_like();

    /// Material Design's motion language: slightly faster springs, more
    /// overshoot on emphasis.
    [[nodiscard]] static MotionScheme material_like();

    [[nodiscard]] static MotionScheme platform_default();

    /// Honors the OS reduced-motion preference: short critically damped
    /// motions (ζ = 1, response ≈ 0.15 s), no bounciness. Does NOT disable
    /// animation entirely — instant state changes are disorienting and fail
    /// accessibility guidance too (docs/05-animation-and-twell.md §5.1).
    [[nodiscard]] static MotionScheme reduced_motion();
};

} // namespace ca::animation
