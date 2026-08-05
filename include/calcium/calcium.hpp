#pragma once

// Calcium — GPU-composited, physics-native application framework.
//
// Umbrella header. Convenient for applications; individual modules should
// include only what they use so the level DAG stays visible.
//
// Documentation lives in docs/:
//   00-overview.md              goals, honest constraints
//   01-principles.md            the 14 architectural principles
//   02-architecture.md          the five-level stack, threading, frame pipeline
//   04-public-api.md            API design at every level
//   05-animation-and-twell.md   the animation engine

#include <cstdint>
#include <string_view>

// ---- Level 0: core ----
#include "calcium/core/export.hpp"
#include "calcium/core/arena_allocator.hpp"
#include "calcium/core/handle.hpp"
#include "calcium/core/identifier.hpp"
#include "calcium/core/interned_string.hpp"
#include "calcium/core/result.hpp"
#include "calcium/core/small_vector.hpp"
#include "calcium/core/thread_affinity.hpp"
#include "calcium/core/time.hpp"

// ---- Level 2: geometry ----
#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/path.hpp"
#include "calcium/geometry/path_builder.hpp"
#include "calcium/geometry/point.hpp"
#include "calcium/geometry/quaternion.hpp"
#include "calcium/geometry/rect.hpp"
#include "calcium/geometry/rounded_rectangle.hpp"
#include "calcium/geometry/transform_3d.hpp"

// ---- Level 2: graphics ----
#include "calcium/graphics/color.hpp"
#include "calcium/graphics/display_list.hpp"
#include "calcium/graphics/paint.hpp"

// ---- Level 3: animation, layer ----
#include "calcium/animation/animatable_property.hpp"
#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/animation/motion.hpp"
#include "calcium/animation/motion_scheme.hpp"
#include "calcium/animation/spring_configuration.hpp"
#include "calcium/animation/transaction.hpp"
#include "calcium/layer/layer.hpp"
#include "calcium/layer/layer_tree.hpp"

// ---- Level 1: gpu, platform ----
#include "calcium/gpu/draw_pass.hpp"
#include "calcium/gpu/graphics_device.hpp"
#include "calcium/gpu/render_pass.hpp"
#include "calcium/gpu/swapchain.hpp"
#include "calcium/platform/application.hpp"
#include "calcium/platform/display.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

// Modules are added here as their milestones land (docs/06-roadmap.md):
//   text — M3; animation, layer, layout, view — M2, M4
//   widget, compose                — M5, M8

namespace ca {

struct VersionInfo {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string_view description;
};

/// Version of the linked Calcium library, which may differ from the headers
/// compiled against.
[[nodiscard]] CALCIUM_API VersionInfo library_version() noexcept;

/// Namespace aliases. Expected in application code; see
/// docs/03-project-structure.md section 1.
namespace core {}
namespace geometry {}

} // namespace ca

namespace ca_aliases_documentation {
// namespace gfx  = ca::graphics;
// namespace anim = ca::animation;
// namespace geo  = ca::geometry;
}
