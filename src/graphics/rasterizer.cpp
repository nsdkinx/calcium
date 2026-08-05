// Display-list rasterization into a gpu::DrawPass (see rasterizer.hpp).

#include "graphics/rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ca::graphics::rasterizer {

namespace {

// The state-stack and clip depths are bounded: save/restore nesting is a
// UI-authoring decision, and 16 levels cover every real layout. A deeper
// list is a bug in the authoring code, not a legitimate frame.
constexpr std::size_t k_max_state_depth = 16;

// Tessellation resolution: samples per quarter-arc (circular corners) or per
// cubic (continuous corners). 8 puts the tessellation error well below a
// device pixel at any usable radius — the silhouette is the smoothest part
// of the shape.
constexpr std::size_t k_corner_segments = 8;
constexpr std::size_t k_max_corner_points =
    4 * (2 * (k_corner_segments + 1) - 1);  // continuous: two cubics per corner

// The continuous-corner cubic constants (fit numerically; the derivation is
// in geometry/rounded_rectangle.hpp — the G2 squircle spec).
constexpr float k_continuous_a = 0.57105f;
constexpr float k_continuous_b = 0.74110f;
constexpr float k_continuous_m = 0.87055f;

// The corner frame: origin at the corner apex, axes pointing toward the rect
// center. In that frame the corner curve runs from (r, 0) on one edge to
// (0, r) on the other.
struct CornerFrame {
    float origin_x;
    float origin_y;
    float axis_x;  // +1 for left corners, −1 for right corners
    float axis_y;  // +1 for top corners, −1 for bottom corners
    float radius;
};

geometry::Point to_world(const CornerFrame& frame, float u, float v) {
    return {frame.origin_x + u * frame.axis_x,
            frame.origin_y + v * frame.axis_y};
}

// The two cubics of a continuous corner (P0…P3 and its mirror), sampled into
// the point stream. See rounded_rectangle.hpp for the control-point spec.
template <typename Emit>
void emit_continuous_corner(const CornerFrame& frame, Emit&& emit) {
    const float r = frame.radius;
    const float a = k_continuous_a * r;
    const float b = k_continuous_b * r;
    const float m = k_continuous_m * r;
    const float p0[2] = {r, 0.0f};
    const float p1[2] = {r, a};
    const float p2[2] = {r, b};
    const float p3[2] = {m, m};
    const float q0[2] = {m, m};
    const float q1[2] = {b, r};
    const float q2[2] = {a, r};
    const float q3[2] = {0.0f, r};

    const auto cubic = [&](const float c0[2], const float c1[2],
                           const float c2[2], const float c3[2],
                           std::size_t start_segment, std::size_t end_segment) {
        for (std::size_t i = start_segment; i <= end_segment; ++i) {
            const float t = static_cast<float>(i) / k_corner_segments;
            const float omt = 1.0f - t;
            const float u = omt * omt * omt * c0[0]
                          + 3.0f * omt * omt * t * c1[0]
                          + 3.0f * omt * t * t * c2[0]
                          + t * t * t * c3[0];
            const float v = omt * omt * omt * c0[1]
                          + 3.0f * omt * omt * t * c1[1]
                          + 3.0f * omt * t * t * c2[1]
                          + t * t * t * c3[1];
            emit(to_world(frame, u, v));
        }
    };
    // The second cubic shares its start point with the first cubic's end.
    cubic(p0, p1, p2, p3, 0, k_corner_segments);
    cubic(q0, q1, q2, q3, 1, k_corner_segments);
}

// The quarter-arc of a circular corner, sampled into the point stream.
template <typename Emit>
void emit_circular_corner(const CornerFrame& frame, Emit&& emit) {
    const float r = frame.radius;
    for (std::size_t i = 0; i <= k_corner_segments; ++i) {
        const float t = static_cast<float>(i) / k_corner_segments;
        const float angle = t * static_cast<float>(0.5 * 3.141592653589793);
        emit(to_world(frame, r * std::cos(angle), r * std::sin(angle)));
    }
}

// Appends the clamped rounded rect's outline (clockwise, top-leading first)
// into `out`; returns the point count. `out` must hold k_max_corner_points.
std::size_t tessellate_rounded_rectangle(
    const geometry::RoundedRectangle& input, geometry::Point* out) {
    const geometry::RoundedRectangle rr = input.clamped_radii();
    const float left = rr.bounds.min_x();
    const float top = rr.bounds.min_y();
    const float right = rr.bounds.max_x();
    const float bottom = rr.bounds.max_y();

    // Clockwise walk: top-leading, top-trailing, bottom-trailing, bottom-
    // leading. Each corner frame points its axes toward the rect center.
    struct Corner {
        float origin_x, origin_y;
        float axis_x, axis_y;
        float radius;
    };
    const Corner corners[4] = {
        {left, top, 1.0f, 1.0f, rr.top_leading_radius},          // TL
        {right, top, -1.0f, 1.0f, rr.top_trailing_radius},       // TR
        {right, bottom, -1.0f, -1.0f, rr.bottom_trailing_radius},  // BR
        {left, bottom, 1.0f, -1.0f, rr.bottom_leading_radius},     // BL
    };

    std::size_t count = 0;
    for (const Corner& corner : corners) {
        const CornerFrame frame{corner.origin_x, corner.origin_y,
                                corner.axis_x, corner.axis_y, corner.radius};
        if (corner.radius <= 0.0f) {
            out[count++] = to_world(frame, 0.0f, 0.0f);  // the apex
            continue;
        }
        if (rr.corner_curve == geometry::CornerCurve::circular) {
            emit_circular_corner(frame,
                                 [&](geometry::Point p) { out[count++] = p; });
        } else {
            emit_continuous_corner(frame,
                                   [&](geometry::Point p) { out[count++] = p; });
        }
    }
    return count;
}

// Whether two rects overlap (the culling test; touching edges do not count).
bool overlaps(geometry::Rect a, geometry::Rect b) {
    return a.min_x() < b.max_x() && b.min_x() < a.max_x()
        && a.min_y() < b.max_y() && b.min_y() < a.max_y();
}

// The axis-aligned hull of a polygon (culling bounds).
geometry::Rect bounds_of(std::span<const geometry::Point> polygon) {
    float left = polygon[0].x, top = polygon[0].y;
    float right = left, bottom = top;
    for (const geometry::Point& p : polygon) {
        left = std::min(left, p.x);
        top = std::min(top, p.y);
        right = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
    }
    return geometry::Rect::from_edges(left, top, right, bottom);
}

// The rasterizer's per-record state machine. Runs on the compositor thread;
// never allocates (all state is fixed-size stack arrays).
class Walker {
public:
    Walker(const DisplayList& display_list, gpu::DrawPass& pass,
           const geometry::AffineTransform& root_transform, float alpha)
        : display_list_(display_list), pass_(pass), alpha_(alpha) {
        ctm_[0] = root_transform;
    }

