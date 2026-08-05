#pragma once

// Points, sizes, vectors.
//
// All constexpr: geometry should be compile-time constructible so layout
// constants cost nothing at runtime.
//
// Coordinates are `float` at the geometry level (they feed the GPU, where the
// extra precision is discarded anyway), but `Vector3`/`Vector4` and everything
// in transform_3d.hpp are `double` to match Twell's representation exactly. The
// conversion happens once, explicitly, at the boundary.

#include <cmath>
#include <compare>

namespace ca::geometry {

/// A location in a 2D coordinate space, in points.
struct Point {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] static constexpr Point zero() noexcept { return {}; }

    [[nodiscard]] constexpr Point operator+(Point other) const noexcept {
        return {x + other.x, y + other.y};
    }
    [[nodiscard]] constexpr Point operator-(Point other) const noexcept {
        return {x - other.x, y - other.y};
    }
    [[nodiscard]] constexpr Point operator*(float scale) const noexcept {
        return {x * scale, y * scale};
    }
    [[nodiscard]] constexpr Point operator/(float divisor) const noexcept {
        return divisor != 0.0f ? Point{x / divisor, y / divisor} : Point{};
    }
    [[nodiscard]] constexpr Point operator-() const noexcept { return {-x, -y}; }

    constexpr Point& operator+=(Point other) noexcept {
        x += other.x; y += other.y; return *this;
    }
    constexpr Point& operator-=(Point other) noexcept {
        x -= other.x; y -= other.y; return *this;
    }

    [[nodiscard]] float distance_to(Point other) const noexcept {
        const float dx = other.x - x;
        const float dy = other.y - y;
        return std::sqrt(dx * dx + dy * dy);
    }

    [[nodiscard]] constexpr float squared_distance_to(Point other) const noexcept {
        const float dx = other.x - x;
        const float dy = other.y - y;
        return dx * dx + dy * dy;
    }

    [[nodiscard]] float magnitude() const noexcept {
        return std::sqrt(x * x + y * y);
    }

    [[nodiscard]] constexpr bool operator==(const Point&) const noexcept = default;
};

/// A 2D extent. Negative dimensions are not meaningful and callers should not
/// construct them; `is_empty()` treats them as empty.
struct Size {
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] static constexpr Size zero() noexcept { return {}; }
    [[nodiscard]] static constexpr Size square(float side) noexcept {
        return {side, side};
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return width <= 0.0f || height <= 0.0f;
    }
    [[nodiscard]] constexpr float area() const noexcept { return width * height; }
    [[nodiscard]] constexpr float aspect_ratio() const noexcept {
        return height != 0.0f ? width / height : 0.0f;
    }

    [[nodiscard]] constexpr Size operator+(Size other) const noexcept {
        return {width + other.width, height + other.height};
    }
    [[nodiscard]] constexpr Size operator-(Size other) const noexcept {
        return {width - other.width, height - other.height};
    }
    [[nodiscard]] constexpr Size operator*(float scale) const noexcept {
        return {width * scale, height * scale};
    }

    [[nodiscard]] constexpr bool operator==(const Size&) const noexcept = default;
};

/// A 3D vector in Twell's representation (`double`, matching `twell_vector3`).
struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] static constexpr Vector3 zero() noexcept { return {}; }
    [[nodiscard]] static constexpr Vector3 one() noexcept { return {1.0, 1.0, 1.0}; }

    [[nodiscard]] constexpr Vector3 operator+(Vector3 other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }
    [[nodiscard]] constexpr Vector3 operator-(Vector3 other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }
    [[nodiscard]] constexpr Vector3 operator*(double scale) const noexcept {
        return {x * scale, y * scale, z * scale};
    }
    [[nodiscard]] constexpr Vector3 operator-() const noexcept {
        return {-x, -y, -z};
    }

    [[nodiscard]] constexpr double dot(Vector3 other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }

    [[nodiscard]] constexpr Vector3 cross(Vector3 other) const noexcept {
        return {y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x};
    }

    [[nodiscard]] double magnitude() const noexcept {
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] Vector3 normalized() const noexcept {
        const double length = magnitude();
        return length > 1e-12 ? Vector3{x / length, y / length, z / length}
                              : Vector3{};
    }

    [[nodiscard]] constexpr bool operator==(const Vector3&) const noexcept = default;
};

/// A 4D vector. Used for the perspective row produced by transform decomposition.
struct Vector4 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;

    [[nodiscard]] static constexpr Vector4 zero() noexcept { return {}; }
    [[nodiscard]] static constexpr Vector4 identity_perspective() noexcept {
        return {0.0, 0.0, 0.0, 1.0};
    }

    [[nodiscard]] constexpr bool operator==(const Vector4&) const noexcept = default;
};

/// Explicit narrowing at the float/double boundary, so the conversion is always
/// visible at the call site rather than implicit.
[[nodiscard]] constexpr Point to_point(Vector3 vector) noexcept {
    return {static_cast<float>(vector.x), static_cast<float>(vector.y)};
}

[[nodiscard]] constexpr Vector3 to_vector3(Point point, double z = 0.0) noexcept {
    return {static_cast<double>(point.x), static_cast<double>(point.y), z};
}

} // namespace ca::geometry
