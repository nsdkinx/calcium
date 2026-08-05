// SDL3 platform backend implementation.

#include "sdl3_platform.hpp"

#include <string>
#include <string_view>
#include <unordered_set>

#include "calcium/core/time.hpp"

namespace ca::platform::backend {

namespace {

constexpr core::Timestamp sdl_ticks_to_timestamp(std::uint64_t ticks_ns) {
    return core::Timestamp::from_seconds(static_cast<double>(ticks_ns) / 1e9);
}

// The gpu_sdl3 swapchain reports two frames in flight (SDL keeps its own
// buffer count opaque; see sdl3_platform.hpp — the presentation-time model
// and the backend are calibrated to the same constant).
constexpr double k_frames_in_flight = 2.0;

// Maps SDL's button (1 = left, 2 = right, 3 = middle, 4/5 = aux) to the
// Calcium mask (bit 0 primary, bit 1 secondary, bit 2 middle).
std::uint32_t button_mask_for(std::uint32_t sdl_button) {
    return sdl_button >= 1 && sdl_button <= 8 ? 1u << (sdl_button - 1) : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Sdl3DisplayTiming
// ---------------------------------------------------------------------------

Sdl3DisplayTiming::Sdl3DisplayTiming(std::uint32_t display_index)
    : display_index_(display_index) {
    // SDL3.4: the current display mode is queried by display id.
    const SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display_id);
        mode != nullptr && mode->refresh_rate > 0.0f) {
        refresh_rate_hz_ = mode->refresh_rate;
    }
    scale_factor_ = SDL_GetDisplayContentScale(display_id);
    if (scale_factor_ <= 0.0f) {
        scale_factor_ = 1.0f;
    }
}

core::Timestamp Sdl3DisplayTiming::predicted_presentation_time() const {
    // Extrapolation from the measured vsync cadence — the documented honest
    // path when the platform cannot provide frame-deadline feedback. A frame
    // composited now is scanned out `frames_in_flight + 1` vsyncs from now.
    const core::Duration interval = core::Duration::from_hertz(refresh_rate_hz_);
    return core::Timestamp::now() + interval * (k_frames_in_flight + 1.0);
}

float Sdl3DisplayTiming::refresh_rate_hz() const {
    return refresh_rate_hz_;
}

float Sdl3DisplayTiming::scale_factor() const {
    return scale_factor_;
}

// ---------------------------------------------------------------------------
// Sdl3Window
// ---------------------------------------------------------------------------

Sdl3Window::~Sdl3Window() {
    SDL_DestroyWindow(window_);
}

std::string_view Sdl3Window::title() const {
    const char* title = SDL_GetWindowTitle(window_);
    return title != nullptr ? std::string_view{title} : std::string_view{};
}

void Sdl3Window::set_title(std::string_view title) {
    SDL_SetWindowTitle(window_, std::string(title).c_str());
}

geometry::Size Sdl3Window::content_size() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}

void Sdl3Window::set_content_size(geometry::Size size) {
    SDL_SetWindowSize(window_, static_cast<int>(size.width),
                      static_cast<int>(size.height));
}

geometry::Size Sdl3Window::content_size_pixels() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}

void Sdl3Window::request_close() noexcept {
    // SDL3.4 removed SDL_SendWindowEvent; the platform-facing way to ask for
    // a close is to post the close event, which the platform loop then
    // normalizes like any other.
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(window_);
    SDL_PushEvent(&event);
}

bool Sdl3Window::is_close_requested() const noexcept {
    return platform_ != nullptr && platform_->is_close_requested(window_id_);
}

std::uint64_t Sdl3Window::native_handle() const {
    // The SDL_Window* pointer value: the GPU backend (gpu_sdl3) creates its
    // renderer from it directly. The HWND extraction this used to do existed
    // for D3D12's CreateSwapChainForHwnd, which is archived.
    return reinterpret_cast<std::uint64_t>(window_);
}

