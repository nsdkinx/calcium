#pragma once

// 4x4 spatial transforms.
//
// CONVENTION (must not drift): row-vector, row-major, identical to
// `twell_transform3d` and `CATransform3D`.
//
//   * Points are ROW vectors, so a point is transformed as  p' = p * M.
//   * Translation lives in the FOURTH ROW: m41, m42, m43.
//   * Perspective lives in m34 (see `twell_transform3d_set_perspective`, which
//     writes -1/depth).
//   * Composition reads left to right: `a.concatenating(b)` applies a, then b.
//
// This matters more than it looks. A column-vector convention puts translation in
// the fourth COLUMN, and mixing the two silently transposes every transform: the
// result still renders, just wrongly, and no unit test on a translation-only
// matrix would catch it. Matching Twell exactly means the animation path can pass
// matrices through without a transpose step that could be forgotten.
//
// DECOMPOSITION is the reason this type exists rather than a bare float[16].
// Interpolating matrix components directly is wrong (see quaternion.hpp), so
// animation decomposes into translation / scale / skew / rotation / perspective,
// interpolates each in its natural space, and recomposes. This is what Core
// Animation does, and it is why rotating a card on iOS looks correct.

#include <cmath>
#include <optional>

#include "calcium/geometry/point.hpp"
#include "calcium/geometry/quaternion.hpp"

namespace ca::geometry {

struct Transform3D {
    // Row-major. Field names match twell_transform3d exactly.
    double m11 = 1.0, m12 = 0.0, m13 = 0.0, m14 = 0.0;
    double m21 = 0.0, m22 = 1.0, m23 = 0.0, m24 = 0.0;
    double m31 = 0.0, m32 = 0.0, m33 = 1.0, m34 = 0.0;
    double m41 = 0.0, m42 = 0.0, m43 = 0.0, m44 = 1.0;

    // --- Construction -------------------------------------------------------

    [[nodiscard]] static constexpr Transform3D identity() noexcept { return {}; }

    [[nodiscard]] static constexpr Transform3D make_translation(
        double tx, double ty, double tz) noexcept {
        Transform3D result;
        result.m41 = tx;
        result.m42 = ty;
        result.m43 = tz;
        return result;
    }

    [[nodiscard]] static constexpr Transform3D make_scale(
        double sx, double sy, double sz) noexcept {
        Transform3D result;
        result.m11 = sx;
        result.m22 = sy;
        result.m33 = sz;
        return result;
    }

    [[nodiscard]] static Transform3D make_rotation(const Quaternion& q) noexcept;

    [[nodiscard]] static Transform3D make_rotation_about_axis(
        Vector3 axis, double angle_radians) noexcept {
        return make_rotation(Quaternion::from_axis_angle(axis, angle_radians));
    }

    /// Perspective foreshortening. `eye_distance` is the distance from the
    /// viewer to the z=0 plane, in points; smaller values exaggerate depth.
    /// Writes -1/eye_distance into m34, matching Twell and CATransform3D.
    [[nodiscard]] static Transform3D make_perspective(double eye_distance) noexcept {
        Transform3D result;
        if (eye_distance > 1e-9) {
            result.m34 = -1.0 / eye_distance;
        }
        return result;
    }

    /// 2D shear. `sx` shifts x proportionally to y, and vice versa.
    [[nodiscard]] static constexpr Transform3D make_skew(double sx, double sy) noexcept {
        Transform3D result;
        result.m21 = sx;
        result.m12 = sy;
        return result;
    }

    // --- Composition --------------------------------------------------------

    /// Returns `*this * other`: applies this transform, then `other`.
    [[nodiscard]] constexpr Transform3D concatenating(
        const Transform3D& other) const noexcept {
        const Transform3D& a = *this;
        const Transform3D& b = other;
        Transform3D r;

        r.m11 = a.m11*b.m11 + a.m12*b.m21 + a.m13*b.m31 + a.m14*b.m41;
        r.m12 = a.m11*b.m12 + a.m12*b.m22 + a.m13*b.m32 + a.m14*b.m42;
        r.m13 = a.m11*b.m13 + a.m12*b.m23 + a.m13*b.m33 + a.m14*b.m43;
        r.m14 = a.m11*b.m14 + a.m12*b.m24 + a.m13*b.m34 + a.m14*b.m44;

        r.m21 = a.m21*b.m11 + a.m22*b.m21 + a.m23*b.m31 + a.m24*b.m41;
        r.m22 = a.m21*b.m12 + a.m22*b.m22 + a.m23*b.m32 + a.m24*b.m42;
        r.m23 = a.m21*b.m13 + a.m22*b.m23 + a.m23*b.m33 + a.m24*b.m43;
        r.m24 = a.m21*b.m14 + a.m22*b.m24 + a.m23*b.m34 + a.m24*b.m44;

        r.m31 = a.m31*b.m11 + a.m32*b.m21 + a.m33*b.m31 + a.m34*b.m41;
        r.m32 = a.m31*b.m12 + a.m32*b.m22 + a.m33*b.m32 + a.m34*b.m42;
        r.m33 = a.m31*b.m13 + a.m32*b.m23 + a.m33*b.m33 + a.m34*b.m43;
        r.m34 = a.m31*b.m14 + a.m32*b.m24 + a.m33*b.m34 + a.m34*b.m44;

        r.m41 = a.m41*b.m11 + a.m42*b.m21 + a.m43*b.m31 + a.m44*b.m41;
        r.m42 = a.m41*b.m12 + a.m42*b.m22 + a.m43*b.m32 + a.m44*b.m42;
        r.m43 = a.m41*b.m13 + a.m42*b.m23 + a.m43*b.m33 + a.m44*b.m43;
        r.m44 = a.m41*b.m14 + a.m42*b.m24 + a.m43*b.m34 + a.m44*b.m44;

        return r;
    }

