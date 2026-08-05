// Display-list rasterizer tests (src/graphics/rasterizer.cpp).
//
// A recording fake DrawPass captures the calls the rasterizer emits, so the
// IR → pass mapping is verified without a GPU: transforms are applied,
// opacity multiplies paints, clips convert to device space and stack, and
// fully transparent or clipped-out fills are culled.

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "calcium/graphics/display_list.hpp"
#include "calcium/graphics/paint.hpp"
#include "calcium/gpu/draw_pass.hpp"
#include "calcium_test.hpp"
#include "graphics/rasterizer.hpp"

using ca::graphics::Color;
using ca::graphics::DisplayList;
using ca::graphics::DisplayListRecorder;
using ca::graphics::Paint;
using ca::geometry::AffineTransform;
using ca::geometry::Point;
using ca::geometry::Rect;
using ca::geometry::RoundedRectangle;

namespace {

// A draw pass that records every call instead of drawing.
class RecordingPass final : public ca::gpu::DrawPass {
public:
    struct Clear {
        float color[4];
    };
    struct FillRect {
        Rect rect;
        float color[4];
    };
    struct FillPolygon {
        std::vector<Point> polygon;
        float color[4];
    };
    struct Clip {
        Rect rect;
    };

    void clear(const float color[4]) override {
        clears.push_back(Clear{{color[0], color[1], color[2], color[3]}});
    }
    void push_clip(Rect rect) override { clips.push_back(Clip{rect}); }
    void pop_clip() override { clip_pops++; }
    void fill_rect(Rect rect, const float color[4]) override {
        fill_rects.push_back(FillRect{
            rect, {color[0], color[1], color[2], color[3]}});
    }
    void fill_polygon(std::span<const Point> polygon,
                      const float color[4]) override {
        FillPolygon record;
        record.polygon.assign(polygon.begin(), polygon.end());
        record.color[0] = color[0];
        record.color[1] = color[1];
        record.color[2] = color[2];
        record.color[3] = color[3];
        polygons.push_back(std::move(record));
    }
    void end_and_present() override {}
    [[nodiscard]] ca::core::Timestamp acquired_at() const noexcept override {
        return ca::core::Timestamp{};
    }
    [[nodiscard]] ca::core::Timestamp submitted_at() const noexcept override {
        return ca::core::Timestamp{};
    }

    std::vector<Clear> clears;
    std::vector<FillRect> fill_rects;
    std::vector<FillPolygon> polygons;
    std::vector<Clip> clips;
    int clip_pops = 0;
};

bool same_color(const float a[4], const std::array<float, 4> b) {
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(a[i] - b[i]) > 1e-5f) {
            return false;
        }
    }
    return true;
}

} // namespace

CA_TEST(rasterizer_fill_rect_translation_fast_path) {
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 100, 50},
                       Paint::solid_color(Color::srgb(1.0f, 0.0f, 0.0f)));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(
        list, pass,
        AffineTransform::make_translation(200.0f, 150.0f), 1.0f);

    CA_CHECK(pass.fill_rects.size() == 1);
    CA_CHECK(pass.polygons.empty());
    CA_CHECK(pass.fill_rects[0].rect ==
             Rect::from_edges(200.0f, 150.0f, 300.0f, 200.0f));
    CA_CHECK(same_color(pass.fill_rects[0].color,
                        std::array{1.0f, 0.0f, 0.0f, 1.0f}));
}

CA_TEST(rasterizer_opacity_multiplies_alpha) {
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 10, 10},
                       Paint::solid_color(Color::srgb(1.0f, 0.0f, 0.0f, 1.0f)));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   0.5f);

    CA_CHECK(pass.fill_rects.size() == 1);
    CA_CHECK(same_color(pass.fill_rects[0].color,
                        std::array{1.0f, 0.0f, 0.0f, 0.5f}));
}

CA_TEST(rasterizer_rotation_emits_polygon) {
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 10, 10},
                       Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(
        list, pass,
        AffineTransform::make_rotation(1.5707963f).concatenating(
            AffineTransform::make_translation(5.0f, 0.0f)),
        1.0f);

    // A 90° rotation about the origin of {0,0,10,10} → {0,-10,0,10}ish:
    // the pass gets a 4-point polygon, not a rect.
    CA_CHECK(pass.fill_rects.empty());
    CA_CHECK(pass.polygons.size() == 1);
    CA_CHECK(pass.polygons[0].polygon.size() == 4);
}

