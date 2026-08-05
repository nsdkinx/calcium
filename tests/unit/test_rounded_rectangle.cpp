// ca::geometry::RoundedRectangle tests.
//
// The corner-curve tests are the conformance checks for the two documented
// curves (see rounded_rectangle.hpp): the circular corner is the kappa cubic
// against a true quarter circle, and the continuous corner is the G2
// two-cubic spec — exact edge tangencies, zero curvature at the junctions,
// exact crossing of the diagonal, monotone axes, and a bounded deviation from
// the n=5 superellipse on the outer half of the corner.

#include "calcium/geometry/path.hpp"
#include "calcium/geometry/rounded_rectangle.hpp"

#include <cmath>

#include "calcium_test.hpp"

using ca::geometry::CornerCurve;
using ca::geometry::Path;
using ca::geometry::PathVerb;
using ca::geometry::Point;
using ca::geometry::Rect;
using ca::geometry::RoundedRectangle;

namespace {

Point cubic_point(Point p0, Point p1, Point p2, Point p3, float t) {
    const float u = 1.0f - t;
    return {u * u * u * p0.x + 3.0f * u * u * t * p1.x
                + 3.0f * u * t * t * p2.x + t * t * t * p3.x,
            u * u * u * p0.y + 3.0f * u * u * t * p1.y
                + 3.0f * u * t * t * p2.y + t * t * t * p3.y};
}

// The first corner's cubics as control-point tuples, in path order. The path
// layout for a non-rect rounded rectangle is:
//   move, [corner: 1 or 2 cubics], line, [corner], line, [corner], line,
//   [corner], close
// and the top-leading corner is the first one.
struct CornerCubics {
    Point p0, p1, p2, p3;        // first cubic (or the single circular one)
    Point q0, q1, q2, q3;        // second cubic (continuous only)
    bool has_second;
};

CornerCubics first_corner(const Path& path) {
    CornerCubics result{};
    const auto& verbs = path.verbs();
    const auto& points = path.points();
    CA_CHECK(verbs[0] == PathVerb::move);
    result.p0 = points[0];  // the corner's start == the move point
    result.p1 = points[1];
    result.p2 = points[2];
    result.p3 = points[3];
    result.has_second = verbs[1] == PathVerb::cubic && verbs[2] == PathVerb::cubic;
    if (result.has_second) {
        result.q0 = points[3];  // shared diagonal point
        result.q1 = points[4];
        result.q2 = points[5];
        result.q3 = points[6];
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CA_TEST(rounded_rectangle_constructors) {
    const Rect bounds = Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f);
    const RoundedRectangle uniform =
        RoundedRectangle::uniform(bounds, 20.0f, CornerCurve::continuous);
    CA_CHECK(uniform.is_uniform());
    CA_CHECK(uniform.maximum_corner_radius() == 20.0f);
    CA_CHECK(uniform.corner_curve == CornerCurve::continuous);

    const RoundedRectangle capsule = RoundedRectangle::capsule(bounds);
    CA_CHECK(capsule.maximum_corner_radius() == 25.0f);  // min(w,h)/2

    const RoundedRectangle rect;
    CA_CHECK(rect.is_rect());
}

CA_TEST(rounded_rectangle_radii_clamp_proportionally) {
    const RoundedRectangle oversized =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  100.0f);
    const RoundedRectangle clamped = oversized.clamped_radii();
    CA_CHECK(clamped.maximum_corner_radius() == 25.0f);  // min(w,h)/2
    CA_CHECK(clamped.bounds == oversized.bounds);

    // Undersized radii pass through unchanged.
    const RoundedRectangle fine =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  12.0f);
    CA_CHECK(fine.clamped_radii() == fine);

    // A degenerate rect admits no radius at all.
    const RoundedRectangle degenerate =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 0.0f, 50.0f), 5.0f);
    CA_CHECK(degenerate.clamped_radii().is_rect());
}

// ---------------------------------------------------------------------------
// Path structure
// ---------------------------------------------------------------------------

