// ca::geometry::AffineTransform tests.

#include "calcium/geometry/affine_transform.hpp"

#include <numbers>

#include "calcium_test.hpp"

using ca::geometry::AffineTransform;
using ca::geometry::Point;
using ca::geometry::Rect;
using ca::geometry::Size;

// Compile-time construction (docs/03 section 5: constexpr all geometry).
static_assert(AffineTransform::identity().is_identity());
static_assert(AffineTransform::make_translation(3.0f, 4.0f)
              .apply_to_point({0.0f, 0.0f}) == Point{3.0f, 4.0f});
static_assert(AffineTransform::make_scale(2.0f, 3.0f)
              .apply_to_point({1.0f, 1.0f}) == Point{2.0f, 3.0f});

CA_TEST(affine_translation) {
    const auto t = AffineTransform::make_translation(10.0f, -5.0f);
    CA_CHECK(t.apply_to_point({1.0f, 2.0f}) == Point{11.0f, -3.0f});
    CA_CHECK(t.is_translation_only());
    CA_CHECK(!t.is_identity());
}

CA_TEST(affine_scale) {
    const auto s = AffineTransform::make_scale(2.0f, 3.0f);
    CA_CHECK(s.apply_to_point({4.0f, 5.0f}) == Point{8.0f, 15.0f});
    CA_CHECK(s.apply_to_size({10.0f, 20.0f}) == Size{20.0f, 60.0f});
    CA_CHECK(s.is_uniform_scale() == false);
    CA_CHECK(AffineTransform::make_scale(2.0f, 2.0f).is_uniform_scale());
}

CA_TEST(affine_rotation) {
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    const auto r = AffineTransform::make_rotation(quarter);

    // Clockwise (y grows down): (1, 0) rotates to (0, 1).
    CA_CHECK_NEAR(r.apply_to_point({1.0f, 0.0f}).x, 0.0, 1e-5);
    CA_CHECK_NEAR(r.apply_to_point({1.0f, 0.0f}).y, 1.0, 1e-5);
}

CA_TEST(affine_concatenating_reads_left_to_right) {
    // a.concatenating(b) applies a, then b — the Transform3D convention.
    const auto shift_then_scale = AffineTransform::make_translation(10.0f, 0.0f)
                                      .concatenating(AffineTransform::make_scale(2.0f, 2.0f));
    // (1,0) shifted to (11,0), then scaled to (22,0).
    CA_CHECK(shift_then_scale.apply_to_point({1.0f, 0.0f}) == Point{22.0f, 0.0f});

    const auto scale_then_shift = AffineTransform::make_scale(2.0f, 2.0f)
                                      .concatenating(AffineTransform::make_translation(10.0f, 0.0f));
    // (1,0) scaled to (2,0), then shifted to (12,0).
    CA_CHECK(scale_then_shift.apply_to_point({1.0f, 0.0f}) == Point{12.0f, 0.0f});
}

CA_TEST(affine_inversion_round_trips) {
    const auto transform = AffineTransform::make_translation(3.0f, -7.0f)
                               .concatenating(AffineTransform::make_rotation(0.4f))
                               .concatenating(AffineTransform::make_scale(2.0f, 1.5f));

    const auto inverted = transform.inverted();
    CA_CHECK(inverted.has_value());

    const Point sample{12.0f, -3.0f};
    const Point round_trip = inverted->apply_to_point(transform.apply_to_point(sample));
    CA_CHECK_NEAR(round_trip.x, sample.x, 1e-4);
    CA_CHECK_NEAR(round_trip.y, sample.y, 1e-4);
}

CA_TEST(affine_singular_has_no_inverse) {
    const auto singular = AffineTransform::make_scale(0.0f, 1.0f);
    CA_CHECK(!singular.inverted().has_value());
}

CA_TEST(affine_rect_mapping_is_the_corner_hull) {
    const Rect rect = Rect::from_xywh(10.0f, 20.0f, 100.0f, 50.0f);

    // A 45-degree rotation's rect hull is the diamond's bounds. Containment is
    // checked with an epsilon because `contains_point` is half-open and a
    // transformed corner lands exactly on the hull's max edges.
    const auto rotate = AffineTransform::make_rotation(std::numbers::pi_v<float> / 4.0f)
                            .concatenating(AffineTransform::make_translation(200.0f, 100.0f));
    const Rect hull = rotate.apply_to_rect(rect);

    const Point corners[4] = {{rect.min_x(), rect.min_y()},
                              {rect.max_x(), rect.min_y()},
                              {rect.max_x(), rect.max_y()},
                              {rect.min_x(), rect.max_y()}};
    for (const Point corner : corners) {
        const Point mapped = rotate.apply_to_point(corner);
        CA_CHECK(mapped.x >= hull.min_x() - 1e-3f && mapped.x <= hull.max_x() + 1e-3f);
        CA_CHECK(mapped.y >= hull.min_y() - 1e-3f && mapped.y <= hull.max_y() + 1e-3f);
    }

    // Width = w·cos(45°) + h·sin(45°): the spread of the diamond's corners.
    const float expected_width = 100.0f * 0.70710678f + 50.0f * 0.70710678f;
    CA_CHECK_NEAR(hull.width(), expected_width, 0.01);
    CA_CHECK_NEAR(hull.height(), expected_width, 0.01);
}

CA_TEST(affine_translation_only_rect_is_exact) {
    const Rect rect = Rect::from_xywh(1.0f, 2.0f, 3.0f, 4.0f);
    const Rect moved =
        AffineTransform::make_translation(10.0f, 20.0f).apply_to_rect(rect);
    CA_CHECK(moved == Rect::from_xywh(11.0f, 22.0f, 3.0f, 4.0f));
}

CA_TEST_MAIN()
