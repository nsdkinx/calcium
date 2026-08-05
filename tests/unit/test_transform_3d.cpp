// Transform3D tests.
//
// This is the most load-bearing math in the framework: every animated transform
// in every layer flows through decompose -> interpolate -> recompose. An error
// here shows up as subtly wrong motion everywhere, so the round-trip tolerance
// (1e-6) is an M0 exit criterion.

#include "calcium/geometry/transform_3d.hpp"

#include <cmath>
#include <numbers>

#include "calcium_test.hpp"

using ca::geometry::Point;
using ca::geometry::Quaternion;
using ca::geometry::Transform3D;
using ca::geometry::Vector3;
using ca::geometry::Vector4;

namespace {

constexpr double round_trip_tolerance = 1e-6;

void check_transforms_near(const Transform3D& actual, const Transform3D& expected,
                           double tolerance, const char* label) {
    const double* a = &actual.m11;
    const double* e = &expected.m11;
    for (int index = 0; index < 16; ++index) {
        if (!ca::test::nearly_equal(a[index], e[index], tolerance)) {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer),
                          "%s: element m%d%d expected %.12g, got %.12g",
                          label, (index / 4) + 1, (index % 4) + 1,
                          e[index], a[index]);
            ca::test::report_failure(__FILE__, __LINE__, buffer);
            return;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Convention: Twell / CATransform3D compatibility.
//
// Row-vector, row-major, translation in the fourth ROW. If this ever regresses,
// every transform in the framework silently transposes.
// ---------------------------------------------------------------------------

CA_TEST(translation_occupies_fourth_row) {
    const Transform3D t = Transform3D::make_translation(10.0, 20.0, 30.0);

    // Translation must be in row 4, matching twell_transform3d_make_translation.
    CA_CHECK_NEAR(t.m41, 10.0, 0.0);
    CA_CHECK_NEAR(t.m42, 20.0, 0.0);
    CA_CHECK_NEAR(t.m43, 30.0, 0.0);

    // It must NOT be in the fourth column, which is the column-vector convention.
    CA_CHECK_NEAR(t.m14, 0.0, 0.0);
    CA_CHECK_NEAR(t.m24, 0.0, 0.0);
    CA_CHECK_NEAR(t.m34, 0.0, 0.0);
}

CA_TEST(perspective_writes_negative_reciprocal_into_m34) {
    // twell_transform3d_set_perspective writes -1/depth into m34.
    const Transform3D t = Transform3D::make_perspective(500.0);
    CA_CHECK_NEAR(t.m34, -1.0 / 500.0, 1e-15);

    // A non-positive depth is ignored rather than producing infinities.
    CA_CHECK(Transform3D::make_perspective(0.0).is_identity());
    CA_CHECK(Transform3D::make_perspective(-1.0).is_identity());
}

CA_TEST(point_transform_applies_row_vector_multiplication) {
    const Transform3D t = Transform3D::make_translation(5.0, 7.0, 0.0);
    const Point transformed = t.apply_to_point(Point{1.0f, 2.0f});
    CA_CHECK_NEAR(transformed.x, 6.0, 1e-6);
    CA_CHECK_NEAR(transformed.y, 9.0, 1e-6);
}

CA_TEST(concatenation_applies_left_operand_first) {
    // Scale by 2, then translate by 10: the translation must NOT be scaled.
    const Transform3D scale_then_translate =
        Transform3D::make_scale(2.0, 2.0, 1.0)
            .concatenating(Transform3D::make_translation(10.0, 0.0, 0.0));

    const Point result = scale_then_translate.apply_to_point(Point{3.0f, 0.0f});
    CA_CHECK_NEAR(result.x, 16.0, 1e-6);  // 3*2 + 10

    // Reverse order: the translation is scaled.
    const Transform3D translate_then_scale =
        Transform3D::make_translation(10.0, 0.0, 0.0)
            .concatenating(Transform3D::make_scale(2.0, 2.0, 1.0));

    const Point reversed = translate_then_scale.apply_to_point(Point{3.0f, 0.0f});
    CA_CHECK_NEAR(reversed.x, 26.0, 1e-6);  // (3 + 10) * 2
}

// ---------------------------------------------------------------------------
// Inversion and determinant
// ---------------------------------------------------------------------------

CA_TEST(identity_inverts_to_identity) {
    const auto inverse = Transform3D::identity().inverted();
    CA_CHECK(inverse.has_value());
    check_transforms_near(*inverse, Transform3D::identity(), 1e-12,
                          "identity inverse");
}

CA_TEST(inverse_composes_to_identity) {
    Transform3D t = Transform3D::make_translation(12.0, -4.0, 7.0)
                        .concatenating(Transform3D::make_scale(2.0, 3.0, 0.5))
                        .concatenating(Transform3D::make_rotation_about_axis(
                            Vector3{0.3, 0.7, 0.2}, 0.9));
    t.m34 = -1.0 / 800.0;  // include perspective

    const auto inverse = t.inverted();
    CA_CHECK(inverse.has_value());
    check_transforms_near(t.concatenating(*inverse), Transform3D::identity(),
                          1e-9, "t * inverse(t)");
}

CA_TEST(singular_matrix_reports_no_inverse) {
    const Transform3D degenerate = Transform3D::make_scale(1.0, 0.0, 1.0);
    CA_CHECK(!degenerate.inverted().has_value());
}

CA_TEST(determinant_of_scale_is_product_of_factors) {
    const Transform3D t = Transform3D::make_scale(2.0, 3.0, 4.0);
    CA_CHECK_NEAR(t.determinant(), 24.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Decompose / recompose round trip  — M0 exit criterion
// ---------------------------------------------------------------------------

CA_TEST(round_trip_identity) {
    const auto components = Transform3D::identity().decompose();
    CA_CHECK(components.has_value());
    CA_CHECK_NEAR(components->translation.x, 0.0, round_trip_tolerance);
    CA_CHECK_NEAR(components->scale.x, 1.0, round_trip_tolerance);
    CA_CHECK_NEAR(components->rotation.w, 1.0, round_trip_tolerance);
    check_transforms_near(Transform3D::recompose(*components),
                          Transform3D::identity(), round_trip_tolerance,
                          "identity round trip");
}

CA_TEST(round_trip_pure_translation) {
    const Transform3D original = Transform3D::make_translation(123.0, -45.0, 6.5);
    const auto components = original.decompose();
    CA_CHECK(components.has_value());

    CA_CHECK_NEAR(components->translation.x, 123.0, round_trip_tolerance);
    CA_CHECK_NEAR(components->translation.y, -45.0, round_trip_tolerance);
    CA_CHECK_NEAR(components->translation.z, 6.5, round_trip_tolerance);

    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "translation round trip");
}

CA_TEST(round_trip_pure_scale) {
    const Transform3D original = Transform3D::make_scale(2.0, 0.5, 3.0);
    const auto components = original.decompose();
    CA_CHECK(components.has_value());

    CA_CHECK_NEAR(components->scale.x, 2.0, round_trip_tolerance);
    CA_CHECK_NEAR(components->scale.y, 0.5, round_trip_tolerance);
    CA_CHECK_NEAR(components->scale.z, 3.0, round_trip_tolerance);

    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "scale round trip");
}

CA_TEST(round_trip_pure_rotation) {
    for (const double angle : {0.1, 0.5, 1.0, 2.0, 3.0}) {
        const Transform3D original = Transform3D::make_rotation_about_axis(
            Vector3{0.0, 0.0, 1.0}, angle);
        const auto components = original.decompose();
        CA_CHECK(components.has_value());
        if (!components.has_value()) {
            continue;
        }
        check_transforms_near(Transform3D::recompose(*components), original,
                              round_trip_tolerance, "z rotation round trip");
    }
}

CA_TEST(round_trip_rotation_about_arbitrary_axis) {
    const Transform3D original = Transform3D::make_rotation_about_axis(
        Vector3{0.4, -0.6, 0.7}, 1.234);
    const auto components = original.decompose();
    CA_CHECK(components.has_value());
    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "arbitrary axis round trip");
}

CA_TEST(round_trip_combined_translation_rotation_scale) {
    const Transform3D original =
        Transform3D::make_scale(1.5, 2.5, 0.75)
            .concatenating(Transform3D::make_rotation_about_axis(
                Vector3{0.2, 0.5, 0.84}, 0.77))
            .concatenating(Transform3D::make_translation(30.0, -12.0, 4.0));

    const auto components = original.decompose();
    CA_CHECK(components.has_value());
    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "combined TRS round trip");
}