CA_TEST(rounded_rectangle_rect_path_is_four_lines) {
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 10.0f, 20.0f),
                                  0.0f)
            .to_path();
    CA_CHECK(path.verb_count() == 5);  // move + 3 lines + close
    CA_CHECK(path.points()[0] == Point{0.0f, 0.0f});
    CA_CHECK(path.points()[1] == Point{10.0f, 0.0f});
    CA_CHECK(path.points()[2] == Point{10.0f, 20.0f});
    CA_CHECK(path.points()[3] == Point{0.0f, 20.0f});
}

CA_TEST(rounded_rectangle_circular_path_is_one_cubic_per_corner) {
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  20.0f, CornerCurve::circular)
            .to_path();
    // move + 4 corner cubics + 3 connector lines + close.
    CA_CHECK(path.verb_count() == 1 + 4 + 3 + 1);

    const CornerCubics corner = first_corner(path);
    CA_CHECK(!corner.has_second);
}

CA_TEST(rounded_rectangle_continuous_path_is_two_cubics_per_corner) {
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  20.0f, CornerCurve::continuous)
            .to_path();
    // move + 8 corner cubics + 3 connector lines + close.
    CA_CHECK(path.verb_count() == 1 + 8 + 3 + 1);

    const CornerCubics corner = first_corner(path);
    CA_CHECK(corner.has_second);
}

// ---------------------------------------------------------------------------
// Circular corner: the kappa cubic against the true quarter circle
// ---------------------------------------------------------------------------

CA_TEST(circular_corner_stays_on_the_arc) {
    constexpr float radius = 20.0f;
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  radius, CornerCurve::circular)
            .to_path();
    const CornerCubics corner = first_corner(path);

    // The corner starts on the top edge and ends on the left edge, exactly.
    CA_CHECK(corner.p0 == Point{radius, 0.0f});
    CA_CHECK(corner.p3 == Point{0.0f, radius});

    // Every point on the cubic is at distance ~radius from the corner apex:
    // the kappa fit's maximum radial error is 2.73e-4 * r.
    constexpr float tolerance = 5.0e-4f * radius;
    for (int step = 0; step <= 100; ++step) {
        const float t = static_cast<float>(step) / 100.0f;
        const Point point = cubic_point(corner.p0, corner.p1, corner.p2,
                                        corner.p3, t);
        const float distance = std::hypot(point.x, point.y);
        CA_CHECK(std::abs(distance - radius) <= tolerance);
    }
}

// ---------------------------------------------------------------------------
// Continuous corner: the G2 two-cubic spec
// ---------------------------------------------------------------------------

CA_TEST(continuous_corner_geometry_is_exact) {
    constexpr float radius = 20.0f;
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  radius, CornerCurve::continuous)
            .to_path();
    const CornerCubics corner = first_corner(path);

    // Endpoints sit exactly on the edges.
    CA_CHECK(corner.p0 == Point{radius, 0.0f});
    CA_CHECK(corner.q3 == Point{0.0f, radius});

    // The two cubics share the diagonal point exactly (G0).
    CA_CHECK(corner.p3 == corner.q0);

    // The 45-degree crossing sits exactly on the n=5 superellipse's diagonal
    // point: (m·r, m·r), m = 2^(-1/5) — the "bite" that reads as squircle.
    const float m = std::pow(0.5f, 0.2f);
    CA_CHECK_NEAR(corner.p3.x, m * radius, 1e-5);
    CA_CHECK_NEAR(corner.p3.y, m * radius, 1e-5);

    // Tangent at the edge junction is parallel to the edge (G1).
    CA_CHECK(corner.p0.x == corner.p1.x);  // vertical tangent at the top edge

    // Zero curvature at the edge junction (G2): P0, P1, P2 are collinear.
    CA_CHECK(corner.p0.x == corner.p1.x);
    CA_CHECK(corner.p1.x == corner.p2.x);

    // Tangent at the diagonal has slope -1 (G1 into the mirror).
    const float tangent_x = corner.p3.x - corner.p2.x;
    const float tangent_y = corner.p3.y - corner.p2.y;
    CA_CHECK_NEAR(tangent_x, -tangent_y, 1e-4);
    CA_CHECK_NEAR(tangent_x, -0.1294494367f * radius, 1e-4);
}