    void run() {
        const std::span<const std::byte> records = display_list_.raw_records();
        std::size_t offset = 0;
        while (offset + 4 <= records.size()) {
            const Command command = static_cast<Command>(
                static_cast<std::uint8_t>(records[offset]));
            const std::uint8_t paint_index =
                static_cast<std::uint8_t>(records[offset + 1]);
            std::uint16_t payload_size = 0;
            std::memcpy(&payload_size, records.data() + offset + 2,
                        sizeof(payload_size));
            const std::span<const std::byte> payload =
                records.subspan(offset + 4, payload_size);
            offset += 4 + payload_size;

            switch (command) {
            case Command::save_state:
                if (state_depth_ + 1 < k_max_state_depth) {
                    ++state_depth_;
                    ctm_[state_depth_] = ctm_[state_depth_ - 1];
                    clip_depth_at_save_[state_depth_] = clip_depth_;
                }
                break;
            case Command::restore_state:
                if (state_depth_ > 0) {
                    while (clip_depth_ > clip_depth_at_save_[state_depth_]) {
                        --clip_depth_;
                        pass_.pop_clip();
                    }
                    --state_depth_;
                }
                break;
            case Command::concat_transform: {
                geometry::AffineTransform transform;
                std::memcpy(&transform, payload.data(), sizeof(transform));
                ctm_[state_depth_] = ctm_[state_depth_].concatenating(transform);
                break;
            }
            case Command::clip_rect: {
                geometry::Rect rect;
                std::memcpy(&rect, payload.data(), sizeof(rect));
                geometry::Rect device = ctm_[state_depth_].apply_to_rect(rect);
                if (clip_depth_ < k_max_state_depth) {
                    if (clip_depth_ > 0) {
                        // Nested clips intersect; an empty intersection is
                        // expressed as a zero-area clip (nothing draws).
                        const geometry::Rect& current =
                            clip_stack_[clip_depth_ - 1];
                        device = geometry::Rect::from_edges(
                            std::max(device.min_x(), current.min_x()),
                            std::max(device.min_y(), current.min_y()),
                            std::min(device.max_x(), current.max_x()),
                            std::min(device.max_y(), current.max_y()));
                        if (device.width() <= 0.0f || device.height() <= 0.0f) {
                            device = geometry::Rect{};
                        }
                    }
                    clip_stack_[clip_depth_++] = device;
                    pass_.push_clip(device);
                }
                break;
            }
            case Command::fill_rect: {
                geometry::Rect rect;
                std::memcpy(&rect, payload.data(), sizeof(rect));
                const Color color = paint_color(paint_index);
                const float c[4] = {color.red, color.green, color.blue,
                                    color.alpha};
                const geometry::AffineTransform& ctm = ctm_[state_depth_];
                if (ctm.is_translation_only()) {
                    const geometry::Point corners[4] = {
                        {rect.min_x() + ctm.tx, rect.min_y() + ctm.ty},
                        {rect.max_x() + ctm.tx, rect.min_y() + ctm.ty},
                        {rect.max_x() + ctm.tx, rect.max_y() + ctm.ty},
                        {rect.min_x() + ctm.tx, rect.max_y() + ctm.ty},
                    };
                    emit_fill(c, corners, 4, true);
                } else {
                    const geometry::Point corners[4] = {
                        ctm.apply_to_point({rect.min_x(), rect.min_y()}),
                        ctm.apply_to_point({rect.max_x(), rect.min_y()}),
                        ctm.apply_to_point({rect.max_x(), rect.max_y()}),
                        ctm.apply_to_point({rect.min_x(), rect.max_y()}),
                    };
                    emit_fill(c, corners, 4, false);
                }
                break;
            }
            case Command::fill_rounded_rectangle: {
                geometry::RoundedRectangle rounded_rectangle;
                std::memcpy(&rounded_rectangle, payload.data(),
                            sizeof(rounded_rectangle));
                const Color color = paint_color(paint_index);
                const float c[4] = {color.red, color.green, color.blue,
                                    color.alpha};
                geometry::Point polygon[k_max_corner_points];
                const std::size_t point_count =
                    tessellate_rounded_rectangle(rounded_rectangle, polygon);
                const geometry::AffineTransform& ctm = ctm_[state_depth_];
                for (std::size_t i = 0; i < point_count; ++i) {
                    polygon[i] = ctm.apply_to_point(polygon[i]);
                }
                emit_fill(c, polygon, point_count, false);
                break;
            }
            }
        }
    }

private:
    [[nodiscard]] Color paint_color(std::uint8_t paint_index) const {
        return paint_index != 0xFF ? display_list_.paint_at(paint_index)
                                   : Color::clear();
    }

