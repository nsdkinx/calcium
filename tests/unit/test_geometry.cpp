// ca::geometry tests: points, sizes, rects, insets, quaternions.

#include "calcium/geometry/point.hpp"
#include "calcium/geometry/quaternion.hpp"
#include "calcium/geometry/rect.hpp"

#include <cmath>
#include <numbers>

#include "calcium_test.hpp"

using ca::geometry::EdgeInsets;
using ca::geometry::Point;
using ca::geometry::Quaternion;
using ca::geometry::Rect;
using ca::geometry::Size;
using ca::geometry::Vector3;

// ---------------------------------------------------------------------------
// Compile-time construction (docs/03 section 5: constexpr all geometry)
// ---------------------------------------------------------------------------

static_assert(Point{1.0f, 2.0f} + Point{3.0f, 4.0f} == Point{4.0f, 6.0f});
static_assert(Rect::from_xywh(0.0f, 0.0f, 10.0f, 20.0f).max_x() == 10.0f);
static_assert(Size{4.0f, 2.0f}.area() == 8.0f);
static_assert(EdgeInsets::all(8.0f).total_horizontal() == 16.0f);

CA_TEST(point_arithmetic) {
    CA_CHECK((Point{1.0f, 2.0f} + Point{3.0f, 4.0f}) == Point{4.0f, 6.0f});
    CA_CHECK((Point{5.0f, 5.0f} - Point{2.0f, 1.0f}) == Point{3.0f, 4.0f});
    CA_CHECK((Point{2.0f, 3.0f} * 2.0f) == Point{4.0f, 6.0f});
    CA_CHECK((-Point{1.0f, -2.0f}) == Point{-1.0f, 2.0f});

    // Division by zero yields zero rather than infinity: a NaN in the layout
    // tree propagates silently and is very hard to trace back.
    CA_CHECK((Point{4.0f, 4.0f} / 0.0f) == Point{0.0f, 0.0f});
}

CA_TEST(point_distance) {
    CA_CHECK_NEAR(Point{0.0f, 0.0f}.distance_to(Point{3.0f, 4.0f}), 5.0, 1e-6);
    CA_CHECK_NEAR(Point{0.0f, 0.0f}.squared_distance_to(Point{3.0f, 4.0f}),
                  25.0, 1e-6);
    CA_CHECK_NEAR(Point{3.0f, 4.0f}.magnitude(), 5.0, 1e-6);
}

CA_TEST(size_emptiness_treats_negative_as_empty) {
    CA_CHECK(Size::zero().is_empty());
    CA_CHECK(Size{0.0f, 10.0f}.is_empty());
    CA_CHECK(Size{-1.0f, 10.0f}.is_empty());
    CA_CHECK(!Size{1.0f, 1.0f}.is_empty());
}

CA_TEST(rect_edges_and_center) {
    const Rect rect = Rect::from_xywh(10.0f, 20.0f, 100.0f, 50.0f);
    CA_CHECK_NEAR(rect.min_x(), 10.0, 1e-6);
    CA_CHECK_NEAR(rect.min_y(), 20.0, 1e-6);
    CA_CHECK_NEAR(rect.max_x(), 110.0, 1e-6);
    CA_CHECK_NEAR(rect.max_y(), 70.0, 1e-6);
    CA_CHECK(rect.center() == Point{60.0f, 45.0f});
}

CA_TEST(rect_from_edges_and_center) {
    const Rect from_edges = Rect::from_edges(10.0f, 20.0f, 110.0f, 70.0f);
    CA_CHECK(from_edges == Rect::from_xywh(10.0f, 20.0f, 100.0f, 50.0f));

    const Rect centered = Rect::from_center_and_size(Point{50.0f, 50.0f},
                                                     Size{20.0f, 10.0f});
    CA_CHECK(centered == Rect::from_xywh(40.0f, 45.0f, 20.0f, 10.0f));
}

CA_TEST(rect_containment_is_half_open) {
    const Rect rect = Rect::from_xywh(0.0f, 0.0f, 10.0f, 10.0f);

    CA_CHECK(rect.contains_point(Point{0.0f, 0.0f}));    // min edge included
    CA_CHECK(rect.contains_point(Point{9.99f, 9.99f}));
    CA_CHECK(!rect.contains_point(Point{10.0f, 5.0f}));  // max edge excluded
    CA_CHECK(!rect.contains_point(Point{-0.01f, 5.0f}));

    // Half-open containment means adjacent rects tile without overlap, which is
    // what hit-testing needs.
    CA_CHECK(rect.contains_rect(Rect::from_xywh(2.0f, 2.0f, 5.0f, 5.0f)));
    CA_CHECK(!rect.contains_rect(Rect::from_xywh(5.0f, 5.0f, 10.0f, 10.0f)));
}