CA_TEST(continuous_corner_is_monotone) {
    constexpr float radius = 20.0f;
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  radius, CornerCurve::continuous)
            .to_path();
    const CornerCubics corner = first_corner(path);

    // Both cubics: x strictly decreases, y strictly increases — no wobble.
    for (int step = 0; step < 100; ++step) {
        const float t = static_cast<float>(step) / 100.0f;
        const float u = static_cast<float>(step + 1) / 100.0f;
        const Point a = cubic_point(corner.p0, corner.p1, corner.p2, corner.p3, t);
        const Point b = cubic_point(corner.p0, corner.p1, corner.p2, corner.p3, u);
        CA_CHECK(b.x < a.x);
        CA_CHECK(b.y > a.y);

        const Point c = cubic_point(corner.q0, corner.q1, corner.q2, corner.q3, t);
        const Point d = cubic_point(corner.q0, corner.q1, corner.q2, corner.q3, u);
        CA_CHECK(d.x < c.x);
        CA_CHECK(d.y > c.y);
    }
}

CA_TEST(continuous_corner_matches_superellipse_on_the_outer_half) {
    // For t >= 0.5 of each cubic (the visible middle of the corner), the
    // curve satisfies x^5 + y^5 = r^5 within 5e-2·r^5 — i.e. the points sit
    // within ~7e-3·r of the superellipse. Near the edge junctions the
    // superellipse has infinite curvature and is unrepresentable by cubics;
    // there the control points are the spec.
    constexpr float radius = 20.0f;
    const float radius5 = radius * radius * radius * radius * radius;
    const Path path =
        RoundedRectangle::uniform(Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
                                  radius, CornerCurve::continuous)
            .to_path();
    const CornerCubics corner = first_corner(path);

    const auto check_cubic = [&](Point p0, Point p1, Point p2, Point p3) {
        for (int step = 50; step <= 100; ++step) {
            const float t = static_cast<float>(step) / 100.0f;
            const Point point = cubic_point(p0, p1, p2, p3, t);
            const float residual = std::abs(std::pow(point.x, 5.0f)
                                            + std::pow(point.y, 5.0f) - radius5);
            CA_CHECK(residual <= 0.05f * radius5);
        }
    };
    check_cubic(corner.p0, corner.p1, corner.p2, corner.p3);
    check_cubic(corner.q0, corner.q1, corner.q2, corner.q3);
}

CA_TEST(rounded_rectangle_all_four_corners_are_present) {
    // A mixed-radius rect with one square corner. Radii stay under
    // min(w,h)/2 = 25 so nothing is clamped away.
    const float r_tl = 10.0f, r_tr = 20.0f, r_br = 24.0f, r_bl = 0.0f;
    const RoundedRectangle mixed{
        Rect::from_xywh(0.0f, 0.0f, 100.0f, 50.0f),
        r_tl, r_tr, r_br, r_bl,
        CornerCurve::continuous};
    const Path path = mixed.to_path();

    // The top-leading corner starts on the top edge at x = r_tl.
    const auto& points = path.points();
    CA_CHECK(points[0] == Point{r_tl, 0.0f});

    // move + 3 continuous corners × 2 cubics + 1 square corner × 1 line
    // + 3 connector lines + close.
    CA_CHECK(path.verb_count() == 1 + 3 * 2 + 1 + 3 + 1);

    // The path closes exactly and the last emitted point is the top-trailing
    // corner's end on the top edge (close itself emits no point).
    CA_CHECK(path.verbs().back() == PathVerb::close);
    CA_CHECK(points.back() == Point{100.0f - r_tr, 0.0f});
}

CA_TEST_MAIN()