CA_TEST(rasterizer_clip_converts_and_stacks) {
    DisplayListRecorder recorder;
    recorder.save_state();
    recorder.clip_to_rect(Rect{0, 0, 50, 50});
    recorder.fill_rect(Rect{0, 0, 100, 100},
                       Paint::solid_color(Color::white()));
    recorder.restore_state();
    recorder.fill_rect(Rect{0, 0, 100, 100},
                       Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(
        list, pass, AffineTransform::make_translation(10.0f, 20.0f), 1.0f);

    // The clip lands in device space (translated), one push and one pop, and
    // the second fill is drawn without a clip.
    CA_CHECK(pass.clips.size() == 1);
    CA_CHECK(pass.clips[0].rect == Rect::from_edges(10.0f, 20.0f, 60.0f, 70.0f));
    CA_CHECK(pass.clip_pops == 1);
    CA_CHECK(pass.fill_rects.size() == 2);
}

CA_TEST(rasterizer_clip_culls_offscreen_fills) {
    DisplayListRecorder recorder;
    recorder.clip_to_rect(Rect{0, 0, 50, 50});
    recorder.fill_rect(Rect{100, 100, 200, 200},
                       Paint::solid_color(Color::white()));
    recorder.fill_rect(Rect{10, 10, 30, 30},
                       Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   1.0f);

    CA_CHECK(pass.fill_rects.size() == 1);
    CA_CHECK(pass.fill_rects[0].rect == Rect{10.0f, 10.0f, 30.0f, 30.0f});
}

CA_TEST(rasterizer_transparent_fill_is_culled) {
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 10, 10},
                       Paint::solid_color(Color::clear()));
    recorder.fill_rect(Rect{0, 0, 10, 10},
                       Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   0.0f);  // layer opacity 0 → everything gone
    CA_CHECK(pass.fill_rects.empty());

    RecordingPass pass2;
    ca::graphics::rasterizer::draw(list, pass2, AffineTransform::identity(),
                                   1.0f);
    CA_CHECK(pass2.fill_rects.size() == 1);
}

CA_TEST(rasterizer_rounded_rectangle_tessellates_inside_bounds) {
    DisplayListRecorder recorder;
    recorder.fill_rounded_rectangle(
        RoundedRectangle::uniform(Rect{0, 0, 100, 60}, 12.0f),
        Paint::solid_color(Color::srgb(0.0f, 0.5f, 1.0f)));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   1.0f);

    CA_CHECK(pass.polygons.size() == 1);
    const auto& polygon = pass.polygons[0].polygon;
    CA_CHECK(polygon.size() >= 4);
    // Every tessellated vertex lies on or inside the rounded rect's bounds.
    for (const Point& p : polygon) {
        CA_CHECK(p.x >= -1e-4f && p.x <= 100.0f + 1e-4f);
        CA_CHECK(p.y >= -1e-4f && p.y <= 60.0f + 1e-4f);
    }
    // Continuous corners (the default) put sample points on the diagonal
    // bite: the polygon is not a plain rectangle.
    bool has_rounded_corner = false;
    for (const Point& p : polygon) {
        if (p.x > 1e-3f && p.x < 11.0f && p.y > 1e-3f && p.y < 11.0f
            && std::fabs(p.x - p.y) > 1e-3f) {
            has_rounded_corner = true;
        }
    }
    CA_CHECK(has_rounded_corner);
}

