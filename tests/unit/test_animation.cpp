// ca::animation tests: springs, motions, the coordinator, transactions.
//
// The coordinator is the architecture's thesis in executable form, so the
// tests stress the properties that thesis promises (docs/00-overview.md §1):
//
//   * analytical sampling — a value can be evaluated at ANY time, in ANY
//     order, so a compositor that skips ticks (stalled UI thread) still ships
//     correct animation;
//   * lossless interruption — retargeting mid-flight preserves velocity by
//     construction, with no bookkeeping;
//   * rest reporting — the compositor can stop waking when everything settles.

#include <cmath>
#include <thread>
#include <utility>

#include "calcium/animation/animatable_property.hpp"
#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/animation/motion.hpp"
#include "calcium/animation/motion_scheme.hpp"
#include "calcium/animation/spring_configuration.hpp"
#include "calcium/animation/transaction.hpp"
#include "calcium/core/thread_affinity.hpp"
#include "calcium/core/time.hpp"

#include "calcium_test.hpp"

using ca::animation::AnimatableFloat;
using ca::animation::AnimatablePoint;
using ca::animation::AnimationCoordinator;
using ca::animation::Motion;
using ca::animation::MotionScheme;
using ca::animation::SpringConfiguration;
using ca::animation::Transaction;
using ca::animation::animate;
using ca::core::Timestamp;

namespace {

// The test's main thread plays the UI thread: it registers its role so the
// coordinator's CA_ASSERT_UI_THREAD entry points are enforced for real.
struct UiRoleRegistrar {
    UiRoleRegistrar() {
        ca::core::register_current_thread_role(ca::core::ThreadRole::ui);
    }
};
const UiRoleRegistrar g_ui_role;

// The compositor's side runs on its own thread with its own role — exactly
// the production split (docs/02-architecture.md §2.3). Ticking through a
// real thread boundary also exercises the intent queue and seqlock for real.
void compositor_tick(AnimationCoordinator& coordinator, Timestamp t) {
    std::thread compositor{[&coordinator, t] {
        ca::core::register_current_thread_role(ca::core::ThreadRole::compositor);
        coordinator.tick_and_publish(t);
    }};
    compositor.join();
}

Timestamp at(double seconds) {
    return Timestamp::from_seconds(seconds);
}

} // namespace

// ---------------------------------------------------------------------------
// Spring configuration math
// ---------------------------------------------------------------------------