// ---------------------------------------------------------------------------
// Sdl3Platform
// ---------------------------------------------------------------------------

Sdl3Platform::Sdl3Platform() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    primary_display_timing_ = std::make_unique<Sdl3DisplayTiming>(0);
}

Sdl3Platform::~Sdl3Platform() {
    SDL_Quit();
}

core::Result<std::unique_ptr<BackendWindow>> Sdl3Platform::create_window(
    const Window::Configuration& configuration) {
    const Uint64 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY
                       | (configuration.is_resizable ? SDL_WINDOW_RESIZABLE : 0);
    SDL_Window* window = SDL_CreateWindow(
        std::string(configuration.title).c_str(),
        static_cast<int>(configuration.size.width),
        static_cast<int>(configuration.size.height), flags);
    if (window == nullptr) {
        return core::Result<std::unique_ptr<BackendWindow>>{
            core::ErrorCode::backend_failure,
            SDL_GetError() != nullptr ? SDL_GetError()
                                      : "SDL_CreateWindow failed"};
    }
    const std::uint64_t window_id = next_window_id_++;
    return core::Result<std::unique_ptr<BackendWindow>>{
        std::unique_ptr<BackendWindow>{new Sdl3Window(
            window, primary_display_timing_.get(), window_id, this)}};
}

const DisplayTiming& Sdl3Platform::primary_display() const {
    return *primary_display_timing_;
}

void Sdl3Platform::set_event_sink(std::function<void(const Event&)> sink) {
    event_sink_ = std::move(sink);
}

void Sdl3Platform::emit(const Event& event) {
    if (event_sink_) {
        event_sink_(event);
    }
}

void Sdl3Platform::request_quit() noexcept {
    quit_requested_.store(true, std::memory_order_release);
}

void Sdl3Platform::run_event_loop(const std::function<void()>& on_idle) {
    while (!quit_requested_.load(std::memory_order_acquire)) {
        SDL_Event sdl_event;
        if (SDL_WaitEventTimeout(&sdl_event, 10)) {
            do {
                dispatch_sdl_event(sdl_event);
            } while (SDL_PollEvent(&sdl_event));
        } else if (on_idle) {
            on_idle();
        }
    }
}

bool Sdl3Platform::is_close_requested(std::uint64_t window_id) const noexcept {
    return close_requested_windows_.count(window_id) != 0;
}

