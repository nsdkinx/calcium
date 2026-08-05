// ca::layer tests: the tree, its SoA packet, and the animatable properties.

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/core/thread_affinity.hpp"
#include "calcium/layer/layer.hpp"
#include "calcium/layer/layer_tree.hpp"
#include "calcium_test.hpp"

// The tree's mutators assert the UI role; the test thread plays it.
struct UiRole {
    UiRole() { ca::core::register_current_thread_role(ca::core::ThreadRole::ui); }
};
static UiRole g_ui_role;

using ca::animation::AnimationCoordinator;
using ca::core::Identifier;
using ca::graphics::Color;
using ca::graphics::DisplayList;
using ca::graphics::DisplayListRecorder;
using ca::graphics::Paint;
using ca::geometry::AffineTransform;
using ca::geometry::Point;
using ca::geometry::Rect;

namespace {

std::unique_ptr<AnimationCoordinator> make_coordinator() {
    auto result = AnimationCoordinator::create(
        {.max_animated_properties = 64, .max_concurrent_gestures = 8});
    CA_CHECK(result.has_value());
    return std::move(result).take_value();
}

} // namespace

CA_TEST(layer_tree_creates_root_and_layers) {
    auto coordinator = make_coordinator();
    auto tree_result = ca::layer::LayerTree::create({.coordinator = coordinator.get()});
    CA_CHECK(tree_result.has_value());
    auto tree = std::move(tree_result).take_value();

    auto root = tree->root_layer();
    CA_CHECK(root.is_valid());
    CA_CHECK(root.sublayers().empty());

    const auto child = tree->create_layer();
    CA_CHECK(child.is_valid());
    CA_CHECK(child.handle().index() == 1);  // row 0 is the root

    const auto invalid = ca::layer::Layer{};
    CA_CHECK(!invalid.is_valid());
}

CA_TEST(layer_tree_attributes_round_trip) {
    auto coordinator = make_coordinator();
    auto tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto layer = tree->create_layer();

    layer.set_bounds(Rect{0.0f, 0.0f, 200.0f, 120.0f});
    layer.set_corner_radius(16.0f);
    layer.set_background_color(Color::srgb(0.1f, 0.2f, 0.3f, 0.9f));
    layer.set_transform(AffineTransform::make_scale(2.0f, 2.0f));
    const Identifier identifier = Identifier::generate();
    layer.set_identifier(identifier);

    CA_CHECK(layer.bounds() == Rect{0.0f, 0.0f, 200.0f, 120.0f});
    CA_CHECK(layer.corner_radius() == 16.0f);
    CA_CHECK(layer.background_color() == Color::srgb(0.1f, 0.2f, 0.3f, 0.9f));
    CA_CHECK(layer.transform() == AffineTransform::make_scale(2.0f, 2.0f));
    CA_CHECK(layer.identifier() == identifier);
}

CA_TEST(layer_tree_sublayers_and_detach) {
    auto coordinator = make_coordinator();
    auto tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto root = tree->root_layer();
    auto a = tree->create_layer();
    auto b = tree->create_layer();

    root.add_sublayer(a);
    root.add_sublayer(b);
    CA_CHECK(root.sublayers().size() == 2);
    CA_CHECK(root.sublayers()[0] == a.handle());
    CA_CHECK(root.sublayers()[1] == b.handle());

    a.remove_from_superlayer();
    CA_CHECK(root.sublayers().size() == 1);
    CA_CHECK(root.sublayers()[0] == b.handle());

    // Adding a layer that already has a parent moves it.
    root.add_sublayer(b);  // b still under root — no-op parent change
    auto c = tree->create_layer();
    c.add_sublayer(b);
    CA_CHECK(root.sublayers().empty());
    CA_CHECK(c.sublayers().size() == 1);
    CA_CHECK(c.sublayers()[0] == b.handle());

    // Layers from a different tree cannot be attached.
    auto other_tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto foreign = other_tree->create_layer();
    root.add_sublayer(foreign);
    CA_CHECK(root.sublayers().empty());
}