CA_TEST(spring_response_and_damping_conversion) {
    // The documented table (docs/05 §5): response/damping -> mass/stiffness/
    // damping consistent with the stated damping ratio.
    const SpringConfiguration standard =
        SpringConfiguration::with_response_and_damping_ratio(0.40, 1.0);
    CA_CHECK_NEAR(standard.damping_ratio(), 1.0, 1e-9);
    CA_CHECK_NEAR(standard.natural_frequency(), 2.0 * 3.141592653589793 / 0.40, 1e-6);
    CA_CHECK_NEAR(standard.settling_duration_estimate(), 5.0 * 0.40 / (2.0 * 3.141592653589793), 1e-6);

    const SpringConfiguration playful =
        SpringConfiguration::with_response_and_damping_ratio(0.55, 0.52);
    CA_CHECK_NEAR(playful.damping_ratio(), 0.52, 1e-9);

    const SpringConfiguration snappy =
        SpringConfiguration::with_response_and_damping_ratio(0.28, 1.0);
    CA_CHECK_NEAR(snappy.damping_ratio(), 1.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Motions and schemes
// ---------------------------------------------------------------------------

CA_TEST(named_motions_carry_their_name) {
    CA_CHECK(Motion::standard().named() == Motion::Named::standard);
    CA_CHECK(Motion::emphasized().named() == Motion::Named::emphasized);
    CA_CHECK(Motion::playful().named() == Motion::Named::playful);
    CA_CHECK(Motion::immediate().kind() == Motion::Kind::immediate);
    CA_CHECK(Motion::spring({1.0, 100.0, 10.0, 0.0}).named() == Motion::Named::none);
}

CA_TEST(scheme_resolves_named_motions) {
    const MotionScheme scheme = MotionScheme::apple_like();
    const Motion resolved = scheme.resolve(Motion::standard());
    CA_CHECK(resolved.named() == Motion::Named::none);  // resolved form
    CA_CHECK_NEAR(resolved.spring_configuration().stiffness, 250.0, 0.5);
    CA_CHECK_NEAR(resolved.spring_configuration().damping, 31.6, 0.5);
    CA_CHECK_NEAR(resolved.spring_configuration().damping_ratio(), 1.0, 1e-6);
}

CA_TEST(reduced_motion_shortens_responses) {
    const MotionScheme reduced = MotionScheme::reduced_motion();
    const Motion resolved = reduced.resolve(Motion::playful());
    CA_CHECK_NEAR(resolved.spring_configuration().damping_ratio(), 1.0, 1e-6);
    CA_CHECK(resolved.spring_configuration().settling_duration_estimate()
             < MotionScheme::apple_like()
                   .resolve(Motion::playful())
                   .spring_configuration()
                   .settling_duration_estimate());
}

// ---------------------------------------------------------------------------
// The coordinator
// ---------------------------------------------------------------------------

CA_TEST(coordinator_springs_to_target) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    CA_CHECK(coordinator.has_value());

    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    CA_CHECK(property_result.has_value());
    auto property = std::move(property_result).take_value();
    CA_CHECK(property.is_at_rest());

    // Spring from 0 to 100 with a snappy motion; the model updates at once.
    property.set_value(100.0f, Motion::snappy());
    CA_CHECK(property.model_value() == 100.0f);

    // The intent applies at the next compositor tick; only then does the
    // property leave rest.
    compositor_tick(*coordinator.value(), at(0.0));
    CA_CHECK(!property.is_at_rest());

    // The presentation lags until the compositor ticks.
    compositor_tick(*coordinator.value(), at(0.016));
    const float first = property.presentation_value();
    CA_CHECK(first > 0.0f && first < 100.0f);

    // After the settling time the value has arrived.
    compositor_tick(*coordinator.value(), at(1.0));
    compositor_tick(*coordinator.value(), at(1.016));
    CA_CHECK_NEAR(property.presentation_value(), 100.0, 0.5);
    CA_CHECK(property.is_at_rest());
}

CA_TEST(analytical_sampling_skips_ticks) {
    // The compositor may stall (a heavy frame) and then tick once at a much
    // later time: the analytical kernel must produce the same value as the
    // step-by-step path — animation time is absolute, not step count.
    auto a = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto b = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto pa = AnimatableFloat::create(*a.value(), 0.0f);
    auto pb = AnimatableFloat::create(*b.value(), 0.0f);
    auto property_a = std::move(pa).take_value();
    auto property_b = std::move(pb).take_value();

    property_a.set_value(50.0f, Motion::standard());
    property_b.set_value(50.0f, Motion::standard());

    // A ticks every 16.7 ms (60 Hz); B stalls for 500 ms then ticks once.
    compositor_tick(*a.value(), at(0.0));
    for (int step = 1; step <= 30; ++step) {
        compositor_tick(*a.value(), at(step * 0.0167));
    }
    compositor_tick(*b.value(), at(0.0));
    compositor_tick(*b.value(), at(30 * 0.0167));

    CA_CHECK_NEAR(property_a.presentation_value(),
                  property_b.presentation_value(), 1e-6);
}

CA_TEST(retarget_mid_flight_preserves_continuity) {
    // Interrupting a spring mid-flight must not snap: the presentation value
    // is continuous across the retarget, and the new target is reached.
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property = std::move(property_result).take_value();

    property.set_value(100.0f, Motion::standard());
    compositor_tick(*coordinator.value(), at(0.0));
    compositor_tick(*coordinator.value(), at(0.2));  // mid-flight
    const float before = property.presentation_value();
    CA_CHECK(before > 10.0f && before < 100.0f);  // genuinely moving

    // Retarget to 30 while moving. The retarget preserves the current
    // velocity (additive impulse superposition), so the value may continue
    // upward briefly before the new spring pulls it down — what must not
    // happen is a snap.
    property.set_value(30.0f, Motion::standard());
    compositor_tick(*coordinator.value(), at(0.2));  // intent applied
    compositor_tick(*coordinator.value(), at(0.216));
    const float after = property.presentation_value();
    CA_CHECK(std::abs(after - before) < 15.0f);  // continuous, no snap

    // 100 ms later the new target has clearly taken over.
    compositor_tick(*coordinator.value(), at(0.3));
    const float turning = property.presentation_value();
    CA_CHECK(turning < before);

    compositor_tick(*coordinator.value(), at(3.0));
    CA_CHECK_NEAR(property.presentation_value(), 30.0, 0.5);
    CA_CHECK(property.is_at_rest());
}

CA_TEST(set_immediate_snaps) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property = std::move(property_result).take_value();

    property.set_value(100.0f, Motion::standard());
    compositor_tick(*coordinator.value(), at(0.0));
    property.set_value_immediately(42.0f);
    compositor_tick(*coordinator.value(), at(0.016));
    CA_CHECK_NEAR(property.presentation_value(), 42.0, 1e-6);
    CA_CHECK(property.is_at_rest());
}

