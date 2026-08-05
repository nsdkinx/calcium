#pragma once

// Motion.
//
// P9: application code names the intent, not the numbers. `Motion::standard()`
// is the everyday critically damped response; the named motions resolve
// through the active MotionScheme at animation start, so retuning the scheme
// (platform defaults, reduced-motion mode) retunes every animation that named
// a motion — no call site changes.

#include "calcium/animation/spring_configuration.hpp"
#include "calcium/core/time.hpp"

namespace ca::animation {

/// Configuration for a viscous-decay animation (momentum handoff).
/// `deceleration_rate` is the multiplicative factor per 1/60 s of simulated
/// time — UIScrollView's normal rate is 0.998, its fast rate 0.99.
struct DecayConfiguration {
    double deceleration_rate = 0.998;
    double threshold = 0.1;
};

/// A named animation intent. Immutable; produced by the static factories.
class Motion {
public:
    enum class Kind : std::uint8_t { spring, decay, immediate };

    /// The named motion, when created through the static factories. `none`
    /// for explicit springs, decays, and immediates; the active MotionScheme
    /// resolves named motions to springs at animation start.
    enum class Named : std::uint8_t {
        none,
        standard,
        emphasized,
        snappy,
        gentle,
        playful,
    };

    [[nodiscard]] static Motion standard();    ///< everyday, critically damped
    [[nodiscard]] static Motion emphasized();  ///< slight overshoot, hero moments
    [[nodiscard]] static Motion snappy();      ///< fast, no overshoot
    [[nodiscard]] static Motion gentle();      ///< slow, soft
    [[nodiscard]] static Motion playful();     ///< clearly underdamped
    [[nodiscard]] static Motion immediate();   ///< no animation

    [[nodiscard]] static Motion spring(SpringConfiguration configuration);
    [[nodiscard]] static Motion decay(DecayConfiguration configuration);

    /// Delay before the animation starts, applied on the compositor clock
    /// (a UI-thread stall does not extend it).
    [[nodiscard]] Motion with_delay(core::Duration delay) const;

    /// Speeds the motion up (`2.0` = twice as fast) by compressing the
    /// spring's response time. Honors reduced-motion semantics.
    [[nodiscard]] Motion with_speed_multiplier(double multiplier) const;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] Named named() const noexcept { return named_; }
    [[nodiscard]] const SpringConfiguration& spring_configuration() const noexcept {
        return spring_;
    }
    [[nodiscard]] const DecayConfiguration& decay_configuration() const noexcept {
        return decay_;
    }
    [[nodiscard]] core::Duration delay() const noexcept { return delay_; }
    [[nodiscard]] double speed_multiplier() const noexcept {
        return speed_multiplier_;
    }

    [[nodiscard]] bool operator==(const Motion&) const noexcept = default;

    /// Set by the scheme's resolve and the named factories (implementation).
    void set_named(Named named) noexcept { named_ = named; }
    void set_spring(SpringConfiguration configuration) noexcept {
        spring_ = configuration;
    }

private:
    explicit Motion(Kind kind) noexcept : kind_(kind) {}

    Kind kind_ = Kind::spring;
    Named named_ = Named::none;
    SpringConfiguration spring_;
    DecayConfiguration decay_;
    core::Duration delay_;
    double speed_multiplier_ = 1.0;
};

} // namespace ca::animation