CA_TEST(rasterizer_rounded_rectangle_outline_and_fan) {
    // The tessellation must trace a proper clockwise outline of the rounded
    // rect: the four straight edges appear as consecutive vertex pairs, and
    // the centroid fan — exactly what the pass renders — must cover the
    // polygon (its area equals the shoelace area). The outline is NOT
    // convex: the squircle corner curves meet the edges perpendicularly, so
    // the corners are concave by design — a fan from an edge vertex would
    // spill across them (the glitched-shape bug this guards against).
    DisplayListRecorder recorder;
    recorder.fill_rounded_rectangle(
        RoundedRectangle::uniform(Rect{0, 0, 200, 120}, 24.0f),
        Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   1.0f);
    CA_CHECK(pass.polygons.size() == 1);
    const auto& polygon = pass.polygons[0].polygon;
    CA_CHECK(polygon.size() == 68);  // 4 corners × 17 samples

    // The four straight boundary edges, as consecutive vertex pairs.
    bool has_top_edge = false, has_right_edge = false;
    bool has_bottom_edge = false, has_left_edge = false;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point a = polygon[i];
        const Point b = polygon[(i + 1) % polygon.size()];
        const bool horizontal = std::fabs(a.y - b.y) < 1e-4f;
        const bool vertical = std::fabs(a.x - b.x) < 1e-4f;
        if (horizontal && a.y == 0.0f && a.x < b.x) {
            has_top_edge = true;
        } else if (vertical && a.x == 200.0f && a.y < b.y) {
            has_right_edge = true;
        } else if (horizontal && a.y == 120.0f && b.x < a.x) {
            has_bottom_edge = true;
        } else if (vertical && a.x == 0.0f && b.y < a.y) {
            has_left_edge = true;
        }
    }
    CA_CHECK(has_top_edge && has_right_edge && has_bottom_edge && has_left_edge);

    // The centroid fan covers the polygon: shoelace area == fan area, and
    // every fan triangle winds the same way (star-shaped w.r.t. the
    // centroid — the pass's contract).
    double area2 = 0.0, centroid_x = 0.0, centroid_y = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point a = polygon[i];
        const Point b = polygon[(i + 1) % polygon.size()];
        const double cross =
            static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
        area2 += cross;
        centroid_x += (static_cast<double>(a.x) + b.x) * cross;
        centroid_y += (static_cast<double>(a.y) + b.y) * cross;
    }
    CA_CHECK(area2 > 0.0);  // clockwise winding (y-down)
    centroid_x /= 3.0 * area2;
    centroid_y /= 3.0 * area2;

    double fan_area2 = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point b = polygon[(i + 1) % polygon.size()];
        const Point c{static_cast<float>(centroid_x),
                      static_cast<float>(centroid_y)};
        const double cross =
            static_cast<double>(polygon[i].x - c.x) * (b.y - c.y)
            - static_cast<double>(polygon[i].y - c.y) * (b.x - c.x);
        CA_CHECK(cross > 0.0);  // every fan triangle winds clockwise
        fan_area2 += cross;
    }
    CA_CHECK_NEAR(fan_area2, area2, area2 * 1e-3);
}

CA_TEST(rasterizer_rounded_rectangle_plain_rect_fast_path) {
    DisplayListRecorder recorder;
    recorder.fill_rounded_rectangle(
        RoundedRectangle::uniform(Rect{0, 0, 100, 60}, 0.0f),
        Paint::solid_color(Color::white()));
    const DisplayList list = recorder.seal();

    RecordingPass pass;
    ca::graphics::rasterizer::draw(list, pass, AffineTransform::identity(),
                                   1.0f);

    // Zero radius: the tessellation degenerates to the rect's four corners.
    CA_CHECK(pass.polygons.size() == 1);
    CA_CHECK(pass.polygons[0].polygon.size() == 4);
}

CA_TEST(rasterizer_layer_background_fill_helper) {
    RecordingPass pass;
    const float background[4] = {0.1f, 0.2f, 0.3f, 0.8f};
    ca::graphics::rasterizer::fill_rounded_rectangle(
        pass, RoundedRectangle::uniform(Rect{0, 0, 200, 120}, 24.0f),
        AffineTransform::make_translation(50.0f, 30.0f), background, 0.5f);

    CA_CHECK(pass.polygons.size() == 1);
    // The transform translated every vertex; opacity halved the alpha.
    const auto& polygon = pass.polygons[0].polygon;
    CA_CHECK(polygon.size() >= 4);
    for (const Point& p : polygon) {
        CA_CHECK(p.x >= 50.0f - 1e-4f && p.x <= 250.0f + 1e-4f);
        CA_CHECK(p.y >= 30.0f - 1e-4f && p.y <= 150.0f + 1e-4f);
    }
    CA_CHECK(same_color(pass.polygons[0].color,
                        std::array{0.1f, 0.2f, 0.3f, 0.4f}));
}

CA_TEST_MAIN()