    [[nodiscard]] constexpr Transform3D translated_by(
        double tx, double ty, double tz) const noexcept {
        return concatenating(make_translation(tx, ty, tz));
    }

    [[nodiscard]] constexpr Transform3D scaled_by(
        double sx, double sy, double sz) const noexcept {
        return concatenating(make_scale(sx, sy, sz));
    }

    [[nodiscard]] Transform3D rotated_by(const Quaternion& q) const noexcept {
        return concatenating(make_rotation(q));
    }

    // --- Queries ------------------------------------------------------------

    [[nodiscard]] constexpr bool is_identity() const noexcept {
        return *this == Transform3D{};
    }

    /// True when the transform has no perspective and no z component, so it can
    /// be flattened to a 2D affine transform. The compositor uses this to pick a
    /// cheaper path.
    [[nodiscard]] constexpr bool is_affine_2d() const noexcept {
        return m13 == 0.0 && m14 == 0.0
            && m23 == 0.0 && m24 == 0.0
            && m31 == 0.0 && m32 == 0.0 && m33 == 1.0 && m34 == 0.0
            && m43 == 0.0 && m44 == 1.0;
    }

    [[nodiscard]] constexpr double determinant() const noexcept {
        // Cofactor expansion along the first row.
        const double s0 = m33 * m44 - m34 * m43;
        const double s1 = m32 * m44 - m34 * m42;
        const double s2 = m32 * m43 - m33 * m42;
        const double s3 = m31 * m44 - m34 * m41;
        const double s4 = m31 * m43 - m33 * m41;
        const double s5 = m31 * m42 - m32 * m41;

        const double c0 = m22 * s0 - m23 * s1 + m24 * s2;
        const double c1 = m21 * s0 - m23 * s3 + m24 * s4;
        const double c2 = m21 * s1 - m22 * s3 + m24 * s5;
        const double c3 = m21 * s2 - m22 * s4 + m23 * s5;

        return m11 * c0 - m12 * c1 + m13 * c2 - m14 * c3;
    }

    [[nodiscard]] std::optional<Transform3D> inverted() const noexcept;

    /// Transforms a 2D point (z = 0), dividing through by w so perspective
    /// applies. Returns the projected point.
    [[nodiscard]] constexpr Point apply_to_point(Point point) const noexcept {
        const double x = static_cast<double>(point.x);
        const double y = static_cast<double>(point.y);

        const double tx = x * m11 + y * m21 + m41;
        const double ty = x * m12 + y * m22 + m42;
        const double tw = x * m14 + y * m24 + m44;

        if (tw != 0.0 && tw != 1.0) {
            return {static_cast<float>(tx / tw), static_cast<float>(ty / tw)};
        }
        return {static_cast<float>(tx), static_cast<float>(ty)};
    }

    [[nodiscard]] constexpr Vector3 apply_to_vector3(Vector3 v) const noexcept {
        const double tx = v.x * m11 + v.y * m21 + v.z * m31 + m41;
        const double ty = v.x * m12 + v.y * m22 + v.z * m32 + m42;
        const double tz = v.x * m13 + v.y * m23 + v.z * m33 + m43;
        const double tw = v.x * m14 + v.y * m24 + v.z * m34 + m44;

        if (tw != 0.0 && tw != 1.0) {
            return {tx / tw, ty / tw, tz / tw};
        }
        return {tx, ty, tz};
    }

    // --- Decomposition ------------------------------------------------------

    /// A transform separated into independently interpolable components.
    ///
    /// Recomposition order is fixed: scale, then skew, then rotate, then
    /// translate, with perspective applied last.
    struct DecomposedComponents {
        Vector3 translation = Vector3::zero();
        Vector3 scale = Vector3::one();
        /// {xy, xz, yz} shear factors.
        Vector3 skew = Vector3::zero();
        Quaternion rotation = Quaternion::identity();
        Vector4 perspective = Vector4::identity_perspective();

        /// Component-wise interpolation, with rotation slerped. This is the
        /// correct way to interpolate a transform.
        [[nodiscard]] static DecomposedComponents interpolate(
            const DecomposedComponents& from,
            const DecomposedComponents& to,
            double t) noexcept;
    };

    /// Separates into translation, scale, skew, rotation and perspective.
    ///
    /// Returns nullopt for a singular (non-invertible) matrix, where no
    /// meaningful decomposition exists. Follows the CSS Transforms /
    /// "Graphics Gems II" unmatrix algorithm.
    [[nodiscard]] std::optional<DecomposedComponents> decompose() const noexcept;

    [[nodiscard]] static Transform3D recompose(
        const DecomposedComponents& components) noexcept;

    /// Interpolates two transforms correctly: decompose both, interpolate the
    /// components, recompose. Falls back to component-wise matrix interpolation
    /// only when a matrix is singular and cannot be decomposed.
    [[nodiscard]] static Transform3D interpolate(const Transform3D& from,
                                                 const Transform3D& to,
                                                 double t) noexcept;

    [[nodiscard]] constexpr bool operator==(const Transform3D&) const noexcept = default;
};

} // namespace ca::geometry
