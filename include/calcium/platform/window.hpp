#pragma once

#include "calcium/core/export.hpp"

// Windows.
//
// A Window is the platform surface the GPU presents to. It carries no drawing
// state of its own — the compositor owns the swapchain and the frame pipeline
// (docs/02-architecture.md §3). Application code reaches it through the
// Application that created it, and it is valid only for that Application's
// lifetime.
//
// The native window handle is exposed as an opaque integer so the GPU backend
// can bind a swapchain to it without naming a platform type in a public header
// (P5). On Windows this is the HWND value; on macOS the NSWindow pointer value;
// on Linux the X11 Window / Wayland surface id.

#include <cstdint>
#include <string_view>

#include "calcium/geometry/point.hpp"
#include "calcium/platform/display.hpp"

namespace ca::platform {

class Application;
struct WindowImpl;

/// The platform's native window handle, opaque to public API consumers.
/// Valid only while the Window is alive.
using NativeWindowHandle = std::uint64_t;

class Window {
public:
    struct Configuration {
        std::string_view title = "Calcium";
        geometry::Size size{800.0f, 600.0f};
        /// Points to device pixels; 0 = use the display's native scale.
        float scale_factor = 0.0f;
        bool is_resizable = true;
    };

    ~Window();

    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] std::string_view title() const noexcept;
    void set_title(std::string_view title);

    /// Content size in points, not including window chrome.
    [[nodiscard]] geometry::Size content_size() const noexcept;
    void set_content_size(geometry::Size size);

    /// The display this window presents on.
    [[nodiscard]] Display display() const noexcept;

    /// Convenience: the display's points-to-pixels scale for this window.
    [[nodiscard]] float scale_factor() const noexcept;

    /// The compositor's binding point. Never dereferenced by public code.
    [[nodiscard]] NativeWindowHandle native_handle() const noexcept;

    /// Asks the platform to close the window. The platform answers with a
    /// `window_close_requested` system event; the application decides whether
    /// to destroy.
    void request_close() noexcept;

    /// True after the user pressed the close button (until destroyed).
    [[nodiscard]] bool is_close_requested() const noexcept;

    /// Unique within the Application; stable for the Window's lifetime.
    [[nodiscard]] std::uint64_t window_id() const noexcept;

private:
    friend class Application;
    Window(WindowImpl* impl) noexcept : impl_(impl) {}
    struct WindowImpl* impl_ = nullptr;
};

} // namespace ca::platform
