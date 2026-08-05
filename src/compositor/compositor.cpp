#include "compositor.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

#include "backends/backend_registration.hpp"
#include "calcium/core/thread_affinity.hpp"
#include "calcium/graphics/color.hpp"
#include "calcium/gpu/render_pass.hpp"
#include "calcium/gpu/swapchain.hpp"

namespace ca::compositor {

namespace {

// The M1 frame value: an analytic hue cycle over the presentation timestamp.
// This is the stand-in for Twell (M2) — a smooth function of absolute time
// evaluated on the compositor, never stepped by the application clock. The
// speed is one full hue rotation per four seconds, chosen to make cadence
// errors visible: at a wrong refresh rate the cycle visibly stutters.
graphics::Color animated_clear_color(core::Timestamp t_present) {
    constexpr double k_seconds_per_cycle = 4.0;
    const double hue = std::fmod(t_present.seconds() / k_seconds_per_cycle, 1.0);

    // HSV → RGB, hue in [0, 1).
    const double sector = hue * 6.0;
    const double region = std::floor(sector);
    const double f = sector - region;
    const double p = 0.0, q = 0.4 * (1.0 - f), t = 0.4 * (1.0 - (1.0 - f));
    double r = 0.0, g = 0.0, b = 0.0;
    switch (static_cast<int>(region)) {
    case 0: r = 0.4, g = t, b = p; break;
    case 1: r = q, g = 0.4, b = p; break;
    case 2: r = p, g = 0.4, b = t; break;
    case 3: r = p, g = q, b = 0.4; break;
    case 4: r = t, g = p, b = 0.4; break;
    default: r = 0.4, g = p, b = q; break;
    }
    return graphics::Color::srgb(static_cast<float>(r), static_cast<float>(g),
                                 static_cast<float>(b), 1.0f);
}

} // namespace

Compositor::Compositor(Configuration configuration)
    : configuration_(configuration) {
    timing_.set_frame_budget(configuration_.frame_budget);
}

core::Result<std::unique_ptr<Compositor>> Compositor::create(
    const Configuration& configuration) {
    if (configuration.window == nullptr) {
        return core::Result<std::unique_ptr<Compositor>>{
            core::ErrorCode::invalid_argument, "compositor needs a window"};
    }

    // The compositor is the framework component that consumes both backends;
    // bringing it up brings them too (see backend_registration.hpp).
    calcium_register_backends();

    auto compositor = std::unique_ptr<Compositor>{new Compositor(configuration)};

    auto device_result = ca::gpu::GraphicsDevice::create(
        ca::gpu::GraphicsDevice::Configuration{
            .enable_debug_layer = false,
            .enable_gpu_timing = false,
        });
    if (!device_result.has_value()) {
        return core::Result<std::unique_ptr<Compositor>>{
            device_result.error()};
    }
    compositor->device_ = std::move(device_result).take_value();
    compositor->device_info_ = compositor->device_->adapter_info();

    const platform::Window& window = *configuration.window;
    auto swapchain_result = compositor->device_->create_swapchain(
        static_cast<ca::gpu::WindowHandle>(window.native_handle()),
        window.content_size() * window.scale_factor());
    if (!swapchain_result.has_value()) {
        return core::Result<std::unique_ptr<Compositor>>{
            swapchain_result.error()};
    }
    compositor->swapchain_ = std::move(swapchain_result).take_value();

    if (!configuration.trace_file_path.empty()) {
        compositor->trace_file_.open(std::string(configuration.trace_file_path));
        if (compositor->trace_file_.is_open()) {
            compositor->trace_file_ << "frame,ui_us,compositor_us,gpu_us\n";
        }
    }

    return core::Result<std::unique_ptr<Compositor>>{std::move(compositor)};
}

Compositor::~Compositor() {
    stop();
}

void Compositor::start() {
    if (thread_running_.exchange(true)) {
        return;
    }
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run_loop(); });
}

void Compositor::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (trace_file_.is_open()) {
        trace_file_.flush();
        trace_file_.close();
    }
}

void Compositor::request_stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
}

void Compositor::handle_event(const platform::Event& event) {
    if (!event.is_system()) {
        return;
    }
    const auto& system = event.as_system();
    switch (system.kind) {
    case platform::SystemEventKind::window_resized:
        if (swapchain_ != nullptr) {
            // The swapchain works in device pixels; SDL reports pixel size.
            swapchain_->resize(geometry::Size{system.size.x, system.size.y});
        }
        break;
    default:
        break;
    }
}

void Compositor::run_loop() {
    core::register_current_thread_role(core::ThreadRole::compositor);

    while (!stop_requested_.load(std::memory_order_acquire)) {
        const core::Timestamp frame_begin = core::Timestamp::now();

        // The presentation timestamp: when this frame will scan out. Twell
        // will tick at exactly this moment (M2); today the clear color is an
        // analytic function of it, so the same timing contract is exercised.
        const platform::Display display = configuration_.window->display();
        const core::Timestamp t_present = display.predicted_presentation_time();
        const graphics::Color color = animated_clear_color(t_present);
        const float clear_color[4] = {color.red, color.green, color.blue,
                                      color.alpha};

        auto pass_result = device_->begin_clear_pass(*swapchain_, clear_color);
        if (!pass_result.has_value()) {
            break;  // the swapchain is gone (window closed under us)
        }
        auto pass = std::move(pass_result).take_value();
        pass->end_and_present();

        const core::Timestamp frame_end = core::Timestamp::now();
        // The frame's work starts when the pacing wait released, so the
        // recorded compositor time excludes the vsync wait itself.
        const core::Duration compositor_time =
            frame_end - pass->acquired_at();
        const core::Duration gpu_time =
            pass->submitted_at() - pass->acquired_at();

        // M1 has no UI thread; the stage exists so the CSV column does not
        // shift when it lands.
        record_frame(core::Duration::zero(), compositor_time, gpu_time);
    }
}

void Compositor::record_frame(core::Duration ui, core::Duration compositor,
                              core::Duration gpu) {
    timing_.record_frame(ui, compositor, gpu);
    if (trace_file_.is_open()) {
        trace_file_ << frame_count_ << ',' << ui.microseconds() << ','
                    << compositor.microseconds() << ',' << gpu.microseconds()
                    << '\n';
    }
    ++frame_count_;
}

} // namespace ca::compositor
