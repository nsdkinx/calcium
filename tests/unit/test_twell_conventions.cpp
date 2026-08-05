// Twell convention compatibility.
//
// ca::geometry::Transform3D and ca::geometry::Quaternion mirror Twell's spatial
// types so the animation path can hand matrices across without a transpose or a
// re-derivation. This test compiles Twell and asserts the two agree numerically.
//
// It exists because a convention mismatch here is nearly undetectable by
// inspection: translation-only matrices look identical under both conventions,
// and the error only appears once rotation and translation are combined — by
// which point the wrongness is spread across the whole framework.
//
// This is the ONLY test that includes twell.h directly. Production code reaches
// Twell exclusively through ca::animation (P5), and the header hygiene gate
// enforces that for public headers.

#include "calcium/geometry/quaternion.hpp"
#include "calcium/geometry/transform_3d.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "calcium_test.hpp"

// Twell is a single-header library; this translation unit provides the impl.
#define TWELL_IMPL
#include "twell.h"

using ca::geometry::Quaternion;
using ca::geometry::Transform3D;
using ca::geometry::Vector3;

namespace {

constexpr double agreement_tolerance = 1e-12;

void check_matrices_agree(const Transform3D& calcium,
                          const twell_transform3d& twell,
                          const char* label) {
    const double* a = &calcium.m11;
    const double* b = &twell.m11;
    for (int index = 0; index < 16; ++index) {
        if (!ca::test::nearly_equal(a[index], b[index], agreement_tolerance)) {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer),
                          "%s: m%d%d — Calcium %.17g, Twell %.17g",
                          label, (index / 4) + 1, (index % 4) + 1,
                          a[index], b[index]);
            ca::test::report_failure(__FILE__, __LINE__, buffer);
            return;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Memory layout
// ---------------------------------------------------------------------------

CA_TEST(transform_layout_matches_twell_exactly) {
    // Same size, same alignment, same field order — so the two are
    // bit-compatible and conversion is a reinterpretation, not a computation.
    static_assert(sizeof(Transform3D) == sizeof(twell_transform3d));
    static_assert(alignof(Transform3D) == alignof(twell_transform3d));
    static_assert(sizeof(Transform3D) == 16 * sizeof(double));

    Transform3D calcium = Transform3D::identity();
    calcium.m11 = 1.5; calcium.m12 = 2.5; calcium.m13 = 3.5; calcium.m14 = 4.5;
    calcium.m21 = 5.5; calcium.m22 = 6.5; calcium.m23 = 7.5; calcium.m24 = 8.5;
    calcium.m31 = 9.5; calcium.m32 = 10.5; calcium.m33 = 11.5; calcium.m34 = 12.5;
    calcium.m41 = 13.5; calcium.m42 = 14.5; calcium.m43 = 15.5; calcium.m44 = 16.5;

    // Field-by-field ordering must be identical when read as a flat array.
    const double* flat = &calcium.m11;
    for (int index = 0; index < 16; ++index) {
        CA_CHECK_NEAR(flat[index], 1.5 + static_cast<double>(index), 0.0);
    }
}

CA_TEST(quaternion_layout_matches_twell_exactly) {
    static_assert(sizeof(Quaternion) == sizeof(twell_quaternion));
    static_assert(alignof(Quaternion) == alignof(twell_quaternion));
    static_assert(sizeof(Quaternion) == 4 * sizeof(double));

    // {x, y, z, w} order, w last.
    const Quaternion calcium{1.0, 2.0, 3.0, 4.0};
    const double* flat = &calcium.x;
    CA_CHECK_NEAR(flat[0], 1.0, 0.0);
    CA_CHECK_NEAR(flat[1], 2.0, 0.0);
    CA_CHECK_NEAR(flat[2], 3.0, 0.0);
    CA_CHECK_NEAR(flat[3], 4.0, 0.0);
}

// ---------------------------------------------------------------------------
// Matrix construction
// ---------------------------------------------------------------------------

CA_TEST(identity_agrees_with_twell) {
    check_matrices_agree(Transform3D::identity(), twell_transform3d_identity(),
                         "identity");
}

CA_TEST(translation_agrees_with_twell) {
    check_matrices_agree(
        Transform3D::make_translation(12.0, -34.0, 56.0),
        twell_transform3d_make_translation(12.0, -34.0, 56.0),
        "translation");
}

CA_TEST(scale_agrees_with_twell) {
    check_matrices_agree(
        Transform3D::make_scale(2.0, 0.5, 3.0),
        twell_transform3d_make_scale(2.0, 0.5, 3.0),
        "scale");
}

CA_TEST(perspective_agrees_with_twell) {
    twell_transform3d expected = twell_transform3d_identity();
    twell_transform3d_set_perspective(&expected, 500.0);
    check_matrices_agree(Transform3D::make_perspective(500.0), expected,
                         "perspective");
}

CA_TEST(rotation_matrix_agrees_with_twell) {
    // The critical case: if the row/column convention diverged, this fails while
    // translation and scale still pass.
    struct Case { Vector3 axis; double angle; const char* label; };
    const Case cases[] = {
        {{0.0, 0.0, 1.0}, std::numbers::pi / 4.0,  "z 45deg"},
        {{1.0, 0.0, 0.0}, std::numbers::pi / 3.0,  "x 60deg"},
        {{0.0, 1.0, 0.0}, std::numbers::pi / 6.0,  "y 30deg"},
        {{0.3, 0.5, 0.8}, 1.234,                   "arbitrary axis"},
    };

    for (const Case& test_case : cases) {
        const Quaternion calcium_quaternion = Quaternion::from_axis_angle(
            test_case.axis, test_case.angle);
        const twell_quaternion twell_q = twell_quaternion_make_axis_angle(
            test_case.axis.x, test_case.axis.y, test_case.axis.z,
            test_case.angle);

        CA_CHECK_NEAR(calcium_quaternion.x, twell_q.x, 1e-15);
        CA_CHECK_NEAR(calcium_quaternion.y, twell_q.y, 1e-15);
        CA_CHECK_NEAR(calcium_quaternion.z, twell_q.z, 1e-15);
        CA_CHECK_NEAR(calcium_quaternion.w, twell_q.w, 1e-15);

        check_matrices_agree(Transform3D::make_rotation(calcium_quaternion),
                             twell_transform3d_from_quaternion(twell_q),
                             test_case.label);
    }
}

CA_TEST(euler_construction_agrees_with_twell) {
    const double pitch = 0.3, yaw = -0.7, roll = 1.1;
    const Quaternion calcium = Quaternion::from_euler_angles(pitch, yaw, roll);
    const twell_quaternion twell = twell_quaternion_make_euler(pitch, yaw, roll);

    CA_CHECK_NEAR(calcium.x, twell.x, 1e-15);
    CA_CHECK_NEAR(calcium.y, twell.y, 1e-15);
    CA_CHECK_NEAR(calcium.z, twell.z, 1e-15);
    CA_CHECK_NEAR(calcium.w, twell.w, 1e-15);
}

// ---------------------------------------------------------------------------
// Slerp
// ---------------------------------------------------------------------------

CA_TEST(slerp_agrees_with_twell_across_the_range) {
    const Quaternion calcium_from = Quaternion::from_axis_angle(
        Vector3{0.2, 0.4, 0.9}, 0.3);
    const Quaternion calcium_to = Quaternion::from_axis_angle(
        Vector3{0.8, -0.1, 0.5}, 2.1);

    const twell_quaternion twell_from = twell_quaternion_make_axis_angle(
        0.2, 0.4, 0.9, 0.3);
    const twell_quaternion twell_to = twell_quaternion_make_axis_angle(
        0.8, -0.1, 0.5, 2.1);

    for (int step = 0; step <= 10; ++step) {
        const double t = static_cast<double>(step) / 10.0;
        const Quaternion calcium = Quaternion::slerp(calcium_from, calcium_to, t);
        const twell_quaternion twell = twell_quaternion_slerp(twell_from,
                                                              twell_to, t);
        CA_CHECK_NEAR(calcium.x, twell.x, 1e-12);
        CA_CHECK_NEAR(calcium.y, twell.y, 1e-12);
        CA_CHECK_NEAR(calcium.z, twell.z, 1e-12);
        CA_CHECK_NEAR(calcium.w, twell.w, 1e-12);
    }
}

CA_TEST(slerp_shorter_arc_flip_agrees_with_twell) {
    // Both implementations must negate the second operand when the dot product
    // is negative, or interpolation takes the long way around.
    const Quaternion calcium_from = Quaternion::from_axis_angle(
        Vector3{0.0, 0.0, 1.0}, 0.1);
    const Quaternion calcium_to = Quaternion::from_axis_angle(
        Vector3{0.0, 0.0, 1.0}, 6.0);  // nearly a full turn

    const twell_quaternion twell_from = twell_quaternion_make_axis_angle(
        0.0, 0.0, 1.0, 0.1);
    const twell_quaternion twell_to = twell_quaternion_make_axis_angle(
        0.0, 0.0, 1.0, 6.0);

    const Quaternion calcium = Quaternion::slerp(calcium_from, calcium_to, 0.5);
    const twell_quaternion twell = twell_quaternion_slerp(twell_from, twell_to, 0.5);

    CA_CHECK_NEAR(calcium.z, twell.z, 1e-12);
    CA_CHECK_NEAR(calcium.w, twell.w, 1e-12);
}

// ---------------------------------------------------------------------------
// Twell's own behaviour, as relied upon by the architecture
// ---------------------------------------------------------------------------

CA_TEST(twell_arena_sizing_is_queryable_and_creation_succeeds) {
    // docs/05 section 3: the arena is sized by asking Twell, never by guessing.
    const std::size_t required = twell_get_memory_requirement(128, 32);
    CA_CHECK(required > 0);

    std::vector<unsigned char> arena(required);
    twell_context* context = twell_context_create(
        arena.data(), arena.size(), 128, 32);
    CA_CHECK(context != nullptr);

    // An undersized arena must fail rather than corrupt memory.
    std::vector<unsigned char> too_small(required / 2);
    CA_CHECK(twell_context_create(too_small.data(), too_small.size(), 128, 32)
             == nullptr);
}

CA_TEST(twell_spring_preserves_velocity_across_interruption) {
    // The additive state machine is the property Calcium's whole animation model
    // depends on: retargeting mid-flight must not reset velocity.
    const std::size_t required = twell_get_memory_requirement(16, 4);
    std::vector<unsigned char> arena(required);
    twell_context* context = twell_context_create(arena.data(), arena.size(), 16, 4);
    CA_CHECK(context != nullptr);
    if (context == nullptr) {
        return;
    }

    const twell_spring_config spring{
        .mass = 1.0, .stiffness = 250.0, .damping = 18.0, .initial_velocity = 0.0};

    const twell_property_id property = twell_property_create_with_unit(
        context, 0.0, TWELL_UNIT_PIXELS);

    constexpr double frame = 1.0 / 120.0;
    double time = 0.0;
    twell_property_animate_to_target(context, property, 100.0, spring, time);

    // Advance until the property is moving fast, sampling the frame either side
    // of the retarget point so velocity can be measured by finite difference
    // (Twell exposes no velocity getter for properties).
    double value_previous = 0.0;
    for (int step = 0; step < 10; ++step) {
        value_previous = twell_property_get_presentation_value(context, property);
        time += frame;
        twell_context_tick(context, time, nullptr, 0);
    }
    const double value_before = twell_property_get_presentation_value(context,
                                                                     property);
    CA_CHECK(value_before > 0.0);
    CA_CHECK(value_before < 100.0);

    const double velocity_before = (value_before - value_previous) / frame;
    CA_CHECK(velocity_before > 100.0);  // genuinely in flight, not nearly at rest

    // Retarget backwards, to 0, while travelling forwards at speed.
    twell_property_animate_to_target(context, property, 0.0, spring, time);
    time += frame;
    twell_context_tick(context, time, nullptr, 0);
    const double value_after = twell_property_get_presentation_value(context,
                                                                     property);
    const double velocity_after = (value_after - value_before) / frame;

    // This is the assertion that matters. Momentum carries the value PAST the
    // old position even though the new target lies behind it: velocity survived
    // the retarget. An implementation that reset velocity to zero would move
    // immediately toward 0 and make this negative.
    CA_CHECK(velocity_after > 0.0);

    // And the handoff is continuous rather than a snap: one frame of a spring
    // decelerating from ~800 px/s changes speed, but cannot reverse or spike.
    CA_CHECK(velocity_after < velocity_before);
    CA_CHECK(velocity_after > velocity_before * 0.5);

    // Contrast: a fresh property starting at the same position with no inherited
    // motion moves the other way on its first frame. That the two differ is what
    // makes the assertion above meaningful rather than incidental.
    const twell_property_id restarted = twell_property_create_with_unit(
        context, value_before, TWELL_UNIT_PIXELS);
    twell_property_animate_to_target(context, restarted, 0.0, spring, time);
    time += frame;
    twell_context_tick(context, time, nullptr, 0);
    const double restarted_velocity =
        (twell_property_get_presentation_value(context, restarted) - value_before)
        / frame;
    CA_CHECK(restarted_velocity < 0.0);
}

CA_TEST(twell_reports_resting_properties_for_idle_gating) {
    // docs/05 section 4: the rest queue is what lets the compositor stop waking.
    const std::size_t required = twell_get_memory_requirement(16, 4);
    std::vector<unsigned char> arena(required);
    twell_context* context = twell_context_create(arena.data(), arena.size(), 16, 4);
    CA_CHECK(context != nullptr);
    if (context == nullptr) {
        return;
    }

    const twell_spring_config spring{
        .mass = 1.0, .stiffness = 250.0, .damping = 31.6, .initial_velocity = 0.0};

    const twell_property_id property = twell_property_create_with_unit(
        context, 0.0, TWELL_UNIT_PIXELS);

    double time = 0.0;
    twell_property_animate_to_target(context, property, 100.0, spring, time);

    bool observed_rest = false;
    twell_property_id resting[8];

    // A critically damped spring at these constants settles well inside 2 s.
    for (int frame = 0; frame < 480 && !observed_rest; ++frame) {
        time += 1.0 / 120.0;
        if (twell_context_tick(context, time, resting, 8) > 0) {
            observed_rest = true;
        }
    }

    CA_CHECK(observed_rest);
    CA_CHECK_NEAR(twell_property_get_presentation_value(context, property),
                  100.0, 0.2);
}

CA_TEST_MAIN()
