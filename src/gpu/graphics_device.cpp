#include "calcium/gpu/graphics_device.hpp"

#include <memory>

#include "backend.hpp"

namespace ca::gpu {

// ---------------------------------------------------------------------------
// Backend registry
// ---------------------------------------------------------------------------

namespace backend {

namespace {
DeviceFactory g_device_factory = nullptr;
}

void register_gpu_backend(DeviceFactory factory) noexcept {
    g_device_factory = factory;
}

DeviceFactory gpu_backend_factory() noexcept {
    return g_device_factory;
}

} // namespace backend

core::Result<std::unique_ptr<GraphicsDevice>> GraphicsDevice::create(
    const Configuration& configuration) {
    const backend::DeviceFactory factory = backend::gpu_backend_factory();
    if (factory == nullptr) {
        return core::Result<std::unique_ptr<GraphicsDevice>>{
            core::ErrorCode::unsupported,
            "no GPU backend linked (CALCIUM_GPU_* is off)"};
    }
    return factory(configuration);
}

} // namespace ca::gpu
