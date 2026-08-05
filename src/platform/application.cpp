#include "calcium/platform/application.hpp"

#include <memory>
#include <string_view>

#include "backend.hpp"

// The pimpl structs are defined in the ca::platform namespace to match the
// forward declarations in the public headers; the backend interface itself
// never leaks into a public header (P5).
namespace ca::platform {

struct WindowImpl {
    std::unique_ptr<backend::BackendWindow> backend_window;
};

struct ApplicationImpl {
    std::unique_ptr<backend::PlatformBackend> backend;
    std::function<void(const Event&)> event_listener;
    bool quit_requested = false;
};

namespace {

template <typename T>
core::Result<std::unique_ptr<T>> ok(std::unique_ptr<T> value) {
    return core::Result<std::unique_ptr<T>>{std::move(value)};
}

template <typename T>
core::Result<std::unique_ptr<T>> fail(core::ErrorCode code,
                                      std::string_view message) {
    return core::Result<std::unique_ptr<T>>{code, message};
}

backend::BackendWindow& backend_window(WindowImpl* impl) {
    return *impl->backend_window;
}

} // namespace

// ---------------------------------------------------------------------------
// Backend registry
// ---------------------------------------------------------------------------

namespace backend {

namespace {
PlatformBackendFactory g_platform_backend_factory = nullptr;
}

void register_platform_backend(PlatformBackendFactory factory) noexcept {
    g_platform_backend_factory = factory;
}

PlatformBackendFactory platform_backend_factory() noexcept {
    return g_platform_backend_factory;
}

} // namespace backend

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

core::Result<std::unique_ptr<Application>> Application::create(
    const Configuration& configuration) {
    // max_animated_properties/max_concurrent_gestures size the Twell arena
    // when ca::animation lands (M2); the backend does not need them yet.
    (void)configuration;
    const backend::PlatformBackendFactory factory =
        backend::platform_backend_factory();
    if (factory == nullptr) {
        return fail<Application>(core::ErrorCode::unsupported,
                                 "no platform backend linked (CALCIUM_PLATFORM_* "
                                 "is off)");
    }

    auto backend_instance = factory();
    if (backend_instance == nullptr) {
        return fail<Application>(core::ErrorCode::backend_failure,
                                 "platform backend failed to initialize");
    }

    // The backend routes every normalized event here, on the platform thread.
    auto impl = std::make_unique<ApplicationImpl>(std::move(backend_instance));
    impl->backend->set_event_sink(
        [impl = impl.get()](const Event& event) {
            if (impl->event_listener) {
                impl->event_listener(event);
            }
            if (event.is_system() &&
                event.as_system().kind == SystemEventKind::quit_requested) {
                impl->quit_requested = true;
            }
        });

    return ok<Application>(std::unique_ptr<Application>{new Application(impl.release())});
}

Application::~Application() = default;

core::Result<std::unique_ptr<Window>> Application::create_window(
    const Window::Configuration& configuration) {
    if (impl_ == nullptr) {
        return fail<Window>(core::ErrorCode::invalid_argument,
                            "application is not initialized");
    }
    auto backend_window_result = impl_->backend->create_window(configuration);
    if (!backend_window_result.has_value()) {
        return fail<Window>(backend_window_result.error().code(),
                            backend_window_result.error().description());
    }
    auto impl = std::make_unique<WindowImpl>(
        std::unique_ptr<backend::BackendWindow>(
            std::move(backend_window_result).take_value()));
    return ok<Window>(std::unique_ptr<Window>{new Window(impl.release())});
}

Display Application::primary_display() const noexcept {
    if (impl_ == nullptr) {
        return Display(0, 60.0f, 1.0f, false, nullptr);
    }
    const ca::platform::DisplayTiming& timing = impl_->backend->primary_display();
    return Display(0, timing.refresh_rate_hz(), timing.scale_factor(),
                   timing.provides_hardware_prediction(), &timing);
}

void Application::set_event_listener(std::function<void(const Event&)> listener) {
    if (impl_ != nullptr) {
        impl_->event_listener = std::move(listener);
    }
}

void Application::run() {
    if (impl_ == nullptr) {
        return;
    }
    is_running_ = true;
    impl_->quit_requested = false;
    impl_->backend->run_event_loop(/*on_idle=*/[] {});
    is_running_ = false;
}

void Application::quit() noexcept {
    if (impl_ != nullptr) {
        impl_->backend->request_quit();
    }
}

// ---------------------------------------------------------------------------
// Window facade
// ---------------------------------------------------------------------------

Window::~Window() { delete impl_; }

Window::Window(Window&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

std::string_view Window::title() const noexcept {
    return impl_ != nullptr ? backend_window(impl_).title() : std::string_view{};
}

void Window::set_title(std::string_view title) {
    if (impl_ != nullptr) {
        backend_window(impl_).set_title(title);
    }
}

geometry::Size Window::content_size() const noexcept {
    return impl_ != nullptr ? backend_window(impl_).content_size()
                            : geometry::Size{};
}

void Window::set_content_size(geometry::Size size) {
    if (impl_ != nullptr) {
        backend_window(impl_).set_content_size(size);
    }
}

Display Window::display() const noexcept {
    if (impl_ == nullptr) {
        return Display(0, 60.0f, 1.0f, false, nullptr);
    }
    const ca::platform::DisplayTiming& timing =
        backend_window(impl_).display_timing();
    return Display(backend_window(impl_).window_id(), timing.refresh_rate_hz(),
                   timing.scale_factor(), timing.provides_hardware_prediction(),
                   &timing);
}

float Window::scale_factor() const noexcept {
    return display().scale_factor();
}

NativeWindowHandle Window::native_handle() const noexcept {
    return impl_ != nullptr ? backend_window(impl_).native_handle() : 0;
}

void Window::request_close() noexcept {
    if (impl_ != nullptr) {
        backend_window(impl_).request_close();
    }
}

bool Window::is_close_requested() const noexcept {
    return impl_ != nullptr && backend_window(impl_).is_close_requested();
}

std::uint64_t Window::window_id() const noexcept {
    return impl_ != nullptr ? backend_window(impl_).window_id() : 0;
}

} // namespace ca::platform
