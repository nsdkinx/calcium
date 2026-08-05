#include "calcium/geometry/transform_3d.hpp"

#include <algorithm>
#include <cmath>

namespace ca::geometry {
namespace {

constexpr double singularity_epsilon = 1e-12;

/// Row accessor for the upper-left 3x3, in row-vector convention.
[[nodiscard]] Vector3 basis_row(const Transform3D& m, int row) noexcept {
    switch (row) {
    case 0: return {m.m11, m.m12, m.m13};
    case 1: return {m.m21, m.m22, m.m23};
    default: return {m.m31, m.m32, m.m33};
    }
}

} // namespace

Transform3D Transform3D::make_rotation(const Quaternion& quaternion) noexcept {
    // Matches twell_transform3d_from_quaternion exactly, including the
    // transposition implied by the row-vector convention.
    const Quaternion q = quaternion.normalized();
    Transform3D m;

    const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    const double yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    const double wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

    m.m11 = 1.0 - (yy + zz);
    m.m12 = xy + wz;
    m.m13 = xz - wy;

    m.m21 = xy - wz;
    m.m22 = 1.0 - (xx + zz);
    m.m23 = yz + wx;

    m.m31 = xz + wy;
    m.m32 = yz - wx;
    m.m33 = 1.0 - (xx + yy);

    return m;
}

std::optional<Transform3D> Transform3D::inverted() const noexcept {
    // Full 4x4 inverse via cofactors. Needed for hit-testing through a
    // perspective-transformed layer: the point must be un-projected.
    const double a2323 = m33 * m44 - m34 * m43;
    const double a1323 = m32 * m44 - m34 * m42;
    const double a1223 = m32 * m43 - m33 * m42;
    const double a0323 = m31 * m44 - m34 * m41;
    const double a0223 = m31 * m43 - m33 * m41;
    const double a0123 = m31 * m42 - m32 * m41;
    const double a2313 = m23 * m44 - m24 * m43;
    const double a1313 = m22 * m44 - m24 * m42;
    const double a1213 = m22 * m43 - m23 * m42;
    const double a2312 = m23 * m34 - m24 * m33;
    const double a1312 = m22 * m34 - m24 * m32;
    const double a1212 = m22 * m33 - m23 * m32;
    const double a0313 = m21 * m44 - m24 * m41;
    const double a0213 = m21 * m43 - m23 * m41;
    const double a0312 = m21 * m34 - m24 * m31;
    const double a0212 = m21 * m33 - m23 * m31;
    const double a0113 = m21 * m42 - m22 * m41;
    const double a0112 = m21 * m32 - m22 * m31;

    double det = m11 * (m22 * a2323 - m23 * a1323 + m24 * a1223)
               - m12 * (m21 * a2323 - m23 * a0323 + m24 * a0223)
               + m13 * (m21 * a1323 - m22 * a0323 + m24 * a0123)
               - m14 * (m21 * a1223 - m22 * a0223 + m23 * a0123);

    if (std::abs(det) < singularity_epsilon) {
        return std::nullopt;
    }
    det = 1.0 / det;

    Transform3D r;
    r.m11 =  det * (m22 * a2323 - m23 * a1323 + m24 * a1223);
    r.m12 = -det * (m12 * a2323 - m13 * a1323 + m14 * a1223);
    r.m13 =  det * (m12 * a2313 - m13 * a1313 + m14 * a1213);
    r.m14 = -det * (m12 * a2312 - m13 * a1312 + m14 * a1212);
    r.m21 = -det * (m21 * a2323 - m23 * a0323 + m24 * a0223);
    r.m22 =  det * (m11 * a2323 - m13 * a0323 + m14 * a0223);
    r.m23 = -det * (m11 * a2313 - m13 * a0313 + m14 * a0213);
    r.m24 =  det * (m11 * a2312 - m13 * a0312 + m14 * a0212);
    r.m31 =  det * (m21 * a1323 - m22 * a0323 + m24 * a0123);
    r.m32 = -det * (m11 * a1323 - m12 * a0323 + m14 * a0123);
    r.m33 =  det * (m11 * a1313 - m12 * a0313 + m14 * a0113);
    r.m34 = -det * (m11 * a1312 - m12 * a0312 + m14 * a0112);
    r.m41 = -det * (m21 * a1223 - m22 * a0223 + m23 * a0123);
    r.m42 =  det * (m11 * a1223 - m12 * a0223 + m13 * a0123);
    r.m43 = -det * (m11 * a1213 - m12 * a0213 + m13 * a0113);
    r.m44 =  det * (m11 * a1212 - m12 * a0212 + m13 * a0112);
    return r;
}

std::optional<Transform3D::DecomposedComponents>
Transform3D::decompose() const noexcept {
    // "unmatrix" from Graphics Gems II, as specified by CSS Transforms. Adapted
    // to the row-vector convention: translation is row 4, and the perspective
    // column is {m14, m24, m34, m44}.
    if (std::abs(m44) < singularity_epsilon) {
        return std::nullopt;
    }

    // Normalize so m44 == 1.
    Transform3D local = *this;
    const double inverse_m44 = 1.0 / m44;
    local.m11 *= inverse_m44; local.m12 *= inverse_m44;
    local.m13 *= inverse_m44; local.m14 *= inverse_m44;
    local.m21 *= inverse_m44; local.m22 *= inverse_m44;
    local.m23 *= inverse_m44; local.m24 *= inverse_m44;
    local.m31 *= inverse_m44; local.m32 *= inverse_m44;
    local.m33 *= inverse_m44; local.m34 *= inverse_m44;
    local.m41 *= inverse_m44; local.m42 *= inverse_m44;
    local.m43 *= inverse_m44; local.m44 = 1.0;

    // The perspective-bearing matrix must be invertible for the split to exist.
    Transform3D perspective_basis = local;
    perspective_basis.m14 = 0.0;
    perspective_basis.m24 = 0.0;
    perspective_basis.m34 = 0.0;
    perspective_basis.m44 = 1.0;
    if (std::abs(perspective_basis.determinant()) < singularity_epsilon) {
        return std::nullopt;
    }

    DecomposedComponents components;

    // --- Perspective --------------------------------------------------------
    if (std::abs(local.m14) > singularity_epsilon ||
        std::abs(local.m24) > singularity_epsilon ||
        std::abs(local.m34) > singularity_epsilon) {
        // Right-hand side is the perspective column.
        const Vector4 rhs{local.m14, local.m24, local.m34, local.m44};

        const auto inverse = perspective_basis.inverted();
        if (!inverse.has_value()) {
            return std::nullopt;
        }
        // The matrix factors as M = A * P, where A is the affine basis (last
        // column {0,0,0,1}) and P is identity with last column p. Expanding the
        // product, column 4 of M is A * p with p treated as a COLUMN vector:
        //
        //     M[i][4] = sum_k A[i][k] * p[k]
        //
        // so p = inverse(A) * rhs, which reads along the ROWS of the inverse.
        // Reading down its columns instead transposes the solve, which is
        // invisible for a bare perspective matrix (identity basis is symmetric)
        // and wrong the moment perspective is combined with translation.
        const Transform3D& n = *inverse;
        components.perspective = Vector4{
            n.m11 * rhs.x + n.m12 * rhs.y + n.m13 * rhs.z + n.m14 * rhs.w,
            n.m21 * rhs.x + n.m22 * rhs.y + n.m23 * rhs.z + n.m24 * rhs.w,
            n.m31 * rhs.x + n.m32 * rhs.y + n.m33 * rhs.z + n.m34 * rhs.w,
            n.m41 * rhs.x + n.m42 * rhs.y + n.m43 * rhs.z + n.m44 * rhs.w};

        local.m14 = 0.0;
        local.m24 = 0.0;
        local.m34 = 0.0;
        local.m44 = 1.0;
    } else {
        components.perspective = Vector4::identity_perspective();
    }

    // --- Translation (row 4) ------------------------------------------------
    components.translation = Vector3{local.m41, local.m42, local.m43};
    local.m41 = 0.0;
    local.m42 = 0.0;
    local.m43 = 0.0;

    // --- Scale and skew from the 3x3 basis rows -----------------------------
    Vector3 row0 = basis_row(local, 0);
    Vector3 row1 = basis_row(local, 1);
    Vector3 row2 = basis_row(local, 2);

    // Gram-Schmidt: extract each axis length, then remove the shear it carries.
    components.scale.x = row0.magnitude();
    if (components.scale.x > singularity_epsilon) {
        row0 = row0 * (1.0 / components.scale.x);
    }

    components.skew.x = row0.dot(row1);                 // xy shear
    row1 = row1 - row0 * components.skew.x;

    components.scale.y = row1.magnitude();
    if (components.scale.y > singularity_epsilon) {
        row1 = row1 * (1.0 / components.scale.y);
        components.skew.x /= components.scale.y;
    }

    components.skew.y = row0.dot(row2);                 // xz shear
    row2 = row2 - row0 * components.skew.y;
    components.skew.z = row1.dot(row2);                 // yz shear
    row2 = row2 - row1 * components.skew.z;

    components.scale.z = row2.magnitude();
    if (components.scale.z > singularity_epsilon) {
        row2 = row2 * (1.0 / components.scale.z);
        components.skew.y /= components.scale.z;
        components.skew.z /= components.scale.z;
    }

    // A left-handed basis means the transform includes a reflection. Fold the
    // sign into the scale so the remaining basis is a pure rotation.
    if (row0.dot(row1.cross(row2)) < 0.0) {
        components.scale.x = -components.scale.x;
        components.scale.y = -components.scale.y;
        components.scale.z = -components.scale.z;
        row0 = -row0;
        row1 = -row1;
        row2 = -row2;
    }

    // --- Rotation: orthonormal basis to quaternion ---------------------------
    // Row-vector convention, so the basis rows are the transposed columns of the
    // textbook (column-vector) form; the off-diagonal signs follow accordingly
    // and match make_rotation()/twell_transform3d_from_quaternion.
    const double trace = row0.x + row1.y + row2.z;
    Quaternion rotation;

    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        rotation.w = 0.25 * s;
        rotation.x = (row1.z - row2.y) / s;
        rotation.y = (row2.x - row0.z) / s;
        rotation.z = (row0.y - row1.x) / s;
    } else if (row0.x > row1.y && row0.x > row2.z) {
        const double s = std::sqrt(1.0 + row0.x - row1.y - row2.z) * 2.0;
        rotation.w = (row1.z - row2.y) / s;
        rotation.x = 0.25 * s;
        rotation.y = (row0.y + row1.x) / s;
        rotation.z = (row0.z + row2.x) / s;
    } else if (row1.y > row2.z) {
        const double s = std::sqrt(1.0 + row1.y - row0.x - row2.z) * 2.0;
        rotation.w = (row2.x - row0.z) / s;
        rotation.x = (row0.y + row1.x) / s;
        rotation.y = 0.25 * s;
        rotation.z = (row1.z + row2.y) / s;
    } else {
        const double s = std::sqrt(1.0 + row2.z - row0.x - row1.y) * 2.0;
        rotation.w = (row0.y - row1.x) / s;
        rotation.x = (row0.z + row2.x) / s;
        rotation.y = (row1.z + row2.y) / s;
        rotation.z = 0.25 * s;
    }

