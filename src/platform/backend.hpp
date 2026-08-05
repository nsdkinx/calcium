// The platform backend interface (internal — never in a public header).
//
// Each backend (SDL3, AppKit, UIKit, Win32…) implements this. The public
// Application/Window/Display classes are thin facades over it, so swapping a
// backend never touches application code (docs/00-overview.md §2.4).
//
// The platform thread runs `run_event_loop`; everything the backend learns
// about the outside world (input, lifecycle) leaves through `push_event` into
// the Application's ring, which is drained by the UI thread. The backend never
// calls into application code directly.

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "calcium/core/result.hpp"
#include "calcium/core/time.hpp"
#include "calcium/geometry/point.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

#include "display_timing.hpp"

namespace ca::platform::backend {

struct BackendWindow {
    virtual ~BackendWindow() = default;

    [[nodiscard]] virtual std::string_view title() const = 0;
    virtual void set_title(std::string_view) = 0;

    [[nodiscard]] virtual geometry::Size content_size() const = 0;
    virtual void set_content_size(geometry::Size) = 0;

    /// The display this window presents on (owned by the backend).
    [[nodiscard]] virtual const DisplayTiming& display_timing() const = 0;

    virtual void request_close() noexcept = 0;
    [[nodiscard]] virtual bool is_close_requested() const noexcept = 0;

    /// Opaque native handle for the GPU backend (HWND, NSWindow*, …).
    [[nodiscard]] virtual std::uint64_t native_handle() const = 0;

    /// Unique within the Application.
    [[nodiscard]] virtual std::uint64_t window_id() const = 0;
};

class PlatformBackend {
public:
    virtual ~PlatformBackend() = default;

    /// Creates a window on the platform. Returns the backend failure reason
    /// when the platform cannot.
    [[nodiscard]] virtual core::Result<std::unique_ptr<BackendWindow>>
    create_window(const Window::Configuration&) = 0;

    /// The display the application's windows present on. The backend owns the
    /// returned object for the backend's lifetime; the public Display facade
    /// borrows it.
    [[nodiscard]] virtual const ca::platform::DisplayTiming& primary_display() const = 0;

    /// The platform thread's main loop. Blocks until quit. `on_idle` is called
    /// when the platform has no events to process, so the compositor can be
    /// poked without spinning the platform thread.
    virtual void run_event_loop(const std::function<void()>& on_idle) = 0;

    /// Stops the event loop from any thread.
    virtual void request_quit() noexcept = 0;

    /// Routes normalized events to the Application, on the platform thread.
    /// Set by the Application at construction; the backend may call it from
    /// its event loop only.
    virtual void set_event_sink(std::function<void(const Event&)>) = 0;
};

/// The factory the umbrella library links: `create_sdl3_platform()` etc.
/// Registered into a static registry so Application::create can find a backend.
using PlatformBackendFactory = std::unique_ptr<PlatformBackend> (*)();

/// Registers a backend factory. Called by the umbrella at static init; not
/// thread-safe (startup only).
void register_platform_backend(PlatformBackendFactory factory) noexcept;

/// The registered backend, or nullptr when none is linked.
PlatformBackendFactory platform_backend_factory() noexcept;

} // namespace ca::platform::backend