CA_TEST(round_trip_with_perspective) {
    Transform3D original =
        Transform3D::make_scale(1.2, 1.2, 1.0)
            .concatenating(Transform3D::make_rotation_about_axis(
                Vector3{0.0, 1.0, 0.0}, 0.6))
            .concatenating(Transform3D::make_translation(15.0, 25.0, -5.0));
    original.m34 = -1.0 / 600.0;

    const auto components = original.decompose();
    CA_CHECK(components.has_value());
    check_transforms_near(Transform3D::recompose(*components), original,
                          1e-5, "perspective round trip");
}

CA_TEST(round_trip_with_skew) {
    const Transform3D original =
        Transform3D::make_skew(0.35, 0.0)
            .concatenating(Transform3D::make_translation(4.0, 8.0, 0.0));

    const auto components = original.decompose();
    CA_CHECK(components.has_value());
    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "skew round trip");
}

CA_TEST(decompose_rejects_singular_matrix) {
    CA_CHECK(!Transform3D::make_scale(1.0, 0.0, 1.0).decompose().has_value());

    Transform3D zero_w = Transform3D::identity();
    zero_w.m44 = 0.0;
    CA_CHECK(!zero_w.decompose().has_value());
}

CA_TEST(decompose_folds_reflection_into_scale_sign) {
    // A negative scale factor is a reflection; the rotation must stay a proper
    // rotation and the sign must live in the scale.
    const Transform3D original = Transform3D::make_scale(-2.0, 2.0, 2.0);
    const auto components = original.decompose();
    CA_CHECK(components.has_value());
    if (!components.has_value()) {
        return;
    }
    const double sign_product = components->scale.x * components->scale.y
                              * components->scale.z;
    CA_CHECK(sign_product < 0.0);
    check_transforms_near(Transform3D::recompose(*components), original,
                          round_trip_tolerance, "reflection round trip");
}