CA_TEST(rect_intersection_and_union) {
    const Rect a = Rect::from_xywh(0.0f, 0.0f, 10.0f, 10.0f);
    const Rect b = Rect::from_xywh(5.0f, 5.0f, 10.0f, 10.0f);

    CA_CHECK(a.intersects(b));
    CA_CHECK(a.intersection_with(b) == Rect::from_xywh(5.0f, 5.0f, 5.0f, 5.0f));
    CA_CHECK(a.union_with(b) == Rect::from_xywh(0.0f, 0.0f, 15.0f, 15.0f));

    const Rect disjoint = Rect::from_xywh(100.0f, 100.0f, 5.0f, 5.0f);
    CA_CHECK(!a.intersects(disjoint));
    CA_CHECK(a.intersection_with(disjoint).is_empty());

    // Union with an empty rect returns the non-empty operand unchanged.
    CA_CHECK(a.union_with(Rect::zero()) == a);
    CA_CHECK(Rect::zero().union_with(a) == a);
}

CA_TEST(rect_inset_and_outset_are_inverse) {
    const Rect rect = Rect::from_xywh(0.0f, 0.0f, 100.0f, 100.0f);
    const EdgeInsets insets{5.0f, 10.0f, 15.0f, 20.0f};

    const Rect inset = rect.inset_by(insets);
    CA_CHECK(inset == Rect::from_edges(10.0f, 5.0f, 80.0f, 85.0f));
    CA_CHECK(inset.outset_by(insets) == rect);
}

CA_TEST(rect_pixel_alignment_expands_outward) {
    const Rect fractional = Rect::from_edges(10.3f, 20.7f, 30.2f, 40.9f);

    // At 1x, aligning outward must fully cover the original.
    const Rect aligned = fractional.pixel_aligned_outward(1.0f);
    CA_CHECK(aligned.min_x() <= fractional.min_x());
    CA_CHECK(aligned.min_y() <= fractional.min_y());
    CA_CHECK(aligned.max_x() >= fractional.max_x());
    CA_CHECK(aligned.max_y() >= fractional.max_y());
    CA_CHECK_NEAR(aligned.min_x(), 10.0, 1e-6);
    CA_CHECK_NEAR(aligned.max_x(), 31.0, 1e-6);

    // At 2x, alignment lands on half-point boundaries.
    const Rect aligned_2x = fractional.pixel_aligned_outward(2.0f);
    CA_CHECK_NEAR(aligned_2x.min_x(), 10.0, 1e-6);
    CA_CHECK_NEAR(aligned_2x.min_y(), 20.5, 1e-6);
}

CA_TEST(edge_insets_constructors) {
    CA_CHECK(EdgeInsets::all(4.0f) == EdgeInsets{4.0f, 4.0f, 4.0f, 4.0f});
    CA_CHECK(EdgeInsets::symmetric(2.0f, 8.0f)
             == EdgeInsets{2.0f, 8.0f, 2.0f, 8.0f});
    CA_CHECK(EdgeInsets::horizontal(6.0f) == EdgeInsets{0.0f, 6.0f, 0.0f, 6.0f});
    CA_CHECK(EdgeInsets::vertical(6.0f) == EdgeInsets{6.0f, 0.0f, 6.0f, 0.0f});
}

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

CA_TEST(vector3_products) {
    const Vector3 x{1.0, 0.0, 0.0};
    const Vector3 y{0.0, 1.0, 0.0};

    CA_CHECK_NEAR(x.dot(y), 0.0, 1e-12);
    CA_CHECK(x.cross(y) == Vector3{0.0, 0.0, 1.0});
    CA_CHECK_NEAR(Vector3{3.0, 4.0, 0.0}.magnitude(), 5.0, 1e-12);
    CA_CHECK_NEAR(Vector3{3.0, 4.0, 0.0}.normalized().magnitude(), 1.0, 1e-12);

    // Normalizing a zero vector must not divide by zero.
    CA_CHECK(Vector3::zero().normalized() == Vector3::zero());
}

// ---------------------------------------------------------------------------
// Quaternions — must match Twell's conventions exactly
// ---------------------------------------------------------------------------

CA_TEST(quaternion_identity_is_w_one) {
    const Quaternion identity = Quaternion::identity();
    CA_CHECK_NEAR(identity.w, 1.0, 0.0);
    CA_CHECK_NEAR(identity.x, 0.0, 0.0);
    CA_CHECK_NEAR(identity.magnitude(), 1.0, 1e-12);
}

