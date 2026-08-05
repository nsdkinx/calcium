// SDL3 platform backend — internal, never public (docs/02-architecture.md §6).
//
// SDL3 is the portable bring-up path for Windows/macOS/Linux (docs/00-overview
// §4.2): windowing, input normalization, display mode. It is not the mobile
// path (lifecycle, IME and safe areas come from the native backends in M6),
// and its presentation timing is the honest extrapolation path — the
// `predicted_presentation_time` contract requires saying so when hardware
// feedback is unavailable (docs/02-architecture.md §3.1).

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_set>

#include <SDL3/SDL.h>

#include "calcium/core/result.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

#include "platform/backend.hpp"  // ca::platform::backend

namespace ca::platform::backend {

/// The vsync-cadence timing model: presentation prediction by extrapolation.
///
///   t_present = t_now + (frames_in_flight + 1) × vsync_interval
///
/// with frames_in_flight = 2 (the D3D12 swapchain's back-buffer count, which
/// the GPU backend will report through a real API once the frame pipeline
/// lands). `provides_hardware_prediction` is false: SDL3 has no frame-deadline
/// feedback, so this model is the documented degradation, visible in
/// diagnostics rather than silently accepted.
class Sdl3DisplayTiming final : public DisplayTiming {
public:
    explicit Sdl3DisplayTiming(std::uint32_t display_index);

    [[nodiscard]] core::Timestamp predicted_presentation_time() const override;
    [[nodiscard]] float refresh_rate_hz() const override;
    [[nodiscard]] float scale_factor() const override;
    [[nodiscard]] bool provides_hardware_prediction() const override {
        return false;
    }

private:
    std::uint32_t display_index_ = 0;
    float refresh_rate_hz_ = 60.0f;
    float scale_factor_ = 1.0f;
};

class Sdl3Platform;

class Sdl3Window final : public BackendWindow {
public:
    Sdl3Window(SDL_Window* window, const Sdl3DisplayTiming* timing,
               std::uint64_t window_id, Sdl3Platform* platform)
        : window_(window), timing_(timing), window_id_(window_id),
          platform_(platform) {}

    ~Sdl3Window() override;

    [[nodiscard]] std::string_view title() const override;
    void set_title(std::string_view title) override;

    [[nodiscard]] geometry::Size content_size() const override;
    void set_content_size(geometry::Size size) override;

    [[nodiscard]] const DisplayTiming& display_timing() const override {
        return *timing_;
    }

    void request_close() noexcept override;
    [[nodiscard]] bool is_close_requested() const noexcept override;

    [[nodiscard]] std::uint64_t native_handle() const override;
    [[nodiscard]] std::uint64_t window_id() const override { return window_id_; }

    /// Content size in device pixels (the GPU swapchain's unit).
    [[nodiscard]] geometry::Size content_size_pixels() const;

private:
    SDL_Window* window_ = nullptr;
    const Sdl3DisplayTiming* timing_ = nullptr;
    std::uint64_t window_id_ = 0;
    Sdl3Platform* platform_ = nullptr;
};

class Sdl3Platform final : public PlatformBackend {
public:
    Sdl3Platform();
    ~Sdl3Platform() override;

    [[nodiscard]] core::Result<std::unique_ptr<BackendWindow>> create_window(
        const Window::Configuration& configuration) override;
    [[nodiscard]] const DisplayTiming& primary_display() const override;
    void run_event_loop(const std::function<void()>& on_idle) override;
    void request_quit() noexcept override;
    void set_event_sink(std::function<void(const Event&)>) override;

private:
    friend class Sdl3Window;
    void dispatch_sdl_event(const SDL_Event& event);
    void emit(const Event& event);
    bool is_close_requested(std::uint64_t window_id) const noexcept;

    std::function<void(const Event&)> event_sink_;
    std::atomic<bool> quit_requested_{false};
    std::unique_ptr<Sdl3DisplayTiming> primary_display_timing_;
    std::unordered_set<std::uint64_t> close_requested_windows_;
    std::uint64_t next_window_id_ = 1;
};

// Called by the umbrella (src/calcium.cpp) at startup.
void register_sdl3_backend();

} // namespace ca::platform::backend