CA_TEST(rest_callback_fires_once_on_dispatch) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property = std::move(property_result).take_value();

    int firings = 0;
    property.on_reach_rest([&] { ++firings; });

    property.set_value(10.0f, Motion::snappy());
    for (int step = 0; step <= 120; ++step) {
        compositor_tick(*coordinator.value(), at(step * 0.0167));
        coordinator.value()->dispatch_rest_callbacks();
    }
    CA_CHECK(firings == 1);

    // The callback fires once per registration; register again for the
    // second animation.
    property.on_reach_rest([&] { ++firings; });
    property.set_value(20.0f, Motion::snappy());
    for (int step = 0; step <= 120; ++step) {
        compositor_tick(*coordinator.value(), at(10.0 + step * 0.0167));
        coordinator.value()->dispatch_rest_callbacks();
    }
    CA_CHECK(firings == 2);
}

CA_TEST(two_dimensional_property) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result =
        AnimatablePoint::create(*coordinator.value(), {10.0f, 20.0f});
    auto property = std::move(property_result).take_value();

    property.set_value({400.0f, 300.0f}, Motion::standard());
    compositor_tick(*coordinator.value(), at(0.0));
    compositor_tick(*coordinator.value(), at(0.2));
    const ca::geometry::Point mid = property.presentation_value();
    CA_CHECK(mid.x > 10.0f && mid.x < 400.0f);
    CA_CHECK(mid.y > 20.0f && mid.y < 300.0f);

    compositor_tick(*coordinator.value(), at(3.0));
    const ca::geometry::Point end = property.presentation_value();
    CA_CHECK_NEAR(end.x, 400.0, 0.5);
    CA_CHECK_NEAR(end.y, 300.0, 0.5);
}

CA_TEST(transaction_sets_ambient_motion) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property = std::move(property_result).take_value();

    // animate() with an explicit motion; set_value picks it up.
    ca::animation::animate(Motion::gentle(), [&] {
        property.set_value(100.0f);
    });
    CA_CHECK(property.model_value() == 100.0f);

    // Outside any transaction the ambient motion is the scheme default
    // (standard); inside the animate block it was gentle.
    CA_CHECK(Transaction::current().default_motion().named() == Motion::Named::standard);

    compositor_tick(*coordinator.value(), at(0.0));
    compositor_tick(*coordinator.value(), at(0.016));
    const float first = property.presentation_value();
    CA_CHECK(first > 0.0f && first < 100.0f);
}

CA_TEST(completion_fires_when_properties_settle) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto pa = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto pb = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property_a = std::move(pa).take_value();
    auto property_b = std::move(pb).take_value();

    bool completed = false;
    ca::animation::animate_with_completion(Motion::snappy(), [&] {
        property_a.set_value(10.0f);
        property_b.set_value(20.0f);
    }, [&] { completed = true; });

    for (int step = 0; step <= 120 && !completed; ++step) {
        compositor_tick(*coordinator.value(), at(step * 0.0167));
        coordinator.value()->dispatch_rest_callbacks();
    }
    CA_CHECK(completed);
    CA_CHECK(property_a.is_at_rest());
    CA_CHECK(property_b.is_at_rest());
}

CA_TEST(disables_animation_snaps) {
    auto coordinator = AnimationCoordinator::create(
        {.max_animated_properties = 16, .max_concurrent_gestures = 4});
    auto property_result = AnimatableFloat::create(*coordinator.value(), 0.0f);
    auto property = std::move(property_result).take_value();

    {
        Transaction transaction = Transaction::begin();
        transaction.set_disables_animation(true);
        property.set_value(77.0f);
        transaction.commit();
    }
    compositor_tick(*coordinator.value(), at(0.0));
    CA_CHECK_NEAR(property.presentation_value(), 77.0, 1e-6);
}

CA_TEST_MAIN()
