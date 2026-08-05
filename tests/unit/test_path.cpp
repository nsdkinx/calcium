// ca::geometry::Path and PathBuilder tests.

#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/path.hpp"
#include "calcium/geometry/path_builder.hpp"

#include "calcium_test.hpp"

using ca::geometry::AffineTransform;
using ca::geometry::Path;
using ca::geometry::PathBuilder;
using ca::geometry::PathVerb;
using ca::geometry::Point;
using ca::geometry::Rect;

CA_TEST(path_builder_verb_and_point_stream) {
    PathBuilder builder;
    builder.move_to({0.0f, 0.0f});
    builder.line_to({10.0f, 0.0f});
    builder.quadratic_to({10.0f, 10.0f}, {0.0f, 10.0f});
    builder.cubic_to({0.0f, 5.0f}, {5.0f, 0.0f}, {0.0f, 0.0f});
    builder.close_path();
    const Path path = builder.build();

    CA_CHECK(path.verb_count() == 5);
    CA_CHECK(path.point_count() == 1 + 1 + 2 + 3);  // move/line/quad/cubic

    const PathVerb expected[] = {PathVerb::move, PathVerb::line,
                                 PathVerb::quadratic, PathVerb::cubic,
                                 PathVerb::close};
    CA_CHECK(path.verbs().size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        CA_CHECK(path.verbs()[i] == expected[i]);
    }
    CA_CHECK(path.points()[2] == Point{10.0f, 10.0f});  // quad control
    CA_CHECK(path.points()[4] == Point{0.0f, 5.0f});    // first cubic control
}

CA_TEST(path_empty) {
    const Path path;
    CA_CHECK(path.is_empty());
    CA_CHECK(path.bounds() == Rect::zero());
}

CA_TEST(path_bounds_are_the_control_hull) {
    // A cubic bulges inside the hull of its control points, so control-point
    // bounds are conservative: they never under-cover.
    PathBuilder builder;
    builder.move_to({0.0f, 0.0f});
    builder.cubic_to({-50.0f, 100.0f}, {150.0f, 100.0f}, {100.0f, 0.0f});
    const Path path = builder.build();

    const Rect bounds = path.bounds();
    CA_CHECK(bounds == Rect::from_edges(-50.0f, 0.0f, 150.0f, 100.0f));
}

CA_TEST(path_transformed_by_maps_points_and_keeps_verbs) {
    PathBuilder builder;
    builder.move_to({0.0f, 0.0f});
    builder.line_to({10.0f, 0.0f});
    builder.close_path();
    const Path path = builder.build();

    const Path moved =
        path.transformed_by(AffineTransform::make_translation(5.0f, -5.0f));
    CA_CHECK(moved.verb_count() == path.verb_count());
    CA_CHECK(moved.points()[0] == Point{5.0f, -5.0f});
    CA_CHECK(moved.points()[1] == Point{15.0f, -5.0f});

    // The source is unchanged.
    CA_CHECK(path.points()[0] == Point{0.0f, 0.0f});
}

CA_TEST(path_builder_reuse_after_build) {
    PathBuilder builder;
    builder.move_to({0.0f, 0.0f});
    builder.line_to({1.0f, 0.0f});
    const Path first = builder.build();
    CA_CHECK(first.point_count() == 2);

    builder.move_to({5.0f, 5.0f});
    builder.line_to({6.0f, 5.0f});
    const Path second = builder.build();
    CA_CHECK(second.point_count() == 2);
    CA_CHECK(second.points()[0] == Point{5.0f, 5.0f});
    CA_CHECK(first.point_count() == 2);  // sealed paths are immutable copies
}

CA_TEST(path_add_rect) {
    PathBuilder builder;
    builder.add_rect(Rect::from_xywh(0.0f, 0.0f, 10.0f, 20.0f));
    const Path path = builder.build();

    CA_CHECK(path.verb_count() == 5);  // move + 3 lines + close
    CA_CHECK(path.points()[0] == Point{0.0f, 0.0f});
    CA_CHECK(path.points()[1] == Point{10.0f, 0.0f});
    CA_CHECK(path.points()[2] == Point{10.0f, 20.0f});
    CA_CHECK(path.points()[3] == Point{0.0f, 20.0f});
}

CA_TEST_MAIN()
