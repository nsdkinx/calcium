// SDL3 renderer backend implementation.

#include "sdl3_gpu_device.hpp"

#include <algorithm>
#include <cmath>
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

void set_draw_color(SDL_Renderer* renderer, const float color[4]) {
    SDL_SetRenderDrawColorFloat(renderer, color[0], color[1], color[2],
                                color[3]);
}

SDL_FRect to_frect(geometry::Rect rect) {
    return {rect.min_x(), rect.min_y(), rect.width(), rect.height()};
}

// The intersection of two rects; empty when they do not overlap.
geometry::Rect intersect(geometry::Rect a, geometry::Rect b) {
    const float left = std::max(a.min_x(), b.min_x());
    const float top = std::max(a.min_y(), b.min_y());
    const float right = std::min(a.max_x(), b.max_x());
    const float bottom = std::min(a.max_y(), b.max_y());
    if (right <= left || bottom <= top) {
        return {};
    }
    return geometry::Rect::from_edges(left, top, right, bottom);
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
// Sdl3DrawPass
// ---------------------------------------------------------------------------

void Sdl3DrawPass::clear(const float color[4]) {
    set_draw_color(renderer_, color);
    SDL_RenderClear(renderer_);
}

void Sdl3DrawPass::push_clip(geometry::Rect rect) {
    // The rasterizer converts clip rects to device space before calling;
    // here they are intersected with the current clip, so nested clips are
    // exact (SDL has a single render clip).
    const geometry::Rect next =
        clip_stack_.empty() ? rect : intersect(clip_stack_.back(), rect);
    clip_stack_.push_back(next);
    const SDL_Rect sdl_rect{static_cast<int>(std::floor(next.min_x())),
                            static_cast<int>(std::floor(next.min_y())),
                            static_cast<int>(std::ceil(next.width())),
                            static_cast<int>(std::ceil(next.height()))};
    // An empty intersection is expressed as a zero-area clip (nothing draws);
    // the rasterizer culls before emitting, so this is only a fallback.
    SDL_SetRenderClipRect(renderer_, &sdl_rect);
}

void Sdl3DrawPass::pop_clip() {
    clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        const geometry::Rect rect = clip_stack_.back();
        const SDL_Rect sdl_rect{static_cast<int>(std::floor(rect.min_x())),
                                static_cast<int>(std::floor(rect.min_y())),
                                static_cast<int>(std::ceil(rect.width())),
                                static_cast<int>(std::ceil(rect.height()))};
        SDL_SetRenderClipRect(renderer_, &sdl_rect);
    }
}

void Sdl3DrawPass::fill_rect(geometry::Rect rect, const float color[4]) {
    set_draw_color(renderer_, color);
    const SDL_FRect frect = to_frect(rect);
    SDL_RenderFillRect(renderer_, &frect);
}

void Sdl3DrawPass::fill_polygon(std::span<const geometry::Point> polygon,
                                const float color[4]) {
    // The contract (draw_pass.hpp): a simple polygon star-shaped with
    // respect to its own centroid — which the rasterizer's rounded-rect
    // outlines are (the squircle corners are concave by design). The fan
    // originates at the centroid, so it covers the polygon exactly; a fan
    // from an edge vertex would spill across concave corners.
    if (polygon.size() < 3) {
        return;
    }
    // Shoelace: twice the signed area and the centroid.
    double area2 = 0.0;
    double centroid_x = 0.0;
    double centroid_y = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const geometry::Point& a = polygon[i];
        const geometry::Point& b = polygon[(i + 1) % polygon.size()];
        const double cross =
            static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
        area2 += cross;
        centroid_x += (static_cast<double>(a.x) + b.x) * cross;
        centroid_y += (static_cast<double>(a.y) + b.y) * cross;
    }
    if (area2 == 0.0) {
        return;  // degenerate
    }
    centroid_x /= 3.0 * area2;
    centroid_y /= 3.0 * area2;

    vertices_.clear();
    // Fan: (C, V_i, V_{i+1}) for every edge — three vertices per triangle.
    vertices_.reserve(polygon.size() * 3);
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const geometry::Point& b = polygon[(i + 1) % polygon.size()];
        for (const auto& corner :
             {geometry::Point{static_cast<float>(centroid_x),
                              static_cast<float>(centroid_y)},
              polygon[i], b}) {
            vertices_.push_back(Vertex{
                .position = {corner.x, corner.y},
                .color = {color[0], color[1], color[2], color[3]},
            });
        }
    }
    emit_triangles(vertices_);
}

void Sdl3DrawPass::emit_triangles(std::span<const Vertex> vertices) {
    if (vertices.empty()) {
        return;
    }
    // SDL_BLENDMODE_BLEND blends as dstRGB = srcRGB·srcA + dst·(1−srcA) —
    // premultiplied-alpha blending with straight-alpha inputs, which is what
    // the rasterizer supplies. The blend mode is set once per renderer in
    // create_renderer and applies to geometry as well as fill rects.
    if (!SDL_RenderGeometryRaw(
            renderer_, nullptr,
            &vertices[0].position.x, sizeof(Vertex),
            &vertices[0].color, sizeof(Vertex),
            nullptr, 0,
            static_cast<int>(vertices.size()),
            nullptr, 0, 0)) {
        // A failed geometry call leaves the renderer in an unusable state;
        // record it the way present failures are recorded (the next acquire
        // surfaces it through the Result channel).
        if (present_error_->empty()) {
            *present_error_ = sdl_error_message("SDL_RenderGeometryRaw failed");
        }
    }
}

void Sdl3DrawPass::end_and_present() {
    // Same timing model as Sdl3RenderPass: submission is recorded before the
    // present, which doubles as the pacing wait (see its comment).
    submitted_at_ = core::Timestamp::now();
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

    // Alpha blending for the draw pass: SDL_BLENDMODE_BLEND is
    // premultiplied-alpha blending with straight-alpha inputs
    // (dstRGB = srcRGB·srcA + dst·(1−srcA)), which is exactly the model the
    // rasterizer supplies. The mode is renderer-wide; the clear pass is
    // unaffected (SDL_RenderClear does not blend).
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

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

core::Result<std::unique_ptr<DrawPass>> Sdl3GpuDevice::begin_draw_pass(
    Swapchain& swapchain) {
    if (!present_error_.empty()) {
        core::ErrorCode code = core::ErrorCode::backend_failure;
        std::string message = present_error_;
        present_error_.clear();
        return core::Result<std::unique_ptr<DrawPass>>{code, message};
    }

    auto& sdl_swapchain = static_cast<Sdl3Swapchain&>(swapchain);
    SDL_Renderer* const renderer = sdl_swapchain.renderer();
    const core::Timestamp acquired_at = core::Timestamp::now();

    return core::Result<std::unique_ptr<DrawPass>>{
        std::unique_ptr<DrawPass>{new Sdl3DrawPass(
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
