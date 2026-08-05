// The layer tree (docs/02-architecture.md §4.1) and the Layer facade.

#include "calcium/layer/layer_tree.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include "calcium/core/thread_affinity.hpp"

namespace ca::layer {

namespace {

constexpr std::uint32_t k_no_parent = 0xFFFFFFFF;

} // namespace

// ---------------------------------------------------------------------------
// LayerTree
// ---------------------------------------------------------------------------

core::Result<std::unique_ptr<LayerTree>> LayerTree::create(
    const Configuration& configuration) {
    if (configuration.coordinator == nullptr) {
        return core::Result<std::unique_ptr<LayerTree>>{
            core::ErrorCode::invalid_argument,
            "layer tree needs an animation coordinator"};
    }
    auto tree =
        std::unique_ptr<LayerTree>{new LayerTree(configuration)};
    // Row 0 is the root: it holds the window's bounds and its subtree is the
    // window's content. Its position property is the window-space origin.
    tree->root_layer_ = tree->create_layer();
    return core::Result<std::unique_ptr<LayerTree>>{std::move(tree)};
}

LayerTree::LayerTree(Configuration configuration)
    : configuration_(configuration) {}

LayerTree::~LayerTree() = default;

Layer LayerTree::create_layer() {
    CA_ASSERT_UI_THREAD();

    bool is_new_slot = false;
    const LayerHandle handle = pool_.acquire(is_new_slot);
    if (is_new_slot) {
        nodes_.emplace_back();
        bounds_.push_back({});
        corner_radii_.push_back(0.0f);
        background_colors_.push_back(graphics::Color::clear());
        transforms_.push_back(geometry::AffineTransform::identity());
        position_property_indices_.push_back(0);
        opacity_property_indices_.push_back(0);
        display_lists_.emplace_back();
        parent_indices_.push_back(k_no_parent);
    }

    // Register the layer's animatable properties with the coordinator. The
    // property objects live in the deque (stable addresses); the indices
    // land in the SoA pools where the compositor's packet reads them.
    auto& node = nodes_.back();
    auto position_result = animation::AnimatablePoint::create(
        *configuration_.coordinator, geometry::Point{0.0f, 0.0f});
    auto opacity_result = animation::AnimatableFloat::create(
        *configuration_.coordinator, 1.0f);
    // Registration can only fail on Twell capacity exhaustion; the pool rows
    // have already grown, so fail loudly rather than half-register.
    if (!position_result.has_value() || !opacity_result.has_value()) {
        assert(false && "animatable property registration failed");
        return Layer{};
    }
    node.position = std::move(position_result).take_value();
    node.opacity = std::move(opacity_result).take_value();
    position_property_indices_.back() = node.position.property_index();
    opacity_property_indices_.back() = node.opacity.property_index();

    return Layer{this, handle};
}

void LayerTree::commit() {
    CA_ASSERT_UI_THREAD();

    auto packet = std::make_shared<FramePacket>();
    packet->bounds_ = bounds_;
    packet->corner_radii_ = corner_radii_;
    packet->background_colors_ = background_colors_;
    packet->transforms_ = transforms_;
    packet->position_property_indices_ = position_property_indices_;
    packet->opacity_property_indices_ = opacity_property_indices_;
    packet->display_lists_ = display_lists_;
    packet->parent_indices_ = parent_indices_;
    packet->identifiers_.reserve(nodes_.size());
    for (const Node& node : nodes_) {
        packet->identifiers_.push_back(node.identifier);
    }

    published_packet_.store(std::move(packet), std::memory_order_release);
}

// --- Storage accessors (the Layer facade's window into the pools) ----------

LayerTree::Node& LayerTree::node(LayerHandle handle) {
    if (!pool_.is_valid(handle)) {
        // A stale handle is a programming error; fail loudly in debug and
        // fall back to the root (a valid row) in release rather than
        // corrupting a sibling (the generation check is what makes this
        // diagnosable — docs/02 §4.1).
        assert(false && "stale layer handle");
        return nodes_.front();
    }
    return nodes_[handle.index()];
}

void LayerTree::set_layer_bounds(LayerHandle handle, geometry::Rect bounds) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        bounds_[handle.index()] = bounds;
    }
}

void LayerTree::set_layer_transform(
    LayerHandle handle, const geometry::AffineTransform& transform) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        transforms_[handle.index()] = transform;
    }
}

void LayerTree::set_layer_corner_radius(LayerHandle handle, float radius) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        corner_radii_[handle.index()] = radius;
    }
}