    void emit_fill(const float color[4], const geometry::Point* polygon,
                   std::size_t point_count, bool is_axis_aligned_rect) {
        // Culling: fully transparent fills and fills outside the clip are
        // free to skip (the clip test is conservative — a rotated shape's
        // bbox may overlap while the shape does not; the GPU clips exactly).
        if (alpha_ <= 0.0f || color[3] <= 0.0f) {
            return;
        }
        float c[4] = {color[0], color[1], color[2], color[3] * alpha_};
        if (clip_depth_ > 0
            && !overlaps(bounds_of({polygon, point_count}),
                         clip_stack_[clip_depth_ - 1])) {
            return;
        }
        if (is_axis_aligned_rect && point_count == 4) {
            // Corners are ordered top-left, top-right, bottom-right,
            // bottom-left — a valid rect under a translation-only CTM.
            pass_.fill_rect(
                geometry::Rect::from_edges(polygon[0].x, polygon[0].y,
                                           polygon[2].x, polygon[2].y),
                c);
            return;
        }
        pass_.fill_polygon({polygon, point_count}, c);
    }

    const DisplayList& display_list_;
    gpu::DrawPass& pass_;
    float alpha_ = 1.0f;

    // CTM stack: entry 0 is the root transform; save/restore push/pop.
    geometry::AffineTransform ctm_[k_max_state_depth];
    std::size_t clip_depth_at_save_[k_max_state_depth];
    std::size_t state_depth_ = 0;

    // Device-space clip stack; moves in lockstep with the pass's own stack
    // (each clip_rect pushes, the matching restore pops).
    geometry::Rect clip_stack_[k_max_state_depth];
    std::size_t clip_depth_ = 0;
};

} // namespace

void draw(const DisplayList& display_list, gpu::DrawPass& pass,
          const geometry::AffineTransform& root_transform, float alpha) {
    Walker walker{display_list, pass, root_transform, alpha};
    walker.run();
}

void fill_rounded_rectangle(gpu::DrawPass& pass,
                            const geometry::RoundedRectangle& rounded_rectangle,
                            const geometry::AffineTransform& transform,
                            const float color[4], float alpha) {
    float c[4] = {color[0], color[1], color[2], color[3] * alpha};
    if (c[3] <= 0.0f) {
        return;
    }
    geometry::Point polygon[k_max_corner_points];
    const std::size_t point_count =
        tessellate_rounded_rectangle(rounded_rectangle, polygon);
    if (transform.is_translation_only()) {
        for (std::size_t i = 0; i < point_count; ++i) {
            polygon[i].x += transform.tx;
            polygon[i].y += transform.ty;
        }
    } else {
        for (std::size_t i = 0; i < point_count; ++i) {
            polygon[i] = transform.apply_to_point(polygon[i]);
        }
    }
    pass.fill_polygon({polygon, point_count}, c);
}

} // namespace ca::graphics::rasterizer
