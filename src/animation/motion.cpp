#include "calcium/animation/motion.hpp"

namespace ca::animation {

namespace {
// The named springs carry the table's defaults so Motion reads sensibly even
// before a scheme resolves them; the scheme overrides at animation start.
Motion with_name(Motion::Named name, double response, double ratio) {
    Motion motion = Motion::spring(
        SpringConfiguration::with_response_and_damping_ratio(response, ratio));
    motion.set_named(name);
    return motion;
}
} // namespace

Motion Motion::standard() { return with_name(Named::standard, 0.40, 1.0); }
Motion Motion::emphasized() { return with_name(Named::emphasized, 0.50, 0.85); }
Motion Motion::snappy() { return with_name(Named::snappy, 0.28, 1.0); }
Motion Motion::gentle() { return with_name(Named::gentle, 0.60, 1.0); }
Motion Motion::playful() { return with_name(Named::playful, 0.55, 0.52); }

Motion Motion::immediate() {
    return Motion{Kind::immediate};
}

Motion Motion::spring(SpringConfiguration configuration) {
    Motion motion{Kind::spring};
    motion.spring_ = configuration;
    return motion;
}

Motion Motion::decay(DecayConfiguration configuration) {
    Motion motion{Kind::decay};
    motion.decay_ = configuration;
    return motion;
}

Motion Motion::with_delay(core::Duration delay) const {
    Motion result = *this;
    result.delay_ = delay;
    return result;
}

Motion Motion::with_speed_multiplier(double multiplier) const {
    Motion result = *this;
    result.speed_multiplier_ = multiplier > 0.0 ? multiplier : 1.0;
    return result;
}

} // namespace ca::animation
