// SDL3 renderer backend — internal, never public (docs/02-architecture.md §6).
//
// Implements GraphicsDevice/Swapchain/RenderPass on SDL3's renderer API — the
// only GPU backend until the MVP lands (SDL3-only policy, docs/06-roadmap.md
// M1). SDL3 picks the renderer driver (on Windows an accelerated driver that
// SDL owns internally); the software driver is the automatic fallback, so the
// clear-color path has no hard GPU requirement.
//
// The frame loop:
//
//   SDL_RenderPresent (previous frame)  → blocks on vsync (pacing anchor)
//   begin_clear_pass: SDL_RenderClear   → acquired_at
//   end_and_present:  SDL_RenderPresent → submitted_at, vsync-locked scanout
//
// The renderer is created lazily at the first create_swapchain: GraphicsDevice
// has no window at create() time, and the swapchain's window arrives through
// the opaque WindowHandle. SDL renderers must be created and used from one
// thread; the compositor satisfies this by bringing the device up on the
// compositor thread (docs/02-architecture.md §2.3).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "calcium/gpu/draw_pass.hpp"
#include "calcium/gpu/graphics_device.hpp"
#include "calcium/gpu/render_pass.hpp"
#include "calcium/gpu/swapchain.hpp"

namespace ca::gpu::backend {
// Called by the umbrella (src/calcium.cpp) at startup.
void register_sdl3_gpu_backend();
} // namespace ca::gpu::backend

namespace ca::gpu {

// The swapchain over the SDL renderer. The renderer belongs to the device and
// is shared; the swapchain adds the window binding and size bookkeeping (the
// renderer follows the window's size automatically — resize() just tracks it).
class Sdl3Swapchain final : public Swapchain {
public:
    Sdl3Swapchain(SDL_Window* window, SDL_Renderer* renderer,
                  geometry::Size size_pixels, std::string* present_error)
        : window_(window), renderer_(renderer), size_pixels_(size_pixels),
          present_error_(present_error) {}

    [[nodiscard]] geometry::Size size() const override { return size_pixels_; }
    void resize(geometry::Size size) override;
    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept override {
        // SDL keeps its internal buffer count opaque; two is the value the
        // Sdl3DisplayTiming extrapolation model is calibrated against
        // (t_present = t_now + (frames_in_flight + 1) × vsync).
        return 2;
    }

    [[nodiscard]] SDL_Window* native_window() const noexcept { return window_; }
    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    geometry::Size size_pixels_;
    // Points at the device's present-error slot; end_and_present records
    // failures there so the next begin_clear_pass can surface them.
    std::string* present_error_ = nullptr;
};

class Sdl3RenderPass final : public RenderPass {
public:
    Sdl3RenderPass(SDL_Renderer* renderer, Sdl3Swapchain* swapchain,
                   std::string* present_error, core::Timestamp acquired_at)
        : renderer_(renderer), swapchain_(swapchain),
          present_error_(present_error), acquired_at_(acquired_at) {}

    void end_and_present() override;

    [[nodiscard]] core::Timestamp acquired_at() const noexcept override {
        return acquired_at_;
    }
    [[nodiscard]] core::Timestamp submitted_at() const noexcept override {
        return submitted_at_;
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    Sdl3Swapchain* swapchain_ = nullptr;
    std::string* present_error_ = nullptr;
    core::Timestamp acquired_at_;
    core::Timestamp submitted_at_;
};

// The M2 draw pass: same acquire/present lifecycle as Sdl3RenderPass plus
// primitive fills. Draw calls are recorded into the SDL render queue in
// device space (the rasterizer maps model space); clip state is a stack the
// pass owns (SDL's render clip is a single rect, so the stack lives here).
//
// The vertex scratch buffer is reserved once and reused, so steady-state
// frames never allocate (P8 — the allocation sentinel gate).
class Sdl3DrawPass final : public DrawPass {
public:
    static constexpr std::size_t k_max_clip_depth = 16;
    static constexpr std::size_t k_scratch_capacity = 512;

    Sdl3DrawPass(SDL_Renderer* renderer, Sdl3Swapchain* swapchain,
                 std::string* present_error, core::Timestamp acquired_at)
        : renderer_(renderer), swapchain_(swapchain),
          present_error_(present_error), acquired_at_(acquired_at) {
        vertices_.reserve(k_scratch_capacity);
        clip_stack_.reserve(k_max_clip_depth);
    }

    void clear(const float color[4]) override;
    void push_clip(geometry::Rect rect) override;
    void pop_clip() override;
    void fill_rect(geometry::Rect rect, const float color[4]) override;
    void fill_polygon(std::span<const geometry::Point> polygon,
                      const float color[4]) override;
    void end_and_present() override;

    [[nodiscard]] core::Timestamp acquired_at() const noexcept override {
        return acquired_at_;
    }
    [[nodiscard]] core::Timestamp submitted_at() const noexcept override {
        return submitted_at_;
    }

private:
    // Interleaved position+color vertex — SDL_RenderGeometryRaw's stride
    // model (the xy array and the color array both advance by this stride).
    struct Vertex {
        SDL_FPoint position;
        SDL_FColor color;
    };
    void emit_triangles(std::span<const Vertex> vertices);

    SDL_Renderer* renderer_ = nullptr;
    Sdl3Swapchain* swapchain_ = nullptr;
    std::string* present_error_ = nullptr;
    core::Timestamp acquired_at_;
    core::Timestamp submitted_at_;
    std::vector<Vertex> vertices_;
    std::vector<geometry::Rect> clip_stack_;
};

class Sdl3GpuDevice final : public GraphicsDevice {
public:
    static core::Result<std::unique_ptr<GraphicsDevice>> create(
        const Configuration& configuration);

    ~Sdl3GpuDevice() override;

    [[nodiscard]] AdapterInfo adapter_info() const override;
    [[nodiscard]] std::string_view api_name() const noexcept override {
        return "sdl3";
    }

    [[nodiscard]] core::Result<std::unique_ptr<Swapchain>> create_swapchain(
        gpu::WindowHandle window_handle, geometry::Size initial_size) override;

    [[nodiscard]] core::Result<std::unique_ptr<RenderPass>> begin_clear_pass(
        Swapchain& swapchain, const float clear_color[4]) override;

    [[nodiscard]] core::Result<std::unique_ptr<DrawPass>> begin_draw_pass(
        Swapchain& swapchain) override;

private:
    // Creates the renderer for `window`: SDL's best driver first, the software
    // driver as fallback, vsync enabled. Sets is_hardware_/adapter_name_.
    core::Result<void> create_renderer(SDL_Window* window);

    SDL_Renderer* renderer_ = nullptr;
    bool is_hardware_ = false;
    std::string adapter_name_;
    // The last present failure (empty when the last present succeeded). Kept
    // on the device so a broken renderer reports a real message instead of a
    // generic clear failure on the next frame.
    std::string present_error_;
};

} // namespace ca::gpu