// ---------------------------------------------------------------------------
// Interpolation — why decomposition exists (docs/05 section 2.1)
// ---------------------------------------------------------------------------

CA_TEST(interpolation_endpoints_are_exact) {
    const Transform3D from = Transform3D::make_translation(0.0, 0.0, 0.0);
    const Transform3D to = Transform3D::make_translation(100.0, 50.0, 0.0);

    check_transforms_near(Transform3D::interpolate(from, to, 0.0), from,
                          round_trip_tolerance, "interpolate at t=0");
    check_transforms_near(Transform3D::interpolate(from, to, 1.0), to,
                          round_trip_tolerance, "interpolate at t=1");
}

CA_TEST(translation_interpolates_linearly) {
    const Transform3D from = Transform3D::make_translation(0.0, 0.0, 0.0);
    const Transform3D to = Transform3D::make_translation(100.0, 200.0, 0.0);
    const Transform3D midpoint = Transform3D::interpolate(from, to, 0.5);

    CA_CHECK_NEAR(midpoint.m41, 50.0, round_trip_tolerance);
    CA_CHECK_NEAR(midpoint.m42, 100.0, round_trip_tolerance);
}

CA_TEST(rotation_interpolation_preserves_scale) {
    // The failure this guards against: naive matrix lerp between 0 and 180
    // degrees passes through a degenerate matrix, collapsing the object. With
    // slerp, the basis stays orthonormal at every t.
    const Transform3D from = Transform3D::make_rotation_about_axis(
        Vector3{0.0, 0.0, 1.0}, 0.0);
    const Transform3D to = Transform3D::make_rotation_about_axis(
        Vector3{0.0, 0.0, 1.0}, std::numbers::pi);

    for (const double t : {0.25, 0.5, 0.75}) {
        const Transform3D interpolated = Transform3D::interpolate(from, to, t);
        const auto components = interpolated.decompose();
        CA_CHECK(components.has_value());
        if (!components.has_value()) {
            continue;
        }
        // Unit scale must be preserved throughout: no collapse, no shear.
        CA_CHECK_NEAR(std::abs(components->scale.x), 1.0, 1e-6);
        CA_CHECK_NEAR(std::abs(components->scale.y), 1.0, 1e-6);
        CA_CHECK_NEAR(std::abs(components->scale.z), 1.0, 1e-6);

        // The determinant of a rotation is 1; a collapsed matrix would be 0.
        CA_CHECK_NEAR(interpolated.determinant(), 1.0, 1e-6);
    }
}

CA_TEST(rotation_interpolation_reaches_expected_midpoint_angle) {
    const double quarter_turn = std::numbers::pi / 2.0;
    const Transform3D from = Transform3D::identity();
    const Transform3D to = Transform3D::make_rotation_about_axis(
        Vector3{0.0, 0.0, 1.0}, quarter_turn);

    const Transform3D midpoint = Transform3D::interpolate(from, to, 0.5);
    const Transform3D expected = Transform3D::make_rotation_about_axis(
        Vector3{0.0, 0.0, 1.0}, quarter_turn / 2.0);

    check_transforms_near(midpoint, expected, 1e-6, "half of a quarter turn");
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CA_TEST(affine_2d_detection) {
    CA_CHECK(Transform3D::identity().is_affine_2d());
    CA_CHECK(Transform3D::make_translation(10.0, 20.0, 0.0).is_affine_2d());
    CA_CHECK(Transform3D::make_scale(2.0, 3.0, 1.0).is_affine_2d());

    // z translation and perspective both break 2D affinity.
    CA_CHECK(!Transform3D::make_translation(0.0, 0.0, 5.0).is_affine_2d());
    CA_CHECK(!Transform3D::make_perspective(500.0).is_affine_2d());
    CA_CHECK(!Transform3D::make_rotation_about_axis(Vector3{1.0, 0.0, 0.0}, 0.5)
                  .is_affine_2d());
}

CA_TEST_MAIN()