    components.rotation = rotation.normalized();
    return components;
}

Transform3D Transform3D::recompose(const DecomposedComponents& c) noexcept {
    Transform3D result = Transform3D::identity();

    // Perspective first, so it ends up applied last in the row-vector chain.
    if (c.perspective != Vector4::identity_perspective()) {
        result.m14 = c.perspective.x;
        result.m24 = c.perspective.y;
        result.m34 = c.perspective.z;
        result.m44 = c.perspective.w;
    }

    // Translation.
    result = Transform3D::make_translation(c.translation.x, c.translation.y,
                                           c.translation.z)
                 .concatenating(result);

    // Rotation.
    result = Transform3D::make_rotation(c.rotation).concatenating(result);

    // Skew, in reverse of extraction order: yz, then xz, then xy.
    if (c.skew.z != 0.0) {
        Transform3D shear = Transform3D::identity();
        shear.m32 = c.skew.z;
        result = shear.concatenating(result);
    }
    if (c.skew.y != 0.0) {
        Transform3D shear = Transform3D::identity();
        shear.m31 = c.skew.y;
        result = shear.concatenating(result);
    }
    if (c.skew.x != 0.0) {
        Transform3D shear = Transform3D::identity();
        shear.m21 = c.skew.x;
        result = shear.concatenating(result);
    }

    // Scale.
    result = Transform3D::make_scale(c.scale.x, c.scale.y, c.scale.z)
                 .concatenating(result);

    return result;
}

