// SDL3 renderer backend implementation.

#include "sdl3_gpu_device.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "gpu/backend.hpp"  // ca::gpu::backend::register_gpu_backend
#include "calcium/core/result.hpp"
#include "calcium/core/time.hpp"
#include "calcium/geometry/point.hpp"

namespace ca::gpu {

namespace {

// The float variant of SDL_SetRenderDrawColor takes sRGB floats in [0, 1] —
// exactly what the compositor's clear color is, so no byte quantization here.
void set_clear_color(SDL_Renderer* renderer, const float clear_color[4]) {
    SDL_SetRenderDrawColorFloat(renderer, clear_color[0], clear_color[1],
                                clear_color[2], clear_color[3]);
}

// Builds "what: <SDL error>" and clears the SDL error slot, so a stale
// message never bleeds into a later unrelated failure.
std::string sdl_error_message(const char* what) {
    std::string message = what;
    const char* sdl_error = SDL_GetError();
    if (sdl_error != nullptr && *sdl_error != '\0') {
        message += ": ";
        message += sdl_error;
    }
    SDL_ClearError();
    return message;
}

} // namespace

// ---------------------------------------------------------------------------
// Sdl3Swapchain
// ---------------------------------------------------------------------------

void Sdl3Swapchain::resize(geometry::Size size) {
    size_pixels_ = size;
    // The SDL renderer tracks the window's drawable size itself; nothing to
    // recreate. The stored size is what size() reports to the compositor.
    (void)renderer_;
    (void)window_;
}

// ---------------------------------------------------------------------------
// Sdl3RenderPass
// ---------------------------------------------------------------------------

void Sdl3RenderPass::end_and_present() {
    // Submission happens BEFORE the present: SDL_RenderPresent is both submit
    // and pacing wait in one call, and the interface's timing model places
    // the pacing wait before acquired_at (of the *next* frame), not after
    // submitted_at. Recording submission here keeps `submitted_at −
    // acquired_at` the frame's true work — the compositor's measured stage
    // cost excludes the vsync wait, matching the D3D12 backend's numbers.
    submitted_at_ = core::Timestamp::now();

    // With vsync enabled the present blocks until scanout — the pacing anchor
    // the compositor's presentation-time prediction extrapolates from. A
    // failed present leaves the renderer in an unusable state; record the
    // reason so the next begin_clear_pass reports it (end_and_present is void
    // by design, the interface's failure channel is the next acquire).
    if (!SDL_RenderPresent(renderer_)) {
        *present_error_ = "SDL_RenderPresent failed: ";
        const char* sdl_error = SDL_GetError();
        if (sdl_error != nullptr) {
            *present_error_ += sdl_error;
        }
        SDL_ClearError();
    }
}

// ---------------------------------------------------------------------------
// Sdl3GpuDevice
// ---------------------------------------------------------------------------

core::Result<std::unique_ptr<GraphicsDevice>> Sdl3GpuDevice::create(
    const Configuration&) {
    return core::Result<std::unique_ptr<GraphicsDevice>>{
        std::unique_ptr<GraphicsDevice>{new Sdl3GpuDevice()}};
}

Sdl3GpuDevice::~Sdl3GpuDevice() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
}

GraphicsDevice::AdapterInfo Sdl3GpuDevice::adapter_info() const {
    return AdapterInfo{
        .name = adapter_name_,
        .is_hardware = is_hardware_,
    };
}

core::Result<void> Sdl3GpuDevice::create_renderer(SDL_Window* window) {
    // SDL's best available driver first (on Windows that is SDL's own
    // accelerated driver — SDL's choice, not Calcium's); the software driver
    // is the documented fallback so the MVP clears with no GPU dependency.
    renderer_ = SDL_CreateRenderer(window, nullptr);
    if (renderer_ != nullptr) {
        is_hardware_ = true;
    } else {
        SDL_ClearError();
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        renderer_ = SDL_CreateRenderer(window, nullptr);
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        if (renderer_ == nullptr) {
            return core::Result<void>{
                core::ErrorCode::backend_failure,
                sdl_error_message("no SDL renderer driver available")};
        }
        is_hardware_ = false;
    }

    adapter_name_ = SDL_GetRendererName(renderer_);
    if (adapter_name_.empty()) {
        adapter_name_ = "unknown";
    }

    // The compositor's timing model assumes vsync-paced present. Drivers that
    // cannot honor it report here; the loop then runs unpaced (honest, but
    // visibly wrong cadence — the clear-color cycle's stutter exposes it).
    if (!SDL_SetRenderVSync(renderer_, 1)) {
        std::fprintf(stderr,
                     "gpu_sdl3: vsync unsupported by renderer '%s' — "
                     "the frame loop will not be display-paced\n",
                     adapter_name_.c_str());
        SDL_ClearError();
    }
    return core::Result<void>{};
}

core::Result<std::unique_ptr<Swapchain>> Sdl3GpuDevice::create_swapchain(
    gpu::WindowHandle window_handle, geometry::Size initial_size) {
    auto* const window = reinterpret_cast<SDL_Window*>(window_handle);
    if (window == nullptr) {
        return core::Result<std::unique_ptr<Swapchain>>{
            core::ErrorCode::invalid_argument,
            "gpu_sdl3: null window handle"};
    }

    // The renderer is created on the first swapchain: GraphicsDevice::create
    // has no window, and the compositor calls this on the compositor thread,
    // which is also the thread that uses the renderer (SDL renderers are
    // single-threaded; docs/02-architecture.md §2.3).
    if (renderer_ == nullptr) {
        auto result = create_renderer(window);
        if (!result.has_value()) {
            return core::Result<std::unique_ptr<Swapchain>>{result.error()};
        }
    }

    // The SDL renderer follows the window's drawable size automatically;
    // the swapchain's size bookkeeping starts from the compositor's
    // device-pixel size (what resize() tracks from then on).
    return core::Result<std::unique_ptr<Swapchain>>{
        std::unique_ptr<Swapchain>{new Sdl3Swapchain(
            window, renderer_, initial_size, &present_error_)}};
}

core::Result<std::unique_ptr<RenderPass>> Sdl3GpuDevice::begin_clear_pass(
    Swapchain& swapchain, const float clear_color[4]) {
    // A failed present leaves the renderer dead; report it here, where the
    // compositor's failure path is (begin_clear_pass already surfaces errors
    // via Result — the interface's one failure channel).
    if (!present_error_.empty()) {
        core::ErrorCode code = core::ErrorCode::backend_failure;
        std::string message = present_error_;
        present_error_.clear();
        return core::Result<std::unique_ptr<RenderPass>>{code, message};
    }

    auto& sdl_swapchain = static_cast<Sdl3Swapchain&>(swapchain);
    SDL_Renderer* const renderer = sdl_swapchain.renderer();
    const core::Timestamp acquired_at = core::Timestamp::now();

    set_clear_color(renderer, clear_color);
    if (!SDL_RenderClear(renderer)) {
        const std::string message =
            sdl_error_message("SDL_RenderClear failed");
        return core::Result<std::unique_ptr<RenderPass>>{
            core::ErrorCode::backend_failure, message};
    }

    return core::Result<std::unique_ptr<RenderPass>>{
        std::unique_ptr<RenderPass>{new Sdl3RenderPass(
            renderer, &sdl_swapchain, &present_error_, acquired_at)}};
}

} // namespace ca::gpu

namespace ca::gpu::backend {

void register_sdl3_gpu_backend() {
    register_gpu_backend(
        [](const GraphicsDevice::Configuration& configuration) {
            return Sdl3GpuDevice::create(configuration);
        });
}

} // namespace ca::gpu::backend
