#pragma once

#include "calcium/core/export.hpp"

// The GPU device: the Level-1 doorway (docs/02-architecture.md §6).
//
// M1 ships the surface the compositor needs to clear and present a window:
// device creation, swapchains, and clear render passes. The full command
// encoder — pipelines, textures, shader modules, custom passes — lands with
// the display-list rasterizer in M2, on this same interface, so the porting
// surface stays exactly what it is today: a handful of virtuals.
//
// Backends are compiled into the umbrella and register themselves;
// `GraphicsDevice::create` fails with CA_ERROR_UNSUPPORTED when no backend is
// linked, so an unconfigured build fails loudly at the call site rather than
// at first present. SDL3's renderer (gpu_sdl3) is the only backend until the
// MVP; D3D12, Metal and Vulkan backends slot in behind this same interface
// after the core stabilizes (docs/06-roadmap.md M1).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "calcium/core/result.hpp"
#include "calcium/geometry/point.hpp"

namespace ca::gpu {

class Swapchain;
class RenderPass;

/// The platform window's native handle, opaque here (Level 1 depends only on
/// core and geometry — the DAG in docs/02-architecture.md §1; the compositor
/// converts from platform::NativeWindowHandle at the boundary).
using WindowHandle = std::uint64_t;

class GraphicsDevice {
public:
    struct Configuration {
        /// Debug layer / validation (backend-specific; gpu_sdl3 ignores it).
        bool enable_debug_layer = false;
        /// Per-frame GPU timestamps (adds a query pass per frame).
        bool enable_gpu_timing = false;
    };

    /// Creates a device with whatever backend is linked.
    [[nodiscard]] static core::Result<std::unique_ptr<GraphicsDevice>> create(
        const Configuration& configuration);

    virtual ~GraphicsDevice() = default;

    struct AdapterInfo {
        std::string name;
        bool is_hardware = true;
    };
    [[nodiscard]] virtual AdapterInfo adapter_info() const = 0;

    /// The backend's API name: "sdl3" today; "d3d12"/"metal"/"vulkan" when
    /// those backends land after the MVP.
    [[nodiscard]] virtual std::string_view api_name() const noexcept = 0;

    /// Binds a swapchain to a platform window's native handle. The swapchain
    /// is owned by the compositor and presents on the compositor thread.
    [[nodiscard]] virtual core::Result<std::unique_ptr<Swapchain>>
    create_swapchain(WindowHandle window_handle,
                     geometry::Size initial_size) = 0;

    /// Begins the frame: acquires the swapchain's back buffer, clears it to
    /// `clear_color` (straight-alpha sRGB, premultiplied by the compositor
    /// when the pipeline demands it), and returns a pass that
    /// `end_and_present()`s. One frame at a time — the compositor never
    /// begins a second pass before presenting the first.
    [[nodiscard]] virtual core::Result<std::unique_ptr<RenderPass>>
    begin_clear_pass(Swapchain& swapchain, const float clear_color[4]) = 0;
};

} // namespace ca::gpu