void Sdl3Platform::dispatch_sdl_event(const SDL_Event& event) {
    const core::Timestamp timestamp =
        sdl_ticks_to_timestamp(SDL_GetTicksNS());

    switch (event.type) {
    case SDL_EVENT_QUIT:
        emit(Event{SystemEvent{SystemEventKind::quit_requested, 0,
                               geometry::Point{}, timestamp}});
        break;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        close_requested_windows_.insert(event.window.windowID);
        emit(Event{SystemEvent{SystemEventKind::window_close_requested,
                               event.window.windowID, geometry::Point{},
                               timestamp}});
        break;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        emit(Event{SystemEvent{SystemEventKind::window_resized,
                               event.window.windowID,
                               {static_cast<float>(event.window.data1),
                                static_cast<float>(event.window.data2)},
                               timestamp}});
        break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        emit(Event{SystemEvent{SystemEventKind::window_gained_focus,
                               event.window.windowID, geometry::Point{},
                               timestamp}});
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        emit(Event{SystemEvent{SystemEventKind::window_lost_focus,
                               event.window.windowID, geometry::Point{},
                               timestamp}});
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        PointerEvent pointer;
        pointer.kind = PointerEventKind::move;
        pointer.pointer_type = PointerType::mouse;
        pointer.pointer_id = 1;  // the single mouse pointer
        pointer.position = {event.motion.x, event.motion.y};
        pointer.button_mask = event.motion.state;  // SDL_BUTTON_LMASK == bit 0
        pointer.timestamp = timestamp;
        emit(Event{pointer});
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        PointerEvent pointer;
        pointer.kind = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                           ? PointerEventKind::down
                           : PointerEventKind::up;
        pointer.pointer_type = PointerType::mouse;
        pointer.pointer_id = 1;
        pointer.position = {event.button.x, event.button.y};
        pointer.button_mask = button_mask_for(event.button.button);
        pointer.timestamp = timestamp;
        emit(Event{pointer});
        break;
    }

    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        // Touch coordinates are normalized [0,1]; convert to points using the
        // window the touch landed on.
        int width = 0;
        int height = 0;
        SDL_Window* window = SDL_GetWindowFromID(event.tfinger.windowID);
        if (window != nullptr) {
            SDL_GetWindowSize(window, &width, &height);
        }
        PointerEvent pointer;
        pointer.kind = event.type == SDL_EVENT_FINGER_DOWN
                           ? PointerEventKind::down
                           : event.type == SDL_EVENT_FINGER_UP
                                 ? PointerEventKind::up
                                 : PointerEventKind::move;
        pointer.pointer_type = PointerType::touch;
        pointer.pointer_id = static_cast<std::uint32_t>(event.tfinger.fingerID);
        pointer.position = {event.tfinger.x * static_cast<float>(width),
                            event.tfinger.y * static_cast<float>(height)};
        pointer.button_mask = pointer.kind == PointerEventKind::move ? 1 : 0;
        pointer.pressure = event.tfinger.pressure;
        pointer.timestamp = timestamp;
        emit(Event{pointer});
        break;
    }

    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP: {
        PointerEvent pointer;
        pointer.kind = event.type == SDL_EVENT_PEN_DOWN ? PointerEventKind::down
                                                        : PointerEventKind::up;
        pointer.pointer_type = event.ptouch.eraser ? PointerType::eraser
                                                   : PointerType::pen;
        pointer.pointer_id = static_cast<std::uint32_t>(event.ptouch.which);
        pointer.position = {event.ptouch.x, event.ptouch.y};
        pointer.button_mask = 1;
        pointer.pressure = event.ptouch.down ? 1.0f : 0.0f;
        pointer.timestamp = timestamp;
        emit(Event{pointer});
        break;
    }

    case SDL_EVENT_PEN_MOTION: {
        PointerEvent pointer;
        pointer.kind = PointerEventKind::move;
        pointer.pointer_type =
            (event.pmotion.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0
                ? PointerType::eraser
                : PointerType::pen;
        pointer.pointer_id = static_cast<std::uint32_t>(event.pmotion.which);
        pointer.position = {event.pmotion.x, event.pmotion.y};
        pointer.button_mask = 1;
        pointer.timestamp = timestamp;
        emit(Event{pointer});
        break;
    }

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        KeyEvent key;
        key.action = event.type == SDL_EVENT_KEY_DOWN ? KeyAction::down
                                                      : KeyAction::up;
        key.keycode = static_cast<std::uint32_t>(event.key.key);
        key.scancode = static_cast<std::uint32_t>(event.key.scancode);
        key.is_auto_repeat = event.key.repeat;
        key.timestamp = timestamp;
        emit(Event{key});
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        ScrollEvent scroll;
        // SDL3.4 reports wheel deltas as floats with accumulated integer
        // ticks; a trackpad's sub-tick deltas are the precise signal.
        scroll.kind = (event.wheel.integer_x == 0 && event.wheel.integer_y == 0)
                          ? ScrollKind::precise
                          : ScrollKind::coarse;
        scroll.delta = {event.wheel.x, event.wheel.y};
        scroll.is_direction_inverted =
            event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED;
        scroll.timestamp = timestamp;
        emit(Event{scroll});
        break;
    }

    default:
        break;  // uninteresting events are dropped at the platform layer
    }
}

} // namespace ca::platform::backend

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

namespace ca::platform::backend {

void register_sdl3_backend() {
    register_platform_backend([]() -> std::unique_ptr<PlatformBackend> {
        return std::make_unique<Sdl3Platform>();
    });
}

} // namespace ca::platform::backend
