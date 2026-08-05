// The GPU backend registry (internal).
//
// Backends (D3D12, Metal, Vulkan) are compiled into the umbrella library and
// register their device factories here at static-init time; GraphicsDevice::
// create consults the registry. An unconfigured build fails with
// CA_ERROR_UNSUPPORTED at the create call — loudly, at the call site.

#pragma once

#include <functional>
#include <memory>

#include "calcium/gpu/graphics_device.hpp"

namespace ca::gpu::backend {

using DeviceFactory = std::function<core::Result<std::unique_ptr<GraphicsDevice>>(
    const GraphicsDevice::Configuration&)>;

/// Registers a device factory. Called by the umbrella at static init;
/// not thread-safe (startup only).
void register_gpu_backend(DeviceFactory factory) noexcept;

/// The registered factory, or nullptr when no backend is linked.
DeviceFactory gpu_backend_factory() noexcept;

} // namespace ca::gpu::backend
