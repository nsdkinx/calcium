#include "backend_registration.hpp"

#include "backends/gpu_d3d12/d3d12_device.hpp"
#include "backends/platform_sdl3/sdl3_platform.hpp"
#include "gpu/backend.hpp"
#include "platform/backend.hpp"

namespace ca {

void calcium_register_backends() {
    platform::backend::register_sdl3_backend();
    gpu::backend::register_d3d12_backend();
}

} // namespace ca
