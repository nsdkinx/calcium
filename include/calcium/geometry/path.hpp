#pragma once

// Vector paths.
//
// A Path is a compact, copyable sequence of drawing verbs over 2D points,
// built exclusively through `PathBuilder` (which owns the invariants: every
// `move`/`line`/`quadratic`/`cubic` verb consumes its points from the point
// stream in order, and `close` consumes none).
//
// The verb set is deliberately the one Skia/CA-style rasterizers understand
// with no further interpretation: moves, lines, quadratics, cubics, and
// closes. There is no arc verb and no conic: arcs are emitted as cubics (see
// rounded_rectangle.hpp for the corner construction) so a backend has exactly
// one curve primitive to tessellate. `fill_rule` is a paint property, not a
// path property, matching the display-list spec.
//
// `bounds()` is conservative — the hull of all control points. A cubic lies
// inside the convex hull of its control points, so this never under-covers;
// the compositor may cull with it but must not clip with it.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "calcium/geometry/point.hpp"
#include "calcium/geometry/rect.hpp"

namespace ca::geometry {

struct AffineTransform;

/// The drawing operations a path can contain, in the order they consume
/// points: move 1, line 1, quadratic 2, cubic 3, close 0.
enum class PathVerb : std::uint8_t { move, line, quadratic, cubic, close };

class PathBuilder;

/// An immutable sequence of drawing verbs and their points.
class Path {
public:
    Path() = default;

    [[nodiscard]] bool is_empty() const noexcept { return verbs_.empty(); }

    [[nodiscard]] std::size_t verb_count() const noexcept { return verbs_.size(); }
    [[nodiscard]] std::size_t point_count() const noexcept { return points_.size(); }

    [[nodiscard]] std::span<const PathVerb> verbs() const noexcept { return verbs_; }
    [[nodiscard]] std::span<const Point> points() const noexcept { return points_; }

    /// Conservative axis-aligned bounds (control-point hull). Zero when empty.
    [[nodiscard]] Rect bounds() const noexcept;

    /// A new path with every point mapped through `transform`.
    [[nodiscard]] Path transformed_by(const AffineTransform& transform) const;

    [[nodiscard]] bool operator==(const Path&) const noexcept = default;

private:
    friend class PathBuilder;
    std::vector<PathVerb> verbs_;
    std::vector<Point> points_;
};

} // namespace ca::geometry
