#pragma once

// Rounded rectangles.
//
// The workhorse shape of a UI framework: every card, button, sheet, and
// dialog is one of these. Two corner curves are specified:
//
//   circular    — quarter-circle arcs. One cubic per corner with the standard
//                 kappa fit (4/3·(√2−1)); maximum radial deviation from the
//                 true arc is 2.73e-4·r, far below one device pixel at any
//                 usable radius. This is the G1 curve.
//
//   continuous  — Apple's squircle corner. Matches the shape of
//                 `CALayerCornerCurveContinuous`, whose corner curve is the
//                 n=5 superellipse x⁵+y⁵ = r⁵. A superellipse cannot be
//                 represented by Bézier cubics near its edge junctions (it
//                 rises as (r−x)^(1/5), while a cubic with a vertical tangent
//                 rises as (r−x)^(1/2) — a structural mismatch no segment
//                 count fixes), so the curve is specified by its control
//                 points rather than approximated:
//
//                   P0 = (r, 0)
//                   P1 = (r, a·r)          a ≈ 0.57105
//                   P2 = (r, b·r)          b = 2m−1 ≈ 0.74110
//                   P3 = (m·r, m·r)        m = 2^(−1/5) ≈ 0.87055
//                   plus the mirror across the diagonal.
//
//                 P0, P1, P2 collinear gives zero curvature at the edge
//                 junction (G2), the mirror gives matching curvature at the
//                 45° diagonal (G2 everywhere), and m places the crossing of
//                 the diagonal exactly on the superellipse (the "bite" —
//                 0.183r from the corner apex — is what reads as squircle).
//                 The middle of the corner matches the superellipse to
//                 < 7e-3·r; near the junctions the superellipse's curvature
//                 is infinite and the control points are the spec.
//                 The constants were fit numerically (see the M0 commit
//                 history for the fitting script and the verification that
//                 the curve is monotone in both axes).
//
// `bounds` is the axis-aligned rectangle the corner radii are measured
// against; the path spans exactly `bounds`, so the curve is fully determined
// by the four radii, the curve choice, and the bounds.

#include "calcium/geometry/path.hpp"
#include "calcium/geometry/path_builder.hpp"
#include "calcium/geometry/point.hpp"
#include "calcium/geometry/rect.hpp"

namespace ca::geometry {

/// The shape of the corner curve (see the file comment).
enum class CornerCurve {
    circular,
    continuous,
};

/// A rectangle with independently-sized corners.
struct RoundedRectangle {
    Rect bounds;

    /// Radii are per-corner and normalized to the top-leading origin; the
    /// corner shape never depends on reading direction, so leading/trailing
    /// naming would be noise here.
    float top_leading_radius = 0.0f;
    float top_trailing_radius = 0.0f;
    float bottom_leading_radius = 0.0f;
    float bottom_trailing_radius = 0.0f;

    CornerCurve corner_curve = CornerCurve::continuous;

    /// All corners the same radius.
    [[nodiscard]] static constexpr RoundedRectangle uniform(
        Rect bounds, float radius,
        CornerCurve curve = CornerCurve::continuous) noexcept {
        return {bounds, radius, radius, radius, radius, curve};
    }

    /// A stadium: the largest uniform radius that fits (`min(w,h)/2`).
    [[nodiscard]] static constexpr RoundedRectangle capsule(Rect bounds) noexcept {
        const float radius = std::min(bounds.width(), bounds.height()) * 0.5f;
        return uniform(bounds, radius);
    }

    [[nodiscard]] constexpr bool is_rect() const noexcept {
        return top_leading_radius == 0.0f && top_trailing_radius == 0.0f
            && bottom_leading_radius == 0.0f && bottom_trailing_radius == 0.0f;
    }

    [[nodiscard]] constexpr bool is_uniform() const noexcept {
        return top_leading_radius == top_trailing_radius
            && top_leading_radius == bottom_leading_radius
            && top_leading_radius == bottom_trailing_radius;
    }

    [[nodiscard]] constexpr float maximum_corner_radius() const noexcept {
        return std::max({top_leading_radius, top_trailing_radius,
                         bottom_leading_radius, bottom_trailing_radius});
    }

    [[nodiscard]] constexpr RoundedRectangle with_corner_curve(
        CornerCurve curve) const noexcept {
        RoundedRectangle result = *this;
        result.corner_curve = curve;
        return result;
    }

    /// Radii scaled down so no corner's radius exceeds `min(w,h)/2` — the
    /// CSS-proportional rule: the largest radius is clamped, and the others
    /// shrink by the same factor so the shape stays consistent.
    /// `to_path()` applies this internally, so a caller can pass oversized
    /// radii without checking.
    [[nodiscard]] RoundedRectangle clamped_radii() const noexcept;

    /// The path of the outline, drawn clockwise from the top-leading corner
    /// (or from the top-leading edge for `is_rect()`).
    [[nodiscard]] Path to_path() const noexcept;

    [[nodiscard]] constexpr bool operator==(const RoundedRectangle&) const noexcept =
        default;
};

} // namespace ca::geometry
