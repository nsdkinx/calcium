#pragma once

// 2D affine transforms.
//
// CONVENTION (must not drift): row-vector, matching the 2D portion of
// `Transform3D` and `CATransform3D`. A point is transformed as p' = p * M:
//
//   M = | a  b  0 |      x' = a·x + c·y + tx
//       | c  d  0 |      y' = b·x + d·y + ty
//       | tx ty 1 |
//
// Field names are CSS-style (a, b, c, d, tx, ty) rather than m11-style
// because 2D affine code reads them constantly; the mapping to Transform3D is
// exact: a=m11, b=m12, c=m21, d=m22, tx=m41, ty=m42.
//
// Composition reads left to right, like Transform3D: `a.concatenating(b)`
// applies a, then b.
//
// All constexpr: geometry should be compile-time constructible (docs/03 §5).

#include <algorithm>
#include <cmath>
#include <optional>

#include "calcium/geometry/point.hpp"
#include "calcium/geometry/rect.hpp"

namespace ca::geometry {

struct AffineTransform {
    float a = 1.0f, b = 0.0f;
    float c = 0.0f, d = 1.0f;
    float tx = 0.0f, ty = 0.0f;

    // --- Construction -------------------------------------------------------

    [[nodiscard]] static constexpr AffineTransform identity() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr AffineTransform make_translation(
        float x, float y) noexcept {
        AffineTransform result;
        result.tx = x;
        result.ty = y;
        return result;
    }

    [[nodiscard]] static constexpr AffineTransform make_scale(
        float sx, float sy) noexcept {
        AffineTransform result;
        result.a = sx;
        result.d = sy;
        return result;
    }

    /// Rotation about the origin, radians, clockwise (y grows down).
    /// Not constexpr: std::cos/std::sin are not constexpr in MSVC's STL, and
    /// geometry stays consistent with Transform3D::make_rotation.
    [[nodiscard]] static AffineTransform make_rotation(float radians) noexcept {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return {cosine, sine, -sine, cosine, 0.0f, 0.0f};
    }

    /// Shear: `sx` shifts x proportionally to y, and vice versa.
    [[nodiscard]] static constexpr AffineTransform make_skew(
        float sx, float sy) noexcept {
        AffineTransform result;
        result.c = sx;
        result.b = sy;
        return result;
    }

    // --- Composition --------------------------------------------------------

    /// Returns `*this * other`: applies this transform, then `other`.
    [[nodiscard]] constexpr AffineTransform concatenating(
        const AffineTransform& o) const noexcept {
        return {a * o.a + b * o.c, a * o.b + b * o.d,
                c * o.a + d * o.c, c * o.b + d * o.d,
                tx * o.a + ty * o.c + o.tx,
                tx * o.b + ty * o.d + o.ty};
    }

    [[nodiscard]] constexpr AffineTransform translated_by(
        float x, float y) const noexcept {
        return concatenating(make_translation(x, y));
    }

    [[nodiscard]] constexpr AffineTransform scaled_by(
        float sx, float sy) const noexcept {
        return concatenating(make_scale(sx, sy));
    }

    [[nodiscard]] constexpr AffineTransform rotated_by(float radians) const noexcept {
        return concatenating(make_rotation(radians));
    }

    // --- Queries ------------------------------------------------------------

    [[nodiscard]] constexpr bool is_identity() const noexcept {
        return *this == AffineTransform{};
    }

    [[nodiscard]] constexpr bool is_translation_only() const noexcept {
        return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f;
    }

    /// Uniform scale without rotation or skew — the cheapest transform class.
    [[nodiscard]] constexpr bool is_uniform_scale() const noexcept {
        return b == 0.0f && c == 0.0f && a == d && a != 0.0f;
    }

    [[nodiscard]] constexpr float determinant() const noexcept {
        return a * d - b * c;
    }

    /// The inverse, or nullopt for a singular (non-invertible) transform.
    [[nodiscard]] constexpr std::optional<AffineTransform> inverted() const noexcept {
        const float det = determinant();
        if (det == 0.0f) {
            return std::nullopt;
        }
        const float inv_det = 1.0f / det;
        return AffineTransform{
            d * inv_det, -b * inv_det,
            -c * inv_det, a * inv_det,
            (c * ty - d * tx) * inv_det,
            (b * tx - a * ty) * inv_det};
    }

    // --- Application --------------------------------------------------------

    [[nodiscard]] constexpr Point apply_to_point(Point point) const noexcept {
        return {a * point.x + c * point.y + tx,
                b * point.x + d * point.y + ty};
    }

    /// Transforms an offset (no translation component).
    [[nodiscard]] constexpr Point apply_to_offset(Point offset) const noexcept {
        return {a * offset.x + c * offset.y,
                b * offset.x + d * offset.y};
    }

    /// Transforms a size as its two corner offsets (scales and skews, never
    /// translates).
    [[nodiscard]] constexpr Size apply_to_size(Size size) const noexcept {
        const auto abs = [](float value) constexpr noexcept {
            return value < 0.0f ? -value : value;
        };
        return {abs(a * size.width + c * size.height),
                abs(b * size.width + d * size.height)};
    }

    /// The bounds of the transformed rect — the axis-aligned hull of its four
    /// transformed corners. Not the tight bounds of a transformed shape; the
    /// compositor's culling uses this because it is exact for axis-aligned
    /// rects and conservative for everything else.
    [[nodiscard]] constexpr Rect apply_to_rect(Rect rect) const noexcept {
        const Point p0 = apply_to_point({rect.min_x(), rect.min_y()});
        const Point p1 = apply_to_point({rect.max_x(), rect.min_y()});
        const Point p2 = apply_to_point({rect.max_x(), rect.max_y()});
        const Point p3 = apply_to_point({rect.min_x(), rect.max_y()});

        const float left = std::min({p0.x, p1.x, p2.x, p3.x});
        const float top = std::min({p0.y, p1.y, p2.y, p3.y});
        const float right = std::max({p0.x, p1.x, p2.x, p3.x});
        const float bottom = std::max({p0.y, p1.y, p2.y, p3.y});
        return Rect::from_edges(left, top, right, bottom);
    }

    [[nodiscard]] constexpr bool operator==(const AffineTransform&) const noexcept =
        default;
};

} // namespace ca::geometry
