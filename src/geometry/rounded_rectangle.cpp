#include "calcium/geometry/rounded_rectangle.hpp"

#include <algorithm>

namespace ca::geometry {

namespace {

// Corner-curve constants. The derivation and the fitting procedure are
// documented in the header; the values are verified by the M0 test suite
// (G2 at the junctions, monotone axes, deviation from the n=5 superellipse
// bounded in the visible region).
constexpr float continuous_split = 0.8705505632961241f;   // 2^(-1/5)
constexpr float continuous_control_a = 0.5710526200545827f;
constexpr float continuous_control_b = 0.7411011265922482f;  // 2·split − 1
constexpr float circular_kappa = 0.5522847498307936f;        // 4/3·(√2−1)

/// Appends the corner curve from the builder's current point, mapping the
/// canonical quadrant (r,0)→(0,r) onto the corner at `apex` with the two edge
/// directions `dir1`/`dir2` (unit vectors along the edges, pointing away from
/// the apex). `reversed` traverses the canonical curve backwards, which is how
/// the trailing corners connect their edges in perimeter order.
void append_corner(PathBuilder& builder, float radius, Point apex, Point dir1,
                   Point dir2, bool reversed, CornerCurve curve) {
    const auto map = [&](float u, float v) {
        return Point{apex.x + u * dir1.x + v * dir2.x,
                     apex.y + u * dir1.y + v * dir2.y};
    };

    if (radius <= 0.0f) {
        // A square corner: the path just turns the corner. Both the start and
        // the end coincide with the apex, so a single degenerate line keeps
        // the perimeter continuous.
        builder.line_to(map(0.0f, 0.0f));
        return;
    }

    if (curve == CornerCurve::continuous) {
        const Point p0 = map(radius, 0.0f);
        const Point p1 = map(radius, continuous_control_a * radius);
        const Point p2 = map(radius, continuous_control_b * radius);
        const Point p3 = map(continuous_split * radius, continuous_split * radius);
        const Point p4 = map(continuous_control_b * radius, radius);
        const Point p5 = map(continuous_control_a * radius, radius);
        const Point p6 = map(0.0f, radius);
        if (reversed) {
            builder.cubic_to(p5, p4, p3);
            builder.cubic_to(p2, p1, p0);
        } else {
            builder.cubic_to(p1, p2, p3);
            builder.cubic_to(p4, p5, p6);
        }
        return;
    }

    const Point p1 = map(radius, circular_kappa * radius);
    const Point p2 = map(circular_kappa * radius, radius);
    const Point p3 = map(0.0f, radius);
    if (reversed) {
        builder.cubic_to(p2, p1, map(radius, 0.0f));
    } else {
        builder.cubic_to(p1, p2, p3);
    }
}

} // namespace

RoundedRectangle RoundedRectangle::clamped_radii() const noexcept {
    const float max_allowed = std::min(bounds.width(), bounds.height()) * 0.5f;
    if (max_allowed <= 0.0f) {
        // Degenerate bounds: no corner can have a radius.
        return {bounds, 0.0f, 0.0f, 0.0f, 0.0f, corner_curve};
    }
    const float max_radius = maximum_corner_radius();
    if (max_radius <= max_allowed) {
        return *this;
    }
    const float factor = max_allowed / max_radius;
    return {bounds,
            top_leading_radius * factor,
            top_trailing_radius * factor,
            bottom_leading_radius * factor,
            bottom_trailing_radius * factor,
            corner_curve};
}

Path RoundedRectangle::to_path() const noexcept {
    const RoundedRectangle rect = clamped_radii();
    if (rect.is_rect()) {
        PathBuilder builder;
        builder.add_rect(rect.bounds);
        return builder.build();
    }

    const float x0 = rect.bounds.min_x();
    const float y0 = rect.bounds.min_y();
    const float x1 = rect.bounds.max_x();
    const float y1 = rect.bounds.max_y();
    const CornerCurve curve = rect.corner_curve;

    // Perimeter order, starting at the top-leading corner: down the left edge,
    // along the bottom, up the right edge, back across the top. `is_rect()` is
    // handled above, so every corner here has a positive radius.
    PathBuilder builder;
    builder.move_to({x0 + rect.top_leading_radius, y0});
    append_corner(builder, rect.top_leading_radius, {x0, y0}, {1.0f, 0.0f},
                  {0.0f, 1.0f}, /*reversed=*/false, curve);
    builder.line_to({x0, y1 - rect.bottom_leading_radius});
    append_corner(builder, rect.bottom_leading_radius, {x0, y1}, {1.0f, 0.0f},
                  {0.0f, -1.0f}, /*reversed=*/true, curve);
    builder.line_to({x1 - rect.bottom_trailing_radius, y1});
    append_corner(builder, rect.bottom_trailing_radius, {x1, y1}, {-1.0f, 0.0f},
                  {0.0f, -1.0f}, /*reversed=*/false, curve);
    builder.line_to({x1, y0 + rect.top_trailing_radius});
    append_corner(builder, rect.top_trailing_radius, {x1, y0}, {-1.0f, 0.0f},
                  {0.0f, 1.0f}, /*reversed=*/true, curve);
    builder.close_path();
    return builder.build();
}

} // namespace ca::geometry
