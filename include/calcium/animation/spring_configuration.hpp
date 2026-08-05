#pragma once

// Spring configuration.
//
// Springs are the default motion of the framework: every animatable value is
// an analytically solved spring, sampled at the presentation timestamp by the
// compositor (docs/00-overview.md §1). The configuration is the classic mass /
// stiffness / damping triple; the conversion helpers let designers think in
// response time and damping ratio instead (docs/05-animation-and-twell.md §5).

#include <cmath>

namespace ca::animation {

/// A damped harmonic oscillator, matching Twell's `twell_spring_config`
/// exactly (the animation path passes it through without conversion).
struct SpringConfiguration {
    double mass = 1.0;
    double stiffness = 250.0;
    double damping = 18.0;   ///< near-critical for mass=1, k=250
    double initial_velocity = 0.0;

    /// Builds a spring from what designers actually specify: the response
    /// time in seconds (one oscillation period at ζ=1) and the damping ratio
    /// (1.0 = critically damped, <1 = underdamped bounce, >1 = overdamped).
    /// Apple's curve: ω₀ = 2π/response; k = m·ω₀²; c = 2ζ·√(m·k).
    /// Not constexpr: std::sqrt is not constexpr in MSVC's STL (same reason
    /// AffineTransform::make_rotation is not).
    [[nodiscard]] static SpringConfiguration
    with_response_and_damping_ratio(double response_seconds,
                                    double damping_ratio) noexcept {
        const double omega = 2.0 * 3.14159265358979323846 / response_seconds;
        const double mass = 1.0;
        const double stiffness = mass * omega * omega;
        const double damping =
            2.0 * damping_ratio * std::sqrt(mass * stiffness);
        return {mass, stiffness, damping, 0.0};
    }

    [[nodiscard]] static SpringConfiguration critically_damped(
        double response_seconds) noexcept {
        return with_response_and_damping_ratio(response_seconds, 1.0);
    }

    /// ζ = c / (2·√(m·k)): 1.0 critical, <1 underdamped, >1 overdamped.
    [[nodiscard]] double damping_ratio() const noexcept {
        const double denominator = 2.0 * std::sqrt(mass * stiffness);
        return denominator != 0.0 ? damping / denominator : 0.0;
    }

    /// The natural frequency ω₀ = √(k/m).
    [[nodiscard]] double natural_frequency() const noexcept {
        return std::sqrt(stiffness / mass);
    }

    /// An estimate of the time until the spring is visually settled
    /// (5 time constants, the point where the envelope is ~0.7% of the
    /// initial displacement).
    [[nodiscard]] double settling_duration_estimate() const noexcept {
        const double tau = 1.0 / (damping_ratio() * natural_frequency());
        return 5.0 * tau;
    }

    [[nodiscard]] constexpr bool operator==(const SpringConfiguration&) const noexcept =
        default;
};

} // namespace ca::animation
