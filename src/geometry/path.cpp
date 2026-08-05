#include "calcium/geometry/path.hpp"

#include <algorithm>

#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/path_builder.hpp"
#include "calcium/geometry/rounded_rectangle.hpp"

namespace ca::geometry {

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

Rect Path::bounds() const noexcept {
    if (points_.empty()) {
        return Rect::zero();
    }
    float min_x = points_.front().x;
    float min_y = points_.front().y;
    float max_x = min_x;
    float max_y = min_y;
    for (const Point point : points_) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }
    return Rect::from_edges(min_x, min_y, max_x, max_y);
}

Path Path::transformed_by(const AffineTransform& transform) const {
    Path result = *this;
    result.points_.clear();
    result.points_.reserve(points_.size());
    for (const Point point : points_) {
        result.points_.push_back(transform.apply_to_point(point));
    }
    return result;
}

// ---------------------------------------------------------------------------
// PathBuilder
// ---------------------------------------------------------------------------

PathBuilder& PathBuilder::move_to(Point point) {
    path_.verbs_.push_back(PathVerb::move);
    path_.points_.push_back(point);
    return *this;
}

PathBuilder& PathBuilder::line_to(Point point) {
    path_.verbs_.push_back(PathVerb::line);
    path_.points_.push_back(point);
    return *this;
}

PathBuilder& PathBuilder::quadratic_to(Point control, Point end) {
    path_.verbs_.push_back(PathVerb::quadratic);
    path_.points_.push_back(control);
    path_.points_.push_back(end);
    return *this;
}

PathBuilder& PathBuilder::cubic_to(Point control1, Point control2, Point end) {
    path_.verbs_.push_back(PathVerb::cubic);
    path_.points_.push_back(control1);
    path_.points_.push_back(control2);
    path_.points_.push_back(end);
    return *this;
}

PathBuilder& PathBuilder::close_path() {
    path_.verbs_.push_back(PathVerb::close);
    return *this;
}

PathBuilder& PathBuilder::add_rect(const Rect& rect) {
    const Point origin = rect.origin;
    const Point far_corner{rect.max_x(), rect.max_y()};
    move_to(origin);
    line_to({far_corner.x, origin.y});
    line_to(far_corner);
    line_to({origin.x, far_corner.y});
    close_path();
    return *this;
}

PathBuilder& PathBuilder::add_rounded_rectangle(const RoundedRectangle& rounded) {
    const Path path = rounded.to_path();
    path_.verbs_.insert(path_.verbs_.end(), path.verbs_.begin(), path.verbs_.end());
    path_.points_.insert(path_.points_.end(), path.points_.begin(), path.points_.end());
    return *this;
}

Path PathBuilder::build() {
    Path result;
    result.verbs_.swap(path_.verbs_);
    result.points_.swap(path_.points_);
    return result;
}

} // namespace ca::geometry
