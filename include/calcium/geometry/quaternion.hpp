#pragma once

// Quaternions.
//
// Rotation is stored as a quaternion rather than Euler angles or a matrix so it
// can be interpolated correctly. Component-wise interpolation of two rotation
// matrices passes through matrices that are not rotations, producing visible
// shear and scale collapse (a 180-degree rotation lerps through a degenerate
// matrix). Slerp on a quaternion stays on the unit sphere.
//
// Layout and conventions match `twell_quaternion` exactly: {x, y, z, w}, doubles,
// w-last, right-handed. Twell already provides slerp and axis-angle construction;
// this type is the public face of the same representation, so conversion is a
// reinterpretation rather than a computation.

#include <cmath>

#include "calcium/geometry/point.hpp"

namespace ca::geometry {

struct Quaternion {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;

    [[nodiscard]] static constexpr Quaternion identity() noexcept {
        return {0.0, 0.0, 0.0, 1.0};
    }

    /// Rotation of `angle_radians` about `axis`. Axis need not be normalized.
    /// Matches `twell_quaternion_make_axis_angle`.
    [[nodiscard]] static Quaternion from_axis_angle(Vector3 axis,
                                                    double angle_radians) noexcept {
        const double length = axis.magnitude();
        if (length < 1e-9) {
            return identity();
        }
        const double half = angle_radians * 0.5;
        const double scale = std::sin(half) / length;
        return {axis.x * scale, axis.y * scale, axis.z * scale, std::cos(half)};
    }

    /// Matches `twell_quaternion_make_euler`'s composition order.
    [[nodiscard]] static Quaternion from_euler_angles(double pitch_radians,
                                                      double yaw_radians,
                                                      double roll_radians) noexcept {
        const double p = pitch_radians * 0.5;
        const double y_half = yaw_radians * 0.5;
        const double r = roll_radians * 0.5;

        const double cp = std::cos(p), sp = std::sin(p);
        const double cy = std::cos(y_half), sy = std::sin(y_half);
        const double cr = std::cos(r), sr = std::sin(r);

        return {sr * cp * cy - cr * sp * sy,
                cr * sp * cy + sr * cp * sy,
                cr * cp * sy - sr * sp * cy,
                cr * cp * cy + sr * sp * sy};
    }

    [[nodiscard]] double magnitude() const noexcept {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    [[nodiscard]] Quaternion normalized() const noexcept {
        const double length = magnitude();
        if (length < 1e-12) {
            return identity();
        }
        return {x / length, y / length, z / length, w / length};
    }

    [[nodiscard]] constexpr Quaternion conjugate() const noexcept {
        return {-x, -y, -z, w};
    }

    [[nodiscard]] constexpr double dot(const Quaternion& other) const noexcept {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    /// Hamilton product. `a * b` applies b's rotation, then a's.
    [[nodiscard]] constexpr Quaternion operator*(const Quaternion& o) const noexcept {
        return {w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z};
    }

    /// Spherical linear interpolation. Mirrors `twell_quaternion_slerp`,
    /// including the shortest-arc sign flip and the near-parallel lerp fallback.
    [[nodiscard]] static Quaternion slerp(Quaternion from, Quaternion to,
                                          double t) noexcept {
        double cosine = from.dot(to);

        // Negate one input if needed so we take the shorter of the two arcs.
        if (cosine < 0.0) {
            to = {-to.x, -to.y, -to.z, -to.w};
            cosine = -cosine;
        }

        // Nearly parallel: sin(theta) approaches zero, so slerp is numerically
        // unstable. Normalized lerp is indistinguishable here.
        if (cosine > 0.9995) {
            const Quaternion lerped{from.x + t * (to.x - from.x),
                                    from.y + t * (to.y - from.y),
                                    from.z + t * (to.z - from.z),
                                    from.w + t * (to.w - from.w)};
            return lerped.normalized();
        }

        const double theta_0 = std::acos(cosine);
        const double theta = theta_0 * t;
        const double sin_theta = std::sin(theta);
        const double sin_theta_0 = std::sin(theta_0);

        const double scale_from = std::cos(theta) - cosine * sin_theta / sin_theta_0;
        const double scale_to = sin_theta / sin_theta_0;

        return {scale_from * from.x + scale_to * to.x,
                scale_from * from.y + scale_to * to.y,
                scale_from * from.z + scale_to * to.z,
                scale_from * from.w + scale_to * to.w};
    }

    [[nodiscard]] constexpr bool operator==(const Quaternion&) const noexcept = default;
};

} // namespace ca::geometry