CA_TEST(layer_tree_commit_publishes_packet) {
    auto coordinator = make_coordinator();
    auto tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto root = tree->root_layer();
    auto card = tree->create_layer();

    CA_CHECK(tree->latest_packet() == nullptr);  // nothing committed yet

    root.set_bounds(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    root.set_background_color(Color::srgb(0.02f, 0.02f, 0.04f));
    card.set_bounds(Rect{0.0f, 0.0f, 200.0f, 120.0f});
    card.set_corner_radius(24.0f);
    card.set_background_color(Color::srgb(0.2f, 0.4f, 0.8f));
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 100, 100}, Paint::solid_color(Color::white()));
    card.set_display_list(recorder.seal());
    root.add_sublayer(card);
    tree->commit();

    const auto packet = tree->latest_packet();
    CA_CHECK(packet != nullptr);
    CA_CHECK(packet->layer_count() == 2);

    // Row 0 = root, row 1 = card (draw order: parents before children).
    CA_CHECK(packet->bounds()[0] == Rect{0.0f, 0.0f, 800.0f, 600.0f});
    CA_CHECK(packet->bounds()[1] == Rect{0.0f, 0.0f, 200.0f, 120.0f});
    CA_CHECK(packet->corner_radii()[1] == 24.0f);
    CA_CHECK(packet->background_colors()[1] == Color::srgb(0.2f, 0.4f, 0.8f));
    CA_CHECK(packet->parent_indices()[1] == 0);
    CA_CHECK(packet->parent_indices()[0] == 0xFFFFFFFF);
    CA_CHECK(!packet->display_lists()[1].is_empty());
    CA_CHECK(packet->display_lists()[0].is_empty());

    // The properties are registered with the coordinator (distinct indices).
    CA_CHECK(packet->position_property_indices()[0]
              != packet->position_property_indices()[1]);
    CA_CHECK(packet->opacity_property_indices()[0]
              != packet->opacity_property_indices()[1]);
}

CA_TEST(layer_tree_packet_updates_on_commit) {
    auto coordinator = make_coordinator();
    auto tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto root = tree->root_layer();
    root.set_bounds(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    tree->commit();
    const auto first = tree->latest_packet();
    CA_CHECK(first->bounds()[0] == Rect{0.0f, 0.0f, 800.0f, 600.0f});

    // Mutating without commit keeps the old packet (the compositor
    // re-composites it with fresh animation values — docs/02 §2.2).
    root.set_bounds(Rect{0.0f, 0.0f, 400.0f, 300.0f});
    CA_CHECK(tree->latest_packet() == first);
    CA_CHECK(tree->latest_packet()->bounds()[0]
             == Rect{0.0f, 0.0f, 800.0f, 600.0f});

    tree->commit();
    const auto second = tree->latest_packet();
    CA_CHECK(second != first);
    CA_CHECK(second->bounds()[0] == Rect{0.0f, 0.0f, 400.0f, 300.0f});
}

CA_TEST(layer_position_animates_through_coordinator) {
    auto coordinator = make_coordinator();
    auto tree = std::move(
        ca::layer::LayerTree::create({.coordinator = coordinator.get()})
            .take_value());
    auto card = tree->create_layer();
    card.position().set_value_immediately({100.0f, 50.0f});
    tree->commit();

    const auto& packet = tree->latest_packet();
    const std::uint32_t position_index = packet->position_property_indices()[1];

    // Ticks run on a real compositor thread (the thread contract: the UI
    // thread never calls tick_and_publish — docs/02 §2.3). The tick thread
    // advances the analytical clock in 60 Hz steps, as the compositor does.
    std::atomic<bool> stop_ticking{false};
    std::thread compositor_thread{[&] {
        ca::core::register_current_thread_role(ca::core::ThreadRole::compositor);
        ca::core::Timestamp t = ca::core::Timestamp::now();
        while (!stop_ticking.load(std::memory_order_acquire)) {
            coordinator->tick_and_publish(t);
            t = t + ca::core::Duration::from_seconds(1.0 / 60.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }};

    // The immediate intent lands within a tick or two.
    for (int i = 0; i < 100
         && coordinator->presentation_value(position_index, 0) != 100.0;
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CA_CHECK(coordinator->presentation_value(position_index, 0) == 100.0);
    CA_CHECK(coordinator->presentation_value(position_index, 1) == 50.0);

    // A spring intent moves the presentation value over ticks.
    card.position().set_value({300.0f, 200.0f},
                              ca::animation::Motion::spring(
                                  ca::animation::SpringConfiguration::
                                      with_response_and_damping_ratio(0.3, 1.0)));
    for (int i = 0; i < 100
         && coordinator->presentation_value(position_index, 0) == 100.0;
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CA_CHECK(coordinator->presentation_value(position_index, 0) != 100.0);
    CA_CHECK(!coordinator->property_at_rest(position_index));

    // The spring settles and reports rest; the value lands on the target.
    for (int i = 0; i < 500 && !coordinator->property_at_rest(position_index);
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CA_CHECK(coordinator->property_at_rest(position_index));
    CA_CHECK_NEAR(coordinator->presentation_value(position_index, 0), 300.0,
                  0.5);
    CA_CHECK_NEAR(coordinator->presentation_value(position_index, 1), 200.0,
                  0.5);

    stop_ticking.store(true, std::memory_order_release);
    compositor_thread.join();
}

CA_TEST(layer_tree_requires_coordinator) {
    const auto result = ca::layer::LayerTree::create({.coordinator = nullptr});
    CA_CHECK(!result.has_value());
    CA_CHECK(result.error().code() == ca::core::ErrorCode::invalid_argument);
}

CA_TEST_MAIN()
