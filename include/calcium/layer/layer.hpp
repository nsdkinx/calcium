#pragma once

// Layers: the composited tree (docs/02-architecture.md §4).
//
// M2 carries the minimal surface the vertical slice needs: bounds, position
// and opacity as animatable properties, a static 2D transform, a
// compositor-resolvable background color and corner radius, a sealed display
// list, and tree operations. The full surface — anchor point (the M2 anchor
// is the top-left), decomposed Transform3D animation, masks, filters,
// rasterization policy, delegate drawing — lands with the view milestone
// (docs/04-public-api.md §2.2).
//
// A Layer is a lightweight handle into the tree's pooled storage
// (docs/02 §4.1): it carries a `LayerTree*` plus a generation-checked
// `LayerHandle`. Every access resolves through the tree, which validates the
// handle in debug and release — a stale handle is detected, not dereferenced.
// Layers are UI-thread objects (docs/02 §2.3).
//
// Coordinate model: the layer's model space has its origin at the layer's
// top-left corner (anchor {0, 0}); the layer's position places that origin
// in the superlayer's space. A display list records in the layer's model
// space, so bounds {0, 0, w, h} is where the background fill lands.

#include <cstdint>
#include <span>
#include <vector>

#include "calcium/animation/animatable_property.hpp"
#include "calcium/core/handle.hpp"
#include "calcium/core/identifier.hpp"
#include "calcium/graphics/color.hpp"
#include "calcium/graphics/display_list.hpp"
#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/point.hpp"
#include "calcium/geometry/rect.hpp"

namespace ca::layer {

class LayerTree;

struct LayerTag;
using LayerHandle = core::Handle<LayerTag>;

class Layer {
public:
    /// An invalid layer (the default); `is_valid()` is false.
    Layer() = default;

    [[nodiscard]] bool is_valid() const noexcept {
        return tree_ != nullptr && handle_.is_valid();
    }
    [[nodiscard]] LayerHandle handle() const noexcept { return handle_; }

    // --- Geometry (model space) ---
    void set_bounds(geometry::Rect bounds);
    [[nodiscard]] geometry::Rect bounds() const;

    // --- Compositor-resolvable animatables (docs/02-architecture.md §2.2) ---
    // Animating these NEVER invalidates a committed packet: the compositor
    // re-resolves them from the coordinator's snapshot each vsync.
    [[nodiscard]] animation::AnimatablePoint& position();
    [[nodiscard]] animation::AnimatableFloat& opacity();

    // --- Compositor-resolvable statics ---
    /// The static transform (applied under `position`; M2). Animated
    /// Transform3D (decomposed) lands with the view milestone.
    void set_transform(const geometry::AffineTransform& transform);
    [[nodiscard]] geometry::AffineTransform transform() const;
    /// The uniform corner radius of the background fill.
    void set_corner_radius(float radius);
    [[nodiscard]] float corner_radius() const;
    void set_background_color(graphics::Color color);
    [[nodiscard]] graphics::Color background_color() const;

    // --- Content: the doorway to Level 2 (P7) ---
    /// A pre-recorded, sealed display list drawn in the layer's model space.
    /// Record-affecting: changing it requires a commit.
    void set_display_list(graphics::DisplayList display_list);
    [[nodiscard]] const graphics::DisplayList* display_list() const;

    // --- Tree ---
    void add_sublayer(Layer child);
    void remove_from_superlayer();
    [[nodiscard]] std::span<const LayerHandle> sublayers() const;

    // --- Identity (P12) ---
    void set_identifier(core::Identifier identifier);
    [[nodiscard]] core::Identifier identifier() const;

private:
    friend class LayerTree;
    Layer(LayerTree* tree, LayerHandle handle) : tree_(tree), handle_(handle) {}

    LayerTree* tree_ = nullptr;
    LayerHandle handle_;
};

} // namespace ca::layer
