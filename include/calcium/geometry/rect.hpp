#pragma once

// Rectangles and edge insets.

#include <algorithm>

#include "calcium/geometry/point.hpp"

namespace ca::geometry {

/// Per-edge spacing. `leading`/`trailing` rather than `left`/`right` so
/// right-to-left layouts resolve correctly without a second type.
struct EdgeInsets {
    float top = 0.0f;
    float leading = 0.0f;
    float bottom = 0.0f;
    float trailing = 0.0f;

    [[nodiscard]] static constexpr EdgeInsets zero() noexcept { return {}; }

    [[nodiscard]] static constexpr EdgeInsets all(float amount) noexcept {
        return {amount, amount, amount, amount};
    }
    [[nodiscard]] static constexpr EdgeInsets symmetric(float vertical,
                                                        float horizontal) noexcept {
        return {vertical, horizontal, vertical, horizontal};
    }
    [[nodiscard]] static constexpr EdgeInsets horizontal(float amount) noexcept {
        return {0.0f, amount, 0.0f, amount};
    }
    [[nodiscard]] static constexpr EdgeInsets vertical(float amount) noexcept {
        return {amount, 0.0f, amount, 0.0f};
    }

    [[nodiscard]] constexpr float total_horizontal() const noexcept {
        return leading + trailing;
    }
    [[nodiscard]] constexpr float total_vertical() const noexcept {
        return top + bottom;
    }

    [[nodiscard]] constexpr EdgeInsets operator+(EdgeInsets other) const noexcept {
        return {top + other.top, leading + other.leading,
                bottom + other.bottom, trailing + other.trailing};
    }

    [[nodiscard]] constexpr bool operator==(const EdgeInsets&) const noexcept = default;
};

/// An axis-aligned rectangle, origin at its top-leading corner (y grows down).
struct Rect {
    Point origin;
    Size size;

    [[nodiscard]] static constexpr Rect zero() noexcept { return {}; }

    [[nodiscard]] static constexpr Rect from_origin_and_size(Point origin,
                                                             Size size) noexcept {
        return {origin, size};
    }

    [[nodiscard]] static constexpr Rect from_xywh(float x, float y,
                                                  float width, float height) noexcept {
        return {{x, y}, {width, height}};
    }

    [[nodiscard]] static constexpr Rect from_edges(float left, float top,
                                                   float right, float bottom) noexcept {
        return {{left, top}, {right - left, bottom - top}};
    }

    [[nodiscard]] static constexpr Rect from_center_and_size(Point center,
                                                             Size size) noexcept {
        return {{center.x - size.width * 0.5f, center.y - size.height * 0.5f}, size};
    }

    [[nodiscard]] constexpr float min_x() const noexcept { return origin.x; }
    [[nodiscard]] constexpr float min_y() const noexcept { return origin.y; }
    [[nodiscard]] constexpr float max_x() const noexcept {
        return origin.x + size.width;
    }
    [[nodiscard]] constexpr float max_y() const noexcept {
        return origin.y + size.height;
    }
    [[nodiscard]] constexpr float mid_x() const noexcept {
        return origin.x + size.width * 0.5f;
    }
    [[nodiscard]] constexpr float mid_y() const noexcept {
        return origin.y + size.height * 0.5f;
    }
    [[nodiscard]] constexpr float width() const noexcept { return size.width; }
    [[nodiscard]] constexpr float height() const noexcept { return size.height; }

    [[nodiscard]] constexpr Point center() const noexcept {
        return {mid_x(), mid_y()};
    }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return size.is_empty(); }

    [[nodiscard]] constexpr bool contains_point(Point point) const noexcept {
        return point.x >= min_x() && point.x < max_x()
            && point.y >= min_y() && point.y < max_y();
    }

    [[nodiscard]] constexpr bool contains_rect(const Rect& other) const noexcept {
        return other.min_x() >= min_x() && other.max_x() <= max_x()
            && other.min_y() >= min_y() && other.max_y() <= max_y();
    }

    [[nodiscard]] constexpr bool intersects(const Rect& other) const noexcept {
        return min_x() < other.max_x() && other.min_x() < max_x()
            && min_y() < other.max_y() && other.min_y() < max_y();
    }

    [[nodiscard]] constexpr Rect inset_by(const EdgeInsets& insets) const noexcept {
        return from_edges(min_x() + insets.leading, min_y() + insets.top,
                          max_x() - insets.trailing, max_y() - insets.bottom);
    }

    [[nodiscard]] constexpr Rect outset_by(const EdgeInsets& insets) const noexcept {
        return from_edges(min_x() - insets.leading, min_y() - insets.top,
                          max_x() + insets.trailing, max_y() + insets.bottom);
    }

    [[nodiscard]] constexpr Rect offset_by(Point delta) const noexcept {
        return {origin + delta, size};
    }

    [[nodiscard]] constexpr Rect union_with(const Rect& other) const noexcept {
        if (is_empty()) return other;
        if (other.is_empty()) return *this;
        return from_edges(std::min(min_x(), other.min_x()),
                          std::min(min_y(), other.min_y()),
                          std::max(max_x(), other.max_x()),
                          std::max(max_y(), other.max_y()));
    }

    /// Returns an empty rect (not nullopt) when there is no overlap, so callers
    /// can chain without branching; check `is_empty()` when it matters.
    [[nodiscard]] constexpr Rect intersection_with(const Rect& other) const noexcept {
        const float left = std::max(min_x(), other.min_x());
        const float top = std::max(min_y(), other.min_y());
        const float right = std::min(max_x(), other.max_x());
        const float bottom = std::min(max_y(), other.max_y());
        if (right <= left || bottom <= top) {
            return Rect::zero();
        }
        return from_edges(left, top, right, bottom);
    }

    /// Snaps outward to whole device pixels. Used when a dirty rect must fully
    /// cover the fractional geometry it was derived from.
    [[nodiscard]] Rect pixel_aligned_outward(float scale_factor) const noexcept {
        const float left = std::floor(min_x() * scale_factor) / scale_factor;
        const float top = std::floor(min_y() * scale_factor) / scale_factor;
        const float right = std::ceil(max_x() * scale_factor) / scale_factor;
        const float bottom = std::ceil(max_y() * scale_factor) / scale_factor;
        return from_edges(left, top, right, bottom);
    }

    [[nodiscard]] constexpr bool operator==(const Rect&) const noexcept = default;
};

} // namespace ca::geometry
