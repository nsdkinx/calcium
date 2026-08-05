#pragma once

// The fluent constructor for `Path`.
//
// Verbs are appended in the order they are called; `close_path` ends the
// current subpath and the next `move_to` begins a new one. Building a path
// never validates that the caller's verb sequence is well formed beyond what
// the verb/point counts enforce; a rasterizer defines the rendering of a
// degenerate sequence (a lone move, a close before any draw) as a no-op.

#include "calcium/geometry/path.hpp"

namespace ca::geometry {

struct RoundedRectangle;

class PathBuilder {
public:
    PathBuilder() = default;

    PathBuilder& move_to(Point point);
    PathBuilder& line_to(Point point);
    PathBuilder& quadratic_to(Point control, Point end);
    PathBuilder& cubic_to(Point control1, Point control2, Point end);
    PathBuilder& close_path();

    PathBuilder& add_rect(const Rect& rect);
    PathBuilder& add_rounded_rectangle(const RoundedRectangle& rounded_rect);

    /// Seals the path and resets this builder for reuse.
    [[nodiscard]] Path build();

private:
    Path path_;
};

} // namespace ca::geometry