Transform3D::DecomposedComponents
Transform3D::DecomposedComponents::interpolate(const DecomposedComponents& from,
                                               const DecomposedComponents& to,
                                               double t) noexcept {
    DecomposedComponents result;
    const auto lerp = [t](double a, double b) { return a + (b - a) * t; };

    result.translation = {lerp(from.translation.x, to.translation.x),
                          lerp(from.translation.y, to.translation.y),
                          lerp(from.translation.z, to.translation.z)};
    result.scale = {lerp(from.scale.x, to.scale.x),
                    lerp(from.scale.y, to.scale.y),
                    lerp(from.scale.z, to.scale.z)};
    result.skew = {lerp(from.skew.x, to.skew.x),
                   lerp(from.skew.y, to.skew.y),
                   lerp(from.skew.z, to.skew.z)};
    result.perspective = {lerp(from.perspective.x, to.perspective.x),
                          lerp(from.perspective.y, to.perspective.y),
                          lerp(from.perspective.z, to.perspective.z),
                          lerp(from.perspective.w, to.perspective.w)};

    // Rotation is slerped, not lerped. This is the whole point of decomposing.
    result.rotation = Quaternion::slerp(from.rotation, to.rotation, t);
    return result;
}

Transform3D Transform3D::interpolate(const Transform3D& from,
                                     const Transform3D& to, double t) noexcept {
    const auto from_components = from.decompose();
    const auto to_components = to.decompose();

    if (from_components.has_value() && to_components.has_value()) {
        return recompose(DecomposedComponents::interpolate(
            *from_components, *to_components, t));
    }

    // One of the matrices is singular, so there is no meaningful decomposition.
    // Component-wise interpolation is not correct, but it is defined and
    // continuous, which is better than failing mid-animation.
    Transform3D result;
    const double* a = &from.m11;
    const double* b = &to.m11;
    double* out = &result.m11;
    for (int index = 0; index < 16; ++index) {
        out[index] = a[index] + (b[index] - a[index]) * t;
    }
    return result;
}

} // namespace ca::geometry
