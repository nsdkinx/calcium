#pragma once

#include "calcium/core/export.hpp"

// The application: platform lifecycle, windows, and the input wire.
//
// One Application per process. It owns the platform backend (SDL3, AppKit,
// UIKit, Win32…), the window list, and the event ring that carries normalized
// events from the platform thread to the UI thread (docs/02-architecture.md
// §2). Application code runs on the UI thread; the platform backend runs on
// the OS main thread and never blocks on it.
//
// The GPU backends register themselves with the umbrella library at link time;
// `Application::create` fails with CA_ERROR_UNSUPPORTED if no backend is
// present (the honest failure for an unconfigured build).

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include "calcium/core/result.hpp"
#include "calcium/platform/display.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

namespace ca::platform {

struct ApplicationImpl;

class Application {
public:
    struct Configuration {
        std::string_view name = "Calcium Application";
        std::string_view bundle_identifier = "app.calcium";
        // The animation coordinator (the "one Twell context per
        // Application" of docs/02 §7) is created by the application's
        // bootstrap — Application itself is Level 1 and never names Level-3
        // types (the level DAG, docs/02 §1). The bootstrap sizes the Twell
        // arena through animation::AnimationCoordinator::Configuration.
    };

    /// Creates the application and initializes the platform backend. Fails
    /// with CA_ERROR_UNSUPPORTED when no backend is compiled in.
    [[nodiscard]] static core::Result<std::unique_ptr<Application>> create(
        const Configuration& configuration);

    ~Application();

    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Creates a window. Fails with CA_ERROR_BACKEND_FAILURE when the platform
    /// cannot (no display, video subsystem unavailable).
    [[nodiscard]] core::Result<std::unique_ptr<Window>> create_window(
        const Window::Configuration& configuration);

    /// The window's display (a window is never born without one).
    [[nodiscard]] Display primary_display() const noexcept;

    /// The event listener, called on the platform thread for every normalized
    /// event. Keep it fast; heavy work belongs on the UI thread. The
    /// compositor registers here for quit/close so it can stop its loop.
    void set_event_listener(std::function<void(const Event&)> listener);

    /// Blocks on the platform thread until quit is requested, then returns.
    /// The compositor (if any) keeps running until it observes the quit event
    /// and stops itself.
    void run();

    /// Ends run() from any thread. Also lets a backend stop the loop
    /// (e.g. the last window closing on mobile).
    void quit() noexcept;

    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
    void set_exit_code(int code) noexcept { exit_code_ = code; }

    /// True once run() has been entered (the platform loop is the OS main
    /// thread and must own the backend).
    [[nodiscard]] bool is_running() const noexcept { return is_running_; }

private:
    explicit Application(ApplicationImpl* impl) noexcept : impl_(impl) {}
    struct ApplicationImpl* impl_ = nullptr;
    int exit_code_ = 0;
    bool is_running_ = false;
};

} // namespace ca::platform
