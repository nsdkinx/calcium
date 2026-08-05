// Clear Color — the M1 exit demo (docs/06-roadmap.md).
//
// A window that clears to an analytically animated color at the display's
// native refresh rate, with per-frame timings recorded. This is the thin
// vertical slice of the frame pipeline: platform events in, GPU frames out,
// paced by the swapchain's vsync anchor, with presentation-time prediction on
// the display object.
//
// M2 replaces the analytic hue cycle with a Twell-driven property; this file
// is also where the spring-animated rounded rect will land.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#include "calcium/calcium.hpp"

#include "backends/backend_registration.hpp"
#include "compositor/compositor.hpp"

namespace platform = ca::platform;

// Runs the app for the given number of seconds, then exits cleanly (CI smoke
// test mode: `example_clear_color --seconds 5`). Without it, the app runs
// until the window is closed.
double seconds_to_run = 0.0;

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string{argv[i]} == "--seconds" && i + 1 < argc) {
            seconds_to_run = std::strtod(argv[i + 1], nullptr);
        }
    }

    // Bring the platform and GPU backends into this link (see
    // backend_registration.hpp; the C-ABI bootstrap owns this at M7).
    ca::calcium_register_backends();

    auto app_result = platform::Application::create(
        {.name = "Clear Color", .bundle_identifier = "dev.calcium.clear-color"});
    if (!app_result.has_value()) {
        std::fprintf(stderr, "application create failed: %s\n",
                     std::string(app_result.error().description()).c_str());
        return 1;
    }
    auto app = std::move(app_result).take_value();

    auto window_result = app->create_window(
        {.title = "Calcium M1 — Clear Color", .size = {800.0f, 600.0f}});
    if (!window_result.has_value()) {
        std::fprintf(stderr, "window create failed: %s\n",
                     std::string(window_result.error().description()).c_str());
        return 1;
    }
    auto window = std::move(window_result).take_value();

    auto compositor_result = ca::compositor::Compositor::create(
        {.window = window.get(), .trace_file_path = "calcium-trace.csv"});
    if (!compositor_result.has_value()) {
        std::fprintf(stderr, "compositor create failed: %s\n",
                     std::string(compositor_result.error().description()).c_str());
        return 1;
    }
    auto compositor = std::move(compositor_result).take_value();

    // The platform thread routes events here; the compositor stops itself and
    // asks the app to end when the user quits or closes the window.
    app->set_event_listener([&](const platform::Event& event) {
        compositor->handle_event(event);
        if (event.is_system()) {
            const auto& system = event.as_system();
            const bool quit = system.kind == platform::SystemEventKind::quit_requested;
            const bool close =
                system.kind == platform::SystemEventKind::window_close_requested &&
                system.window_id == window->window_id();
            if (quit || close) {
                compositor->request_stop();
                app->quit();
            }
        }
    });

    if (seconds_to_run > 0.0) {
        std::thread timer{[&] {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<long long>(seconds_to_run * 1000.0)));
            compositor->request_stop();
            app->quit();
        }};
        timer.detach();
    }

    const platform::Display display = window->display();
    std::printf("Calcium M1 — Clear Color\n");
    std::printf("  display : %.1f Hz, scale %.1f, presentation prediction %s\n",
                display.refresh_rate_hz(), display.scale_factor(),
                display.provides_hardware_presentation_prediction()
                    ? "hardware"
                    : "extrapolated (vsync cadence)");

    compositor->start();

    // The device comes up on the compositor thread; wait briefly for it (or
    // for the failure it reports).
    for (int i = 0; i < 100; ++i) {
        if (compositor->device_ready()) {
            break;
        }
        if (!compositor->failure_message().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!compositor->device_ready()) {
        std::fprintf(stderr, "compositor failed: %s\n",
                     std::string(compositor->failure_message()).c_str());
        return 1;
    }
    std::printf("  adapter : %s (%s)\n",
                compositor->device_info().name.c_str(),
                compositor->device_info().is_hardware ? "hardware"
                                                      : "software");
    std::printf("  tracing : calcium-trace.csv\n\n");

    app->run();
    compositor->stop();

    // The same numbers calcium-tracer reports from the CSV; the in-process
    // summary is the CI gate's source once that lands.
    const auto& timings = compositor->timing_recorder();
    std::printf("frames: %u  compositor p50 %.2f ms / p99 %.2f ms\n",
                timings.recorded_frame_count(),
                timings.percentile_total(0.50).milliseconds(),
                timings.percentile_total(0.99).milliseconds());
    std::printf("budget overruns (> %.2f ms): %u\n",
                timings.frame_budget().milliseconds(),
                timings.budget_overrun_count());
    return 0;
}