CA_TEST(quaternion_from_axis_angle_matches_twell_formula) {
    // twell_quaternion_make_axis_angle: xyz = axis * sin(angle/2)/len,
    // w = cos(angle/2).
    const double angle = std::numbers::pi / 2.0;
    const Quaternion q = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0}, angle);

    CA_CHECK_NEAR(q.z, std::sin(angle / 2.0), 1e-12);
    CA_CHECK_NEAR(q.w, std::cos(angle / 2.0), 1e-12);
    CA_CHECK_NEAR(q.magnitude(), 1.0, 1e-12);
}

CA_TEST(quaternion_from_axis_angle_normalizes_axis) {
    // An unnormalized axis must give the same rotation as a normalized one.
    const Quaternion from_unit = Quaternion::from_axis_angle(
        Vector3{0.0, 0.0, 1.0}, 1.0);
    const Quaternion from_scaled = Quaternion::from_axis_angle(
        Vector3{0.0, 0.0, 5.0}, 1.0);

    CA_CHECK_NEAR(from_scaled.x, from_unit.x, 1e-12);
    CA_CHECK_NEAR(from_scaled.z, from_unit.z, 1e-12);
    CA_CHECK_NEAR(from_scaled.w, from_unit.w, 1e-12);

    // A degenerate axis yields identity rather than NaN.
    CA_CHECK(Quaternion::from_axis_angle(Vector3::zero(), 1.0)
             == Quaternion::identity());
}

CA_TEST(quaternion_slerp_endpoints_are_exact) {
    const Quaternion from = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0}, 0.0);
    const Quaternion to = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0}, 1.5);

    const Quaternion at_zero = Quaternion::slerp(from, to, 0.0);
    CA_CHECK_NEAR(at_zero.w, from.w, 1e-9);
    CA_CHECK_NEAR(at_zero.z, from.z, 1e-9);

    const Quaternion at_one = Quaternion::slerp(from, to, 1.0);
    CA_CHECK_NEAR(at_one.w, to.w, 1e-9);
    CA_CHECK_NEAR(at_one.z, to.z, 1e-9);
}

CA_TEST(quaternion_slerp_stays_on_unit_sphere) {
    // This is the property that keeps interpolated rotations valid rotations.
    const Quaternion from = Quaternion::from_axis_angle(Vector3{1.0, 0.0, 0.0}, 0.2);
    const Quaternion to = Quaternion::from_axis_angle(Vector3{0.0, 1.0, 0.3}, 2.4);

    for (int step = 0; step <= 10; ++step) {
        const double t = static_cast<double>(step) / 10.0;
        CA_CHECK_NEAR(Quaternion::slerp(from, to, t).magnitude(), 1.0, 1e-9);
    }
}

CA_TEST(quaternion_slerp_takes_the_shorter_arc) {
    // A quaternion and its negation represent the same rotation. Slerp must flip
    // the sign so it never travels the long way around.
    const Quaternion from = Quaternion::identity();
    const Quaternion negated_identity{0.0, 0.0, 0.0, -1.0};

    const Quaternion midpoint = Quaternion::slerp(from, negated_identity, 0.5);
    CA_CHECK_NEAR(std::abs(midpoint.w), 1.0, 1e-9);
    CA_CHECK_NEAR(midpoint.x, 0.0, 1e-9);
}

CA_TEST(quaternion_slerp_handles_nearly_parallel_inputs) {
    // Near-parallel inputs take the lerp fallback; the result must still be unit.
    const Quaternion from = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0}, 1.0);
    const Quaternion to = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0},
                                                     1.0 + 1e-7);
    const Quaternion midpoint = Quaternion::slerp(from, to, 0.5);
    CA_CHECK_NEAR(midpoint.magnitude(), 1.0, 1e-9);
}

CA_TEST(quaternion_multiplication_composes_rotations) {
    const double quarter = std::numbers::pi / 2.0;
    const Quaternion first = Quaternion::from_axis_angle(Vector3{0.0, 0.0, 1.0},
                                                        quarter);
    const Quaternion composed = first * first;
    const Quaternion expected = Quaternion::from_axis_angle(
        Vector3{0.0, 0.0, 1.0}, quarter * 2.0);

    CA_CHECK_NEAR(composed.z, expected.z, 1e-9);
    CA_CHECK_NEAR(composed.w, expected.w, 1e-9);
}

CA_TEST(quaternion_conjugate_inverts_unit_rotation) {
    const Quaternion q = Quaternion::from_axis_angle(Vector3{0.3, 0.5, 0.8}, 1.1);
    const Quaternion product = q * q.conjugate();
    CA_CHECK_NEAR(product.w, 1.0, 1e-9);
    CA_CHECK_NEAR(product.x, 0.0, 1e-9);
    CA_CHECK_NEAR(product.y, 0.0, 1e-9);
    CA_CHECK_NEAR(product.z, 0.0, 1e-9);
}

CA_TEST_MAIN()
