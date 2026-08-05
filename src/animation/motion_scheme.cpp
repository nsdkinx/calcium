#include "calcium/animation/motion_scheme.hpp"

namespace ca::animation {

Motion MotionScheme::resolve(Motion motion) const {
    if (motion.kind() != Motion::Kind::spring) {
        return motion;
    }

    SpringConfiguration configuration = motion.spring_configuration();
    switch (motion.named()) {
    case Motion::Named::none:
        break;  // an explicit spring is already the resolved form
    case Motion::Named::standard:   configuration = standard;   break;
    case Motion::Named::emphasized: configuration = emphasized; break;
    case Motion::Named::snappy:     configuration = snappy;     break;
    case Motion::Named::gentle:     configuration = gentle;     break;
    case Motion::Named::playful:    configuration = playful;    break;
    }

    // Speed multiplier: compress the response time (docs/05 §5) — stiffness
    // scales with omega^2, damping with omega.
    const double multiplier = motion.speed_multiplier();
    if (multiplier != 1.0) {
        configuration.stiffness *= multiplier * multiplier;
        configuration.damping *= multiplier;
    }

    Motion resolved = Motion::spring(configuration);
    if (motion.delay() != core::Duration::zero()) {
        resolved = resolved.with_delay(motion.delay());
    }
    return resolved;
}

MotionScheme MotionScheme::apple_like() {
    // The table in the header comment, from
    // docs/05-animation-and-twell.md §5.
    return MotionScheme{
        .standard = SpringConfiguration::with_response_and_damping_ratio(0.40, 1.0),
        .emphasized = SpringConfiguration::with_response_and_damping_ratio(0.50, 0.85),
        .snappy = SpringConfiguration::with_response_and_damping_ratio(0.28, 1.0),
        .gentle = SpringConfiguration::with_response_and_damping_ratio(0.60, 1.0),
        .playful = SpringConfiguration::with_response_and_damping_ratio(0.55, 0.52),
        .scroll_deceleration_rate = 0.998,
        .fast_scroll_deceleration_rate = 0.99,
        .rubber_band_tension = 0.55,
        .flick_velocity_threshold = 250.0,
    };
}

MotionScheme MotionScheme::material_like() {
    // Faster springs than iOS, more overshoot where it matters.
    return MotionScheme{
        .standard = SpringConfiguration::with_response_and_damping_ratio(0.35, 1.0),
        .emphasized = SpringConfiguration::with_response_and_damping_ratio(0.45, 0.80),
        .snappy = SpringConfiguration::with_response_and_damping_ratio(0.25, 1.0),
        .gentle = SpringConfiguration::with_response_and_damping_ratio(0.50, 1.0),
        .playful = SpringConfiguration::with_response_and_damping_ratio(0.50, 0.60),
        .scroll_deceleration_rate = 0.994,
        .fast_scroll_deceleration_rate = 0.985,
        .rubber_band_tension = 0.60,
        .flick_velocity_threshold = 250.0,
    };
}

MotionScheme MotionScheme::platform_default() {
    return apple_like();
}

MotionScheme MotionScheme::reduced_motion() {
    // Short critically damped motions (ζ = 1, response ≈ 0.15 s), no
    // bounciness (docs/05-animation-and-twell.md §5.1).
    return MotionScheme{
        .standard = SpringConfiguration::critically_damped(0.15),
        .emphasized = SpringConfiguration::critically_damped(0.15),
        .snappy = SpringConfiguration::critically_damped(0.10),
        .gentle = SpringConfiguration::critically_damped(0.20),
        .playful = SpringConfiguration::critically_damped(0.15),
        .scroll_deceleration_rate = 0.998,
        .fast_scroll_deceleration_rate = 0.99,
        .rubber_band_tension = 0.55,
        .flick_velocity_threshold = 250.0,
    };
}

} // namespace ca::animation