void LayerTree::set_layer_background_color(LayerHandle handle,
                                           graphics::Color color) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        background_colors_[handle.index()] = color;
    }
}

void LayerTree::set_layer_display_list(LayerHandle handle,
                                       graphics::DisplayList display_list) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        display_lists_[handle.index()] = std::move(display_list);
    }
}

void LayerTree::set_layer_identifier(LayerHandle handle,
                                     core::Identifier identifier) {
    CA_ASSERT_UI_THREAD();
    if (pool_.is_valid(handle)) {
        node(handle).identifier = identifier;
    }
}

// ---------------------------------------------------------------------------
// Layer facade
// ---------------------------------------------------------------------------

void Layer::set_bounds(geometry::Rect bounds) {
    if (tree_ != nullptr) {
        tree_->set_layer_bounds(handle_, bounds);
    }
}

geometry::Rect Layer::bounds() const {
    if (tree_ == nullptr || !tree_->pool_.is_valid(handle_)) {
        return {};
    }
    return tree_->bounds_[handle_.index()];
}

animation::AnimatablePoint& Layer::position() {
    return tree_->node(handle_).position;
}

animation::AnimatableFloat& Layer::opacity() {
    return tree_->node(handle_).opacity;
}

void Layer::set_transform(const geometry::AffineTransform& transform) {
    if (tree_ != nullptr) {
        tree_->set_layer_transform(handle_, transform);
    }
}

geometry::AffineTransform Layer::transform() const {
    if (tree_ == nullptr || !tree_->pool_.is_valid(handle_)) {
        return geometry::AffineTransform::identity();
    }
    return tree_->transforms_[handle_.index()];
}

void Layer::set_corner_radius(float radius) {
    if (tree_ != nullptr) {
        tree_->set_layer_corner_radius(handle_, radius);
    }
}

float Layer::corner_radius() const {
    if (tree_ == nullptr || !tree_->pool_.is_valid(handle_)) {
        return 0.0f;
    }
    return tree_->corner_radii_[handle_.index()];
}

void Layer::set_background_color(graphics::Color color) {
    if (tree_ != nullptr) {
        tree_->set_layer_background_color(handle_, color);
    }
}

graphics::Color Layer::background_color() const {
    if (tree_ == nullptr || !tree_->pool_.is_valid(handle_)) {
        return graphics::Color::clear();
    }
    return tree_->background_colors_[handle_.index()];
}

void Layer::set_display_list(graphics::DisplayList display_list) {
    if (tree_ != nullptr) {
        tree_->set_layer_display_list(handle_, std::move(display_list));
    }
}

const graphics::DisplayList* Layer::display_list() const {
    if (tree_ == nullptr || !tree_->pool_.is_valid(handle_)) {
        return nullptr;
    }
    const graphics::DisplayList& list = tree_->display_lists_[handle_.index()];
    return list.is_empty() ? nullptr : &list;
}

void Layer::add_sublayer(Layer child) {
    if (tree_ == nullptr || !child.is_valid() || child.tree_ != tree_) {
        return;
    }
    CA_ASSERT_UI_THREAD();
    // A layer has one parent; detach from any previous one first.
    child.remove_from_superlayer();
    auto& parent_node = tree_->node(handle_);
    parent_node.sublayers.push_back(child.handle_);
    tree_->parent_indices_[child.handle_.index()] = handle_.index();
}

void Layer::remove_from_superlayer() {
    if (tree_ == nullptr) {
        return;
    }
    CA_ASSERT_UI_THREAD();
    const std::uint32_t parent_index = tree_->parent_indices_[handle_.index()];
    if (parent_index == k_no_parent) {
        return;
    }
    auto& parent_node = tree_->nodes_[parent_index];
    const auto it = std::find(parent_node.sublayers.begin(),
                              parent_node.sublayers.end(), handle_);
    if (it != parent_node.sublayers.end()) {
        parent_node.sublayers.erase(it);
    }
    tree_->parent_indices_[handle_.index()] = k_no_parent;
}

std::span<const LayerHandle> Layer::sublayers() const {
    if (tree_ == nullptr) {
        return {};
    }
    const auto& node = tree_->node(handle_);
    return {node.sublayers.data(), node.sublayers.size()};
}

void Layer::set_identifier(core::Identifier identifier) {
    if (tree_ != nullptr) {
        tree_->set_layer_identifier(handle_, identifier);
    }
}

core::Identifier Layer::identifier() const {
    if (tree_ == nullptr) {
        return core::Identifier{};
    }
    return tree_->node(handle_).identifier;
}

} // namespace ca::layer
