#pragma once

// The layer tree: pooled, index-addressed storage behind Layer handles
// (docs/02-architecture.md §4.1).
//
// The tree is mutated on the UI thread and READ by the compositor through
// immutable FramePackets: `commit()` copies the tree's current static state
// into a fresh packet and publishes it lock-free (atomic shared_ptr — the
// compositor retains the packet it rendered, so a stale packet can be
// re-composited with fresh animation values, docs/02 §2.2). Animating a
// compositor-resolvable property (position, opacity) never requires a
// commit; mutating anything else (bounds, transform, color, radius, content,
// topology) requires one, followed by a repaint request.
//
// The FramePacket is true structure-of-arrays — the compositor's resolve
// loop walks parallel arrays, which is the point of §4.1's layout. The
// UI-side storage mirrors it, except the per-layer property objects, which
// live in a side pool with stable addresses so `Layer::position()` returns a
// reference that survives pool growth.
//
// Thread contract (docs/02 §2.3): UI thread mutates, compositor reads.
// M2 runs the model on the application's main thread — the dedicated UI
// thread lands with the event-queue milestone; the tree asserts the UI role
// either way.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/core/handle.hpp"
#include "calcium/core/identifier.hpp"
#include "calcium/core/result.hpp"
#include "calcium/graphics/color.hpp"
#include "calcium/graphics/display_list.hpp"
#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/rect.hpp"
#include "calcium/layer/layer.hpp"

namespace ca::layer {

/// One committed frame's static layer state — the packet the compositor
/// re-composites with fresh presentation values each vsync (docs/02 §2.2).
/// Immutable by construction; the compositor never mutates one.
class FramePacket {
public:
    FramePacket() = default;

    [[nodiscard]] std::size_t layer_count() const noexcept {
        return bounds_.size();
    }

    // SoA rows (docs/02 §4.1): parallel arrays indexed by layer row, in
    // draw order (parents before children, siblings in insertion order).
    [[nodiscard]] std::span<const geometry::Rect> bounds() const noexcept {
        return bounds_;
    }
    [[nodiscard]] std::span<const float> corner_radii() const noexcept {
        return corner_radii_;
    }
    [[nodiscard]] std::span<const graphics::Color>
    background_colors() const noexcept {
        return background_colors_;
    }
    [[nodiscard]] std::span<const geometry::AffineTransform>
    transforms() const noexcept {
        return transforms_;
    }
    /// Coordinator snapshot indices — the compositor reads the presentation
    /// values from there (position 2D, opacity 1D).
    [[nodiscard]] std::span<const std::uint32_t>
    position_property_indices() const noexcept {
        return position_property_indices_;
    }
    [[nodiscard]] std::span<const std::uint32_t>
    opacity_property_indices() const noexcept {
        return opacity_property_indices_;
    }
    [[nodiscard]] std::span<const graphics::DisplayList>
    display_lists() const noexcept {
        return display_lists_;
    }
    /// The parent's row index; 0xFFFFFFFF for the root.
    [[nodiscard]] std::span<const std::uint32_t> parent_indices() const noexcept {
        return parent_indices_;
    }
    [[nodiscard]] std::span<const core::Identifier> identifiers() const noexcept {
        return identifiers_;
    }

private:
    friend class LayerTree;
    std::vector<geometry::Rect> bounds_;
    std::vector<float> corner_radii_;
    std::vector<graphics::Color> background_colors_;
    std::vector<geometry::AffineTransform> transforms_;
    std::vector<std::uint32_t> position_property_indices_;
    std::vector<std::uint32_t> opacity_property_indices_;
    std::vector<graphics::DisplayList> display_lists_;
    std::vector<std::uint32_t> parent_indices_;
    std::vector<core::Identifier> identifiers_;
};

/// The tree. One per window in the full design; M2 has one tree, whose root
/// layer spans the window.
class LayerTree {
public:
    struct Configuration {
        /// Registers every layer's animatable properties with this
        /// coordinator (the one the compositor ticks). Required.
        animation::AnimationCoordinator* coordinator = nullptr;
    };

    [[nodiscard]] static core::Result<std::unique_ptr<LayerTree>> create(
        const Configuration& configuration);

    ~LayerTree();

    LayerTree(LayerTree&&) = delete;
    LayerTree& operator=(LayerTree&&) = delete;
    LayerTree(const LayerTree&) = delete;
    LayerTree& operator=(const LayerTree&) = delete;

    // === UI THREAD ONLY ===

    /// Creates a layer and returns its handle. Layers live for the tree's
    /// lifetime in M2 (there is no destroy; the generation-checked handles
    /// are what the future destroy slots in behind).
    [[nodiscard]] Layer create_layer();

    /// The root layer (row 0 — created by the tree). The compositor draws
    /// it and its subtree; give it the window's bounds.
    [[nodiscard]] Layer root_layer() const noexcept { return root_layer_; }

    /// Publishes the tree's current static state to the compositor. Required
    /// after mutating bounds/transform/color/radius/content/topology; NOT
    /// needed for position/opacity (those resolve per frame from the
    /// coordinator's snapshot).
    void commit();

    // === COMPOSITOR THREAD ONLY ===

    /// The newest committed packet; null until the first commit. Lock-free.
    [[nodiscard]] std::shared_ptr<const FramePacket> latest_packet()
        const noexcept {
        return published_packet_.load(std::memory_order_acquire);
    }

private:
    explicit LayerTree(Configuration configuration);

    // Per-layer property objects live at stable addresses (std::deque) so
    // `Layer::position()`/`opacity()` references survive pool growth; the
    // parallel vectors below are the SoA pools (docs/02 §4.1).
    struct Node {
        animation::AnimatablePoint position;
        animation::AnimatableFloat opacity;
        std::vector<LayerHandle> sublayers;
        core::Identifier identifier;
    };

    friend class Layer;  // the facade resolves through these storage methods

    // Storage accessors for the Layer facade (validating + role-asserting).
    [[nodiscard]] Node& node(LayerHandle handle);
    [[nodiscard]] const Node& node(LayerHandle handle) const;
    void set_layer_bounds(LayerHandle handle, geometry::Rect bounds);
    void set_layer_transform(LayerHandle handle,
                             const geometry::AffineTransform& transform);
    void set_layer_corner_radius(LayerHandle handle, float radius);
    void set_layer_background_color(LayerHandle handle, graphics::Color color);
    void set_layer_display_list(LayerHandle handle,
                                graphics::DisplayList display_list);
    void set_layer_identifier(LayerHandle handle, core::Identifier identifier);

    Configuration configuration_;
    core::HandlePool<LayerTag> pool_;
    std::deque<Node> nodes_;
    std::vector<geometry::Rect> bounds_;
    std::vector<float> corner_radii_;
    std::vector<graphics::Color> background_colors_;
    std::vector<geometry::AffineTransform> transforms_;
    std::vector<std::uint32_t> position_property_indices_;
    std::vector<std::uint32_t> opacity_property_indices_;
    std::vector<graphics::DisplayList> display_lists_;
    std::vector<std::uint32_t> parent_indices_;
    Layer root_layer_;
    std::atomic<std::shared_ptr<const FramePacket>> published_packet_;
};

} // namespace ca::layer
