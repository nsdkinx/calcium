// Spring Rectangle — the M2 exit demo (docs/06-roadmap.md M2).
//
// A rounded rect springs to where you tap. Tap again mid-flight to retarget:
// Twell's impulse superposition preserves the incoming velocity, so there is
// no snap and no velocity reset.
//
// The demo also measures the M2 exit criteria. Run with `--verify` to drive
// them automatically before the interactive session:
//
//   (2) retarget mid-flight preserves continuity — the position after a
//       retarget differs from the pre-retarget trajectory by only a few
//       frames' worth of motion (no jump to the target). The exact
//       velocity-preservation property is proven analytically in the unit
//       tests; this is the visible proxy.
//   (3) a 200 ms artificial UI-thread stall mid-animation does not interrupt
//       the animation: the compositor keeps shipping frames and the position
//       keeps advancing through the stall.
//   (4) at rest the compositor stops waking: no frames ship for the whole
//       following second — the idle-CPU-0% story (docs/02 §7).
//
// Controls: tap to spring the card, 's' runs the stall test interactively,
// ESC or the window close button quits. `--seconds N` exits after N seconds
// (CI smoke mode).

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "calcium/calcium.hpp"

#include "backends/backend_registration.hpp"
#include "compositor/compositor.hpp"

namespace platform = ca::platform;
namespace animation = ca::animation;
namespace graphics = ca::graphics;
namespace geometry = ca::geometry;
namespace layer = ca::layer;

namespace {

constexpr float k_card_width = 200.0f;
constexpr float k_card_height = 120.0f;

bool verify_requested = false;
double seconds_to_run = 0.0;

// The card's recorded content — the Level-3 → Level-2 doorway (P7): an
// inset, lighter rounded rectangle drawn through the recorder.
graphics::DisplayList make_card_content() {
    graphics::DisplayListRecorder recorder;
    recorder.fill_rounded_rectangle(
        geometry::RoundedRectangle::uniform(
            {8.0f, 8.0f, k_card_width - 8.0f, k_card_height - 8.0f}, 16.0f),
        graphics::Paint::solid_color(
            graphics::Color::srgb(0.45f, 0.62f, 0.98f)));
    return recorder.seal();
}

struct VerifyResults {
    int frames_during_stall = -1;
    float position_advance_during_stall = 0.0f;
    int idle_frames_in_one_second = -1;
    bool retarget_no_snap = false;
    bool stall_frames_ok = false;
    bool stall_advance_ok = false;
    bool idle_ok = false;
};

void sleep_ms(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

float distance(geometry::Point a, geometry::Point b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Criterion 3: stall the UI thread for 200 ms mid-animation and measure what
// the compositor shipped through the stall.
void run_stall_test(ca::compositor::Compositor& compositor,
                    layer::Layer& card, VerifyResults* results) {
    const std::uint64_t frames_before = compositor.frame_count();
    const geometry::Point position_before = card.position().presentation_value();

    sleep_ms(200);  // THE STALL: the UI thread blocks, the compositor doesn't

    const std::uint64_t frames_during =
        static_cast<std::uint64_t>(
            compositor.frame_count() - frames_before);
    const geometry::Point position_after = card.position().presentation_value();

    if (results != nullptr) {
        results->frames_during_stall = static_cast<int>(frames_during);
        results->position_advance_during_stall =
            distance(position_after, position_before);
    }
    std::printf("  stall: UI thread blocked 200 ms mid-animation\n");
    std::printf("    frames shipped during the stall: %llu (60 Hz ~12)\n",
                static_cast<unsigned long long>(frames_during));
    std::printf("    position advanced during the stall: %.1f px\n",
                distance(position_after, position_before));
}

// The automated exit-gate sequence (--verify). Runs on the UI thread (the
// main thread, in M2) before the interactive session.
VerifyResults run_verify_sequence(ca::compositor::Compositor& compositor,
                                  layer::Layer& card) {
    VerifyResults results;

    std::printf("\nM2 exit gate (--verify):\n");

    // Criterion 1: a rounded rect springs to a new position.
    sleep_ms(500);
    const geometry::Point target_a{250.0f, 150.0f};
    card.position().set_value(target_a, animation::Motion::standard());
    compositor.request_repaint();

    // Criterion 2 proxy: retarget mid-flight; the position must not snap.
    sleep_ms(300);  // A's spring is still mid-flight
    const geometry::Point before_retarget = card.position().presentation_value();
    const geometry::Point target_b{450.0f, 350.0f};
    card.position().set_value(target_b, animation::Motion::emphasized());
    compositor.request_repaint();
    sleep_ms(100);
    const geometry::Point after_retarget = card.position().presentation_value();
    const float remaining_distance = distance(target_b, before_retarget);
    const float moved_after_retarget = distance(after_retarget, before_retarget);
    results.retarget_no_snap =
        moved_after_retarget < 0.3f * remaining_distance
        && moved_after_retarget > 0.0f;
    std::printf("  retarget: moved %.1f px toward the new target in the first "
                "100 ms (no snap: < 30%% of %.1f px)\n",
                moved_after_retarget, remaining_distance);

    // Criterion 3: stall 200 ms while B's spring is mid-flight.
    sleep_ms(200);  // ≈ 0.6 s into B's emphasized spring
    run_stall_test(compositor, card, &results);
    results.stall_frames_ok = results.frames_during_stall >= 6;
    results.stall_advance_ok = results.position_advance_during_stall > 10.0f;

    // Criterion 4: at rest, the compositor stops waking.
    bool reached_rest = false;
    for (int i = 0; i < 300 && !reached_rest; ++i) {
        reached_rest = card.position().is_at_rest()
            && card.opacity().is_at_rest();
        if (!reached_rest) {
            sleep_ms(16);
        }
    }
    if (!reached_rest) {
        std::printf("  idle: spring did not reach rest within 5 s (FAIL)\n");
        return results;
    }
    sleep_ms(300);  // let the compositor's idle transition settle
    const std::uint64_t rest_frame = compositor.frame_count();
    sleep_ms(1000);
    const std::uint64_t idle_frames = compositor.frame_count() - rest_frame;
    results.idle_frames_in_one_second = static_cast<int>(idle_frames);
    results.idle_ok = idle_frames <= 1;
    std::printf("  idle: frames shipped in the 1 s after rest: %llu "
                "(idle-CPU-0%% proxy: <= 1)\n",
                static_cast<unsigned long long>(idle_frames));

    // Report.
    std::printf("\nM2 exit gate results:\n");
    std::printf("  [%s] criterion 2 — retarget preserves continuity\n",
                results.retarget_no_snap ? "PASS" : "FAIL");
    std::printf("  [%s] criterion 3 — 200 ms stall does not interrupt the "
                "animation (%d frames, %.1f px advanced)\n",
                results.stall_frames_ok && results.stall_advance_ok ? "PASS"
                                                                    : "FAIL",
                results.frames_during_stall,
                results.position_advance_during_stall);
    std::printf("  [%s] criterion 4 — idle CPU 0%% at rest (%d frames in 1 s)\n",
                results.idle_ok ? "PASS" : "FAIL",
                results.idle_frames_in_one_second);
    return results;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument{argv[i]};
        if (argument == "--verify") {
            verify_requested = true;
        } else if (argument == "--seconds" && i + 1 < argc) {
            seconds_to_run = std::strtod(argv[i + 1], nullptr);
        }
    }

    // Bring the platform and GPU backends into this link (see
    // backend_registration.hpp; the C-ABI bootstrap owns this at M7).
    ca::calcium_register_backends();

    auto app_result = platform::Application::create(
        {.name = "Spring Rectangle",
         .bundle_identifier = "dev.calcium.spring-rectangle"});
    if (!app_result.has_value()) {
        std::fprintf(stderr, "application create failed: %s\n",
                     std::string(app_result.error().description()).c_str());
        return 1;
    }
    auto app = std::move(app_result).take_value();

    auto window_result = app->create_window(
        {.title = "Calcium M2 — Spring Rectangle", .size = {800.0f, 600.0f}});
    if (!window_result.has_value()) {
        std::fprintf(stderr, "window create failed: %s\n",
                     std::string(window_result.error().description()).c_str());
        return 1;
    }
    auto window = std::move(window_result).take_value();

    // M2: the application's main thread plays the UI role — the dedicated UI
    // thread (docs/02-architecture.md §2) lands with the event-queue
    // milestone. Everything below mutates the model on this thread.
    ca::core::register_current_thread_role(ca::core::ThreadRole::ui);

    // The animation coordinator ("one per Application", docs/02 §7 — created
    // by the bootstrap, since Application itself is Level 1 and never names
    // Level-3 types) and the layer tree it drives.
    auto coordinator_result = animation::AnimationCoordinator::create({});
    if (!coordinator_result.has_value()) {
        std::fprintf(stderr, "coordinator create failed: %s\n",
                     std::string(coordinator_result.error().description())
                         .c_str());
        return 1;
    }
    auto coordinator = std::move(coordinator_result).take_value();

    auto tree_result =
        layer::LayerTree::create({.coordinator = coordinator.get()});
    if (!tree_result.has_value()) {
        std::fprintf(stderr, "layer tree create failed: %s\n",
                     std::string(tree_result.error().description()).c_str());
        return 1;
    }
    auto tree = std::move(tree_result).take_value();

    auto compositor_result = ca::compositor::Compositor::create(
        {.window = window.get(),
         .animation = coordinator.get(),
         .layer_tree = tree.get(),
         .trace_file_path = "calcium-trace.csv"});
    if (!compositor_result.has_value()) {
        std::fprintf(stderr, "compositor create failed: %s\n",
                     std::string(compositor_result.error().description())
                         .c_str());
        return 1;
    }
    auto compositor = std::move(compositor_result).take_value();

    // The scene: the root layer is the window background (a compositor-
    // resolvable attribute); the card is a sublayer with an animatable
    // position and a recorded display list.
    layer::Layer background = tree->root_layer();
    background.set_bounds({0.0f, 0.0f, window->content_size().width,
                           window->content_size().height});
    background.set_background_color(graphics::Color::srgb(0.05f, 0.06f, 0.10f));

    layer::Layer card = tree->create_layer();
    card.set_bounds({0.0f, 0.0f, k_card_width, k_card_height});
    card.set_corner_radius(24.0f);
    card.set_background_color(graphics::Color::srgb(0.20f, 0.38f, 0.80f));
    card.set_display_list(make_card_content());
    card.position().set_value_immediately({300.0f, 240.0f});
    background.add_sublayer(card);
    tree->commit();

    // The platform thread routes events here; the compositor stops itself and
    // asks the app to end when the user quits or closes the window.
    app->set_event_listener([&](const platform::Event& event) {
        compositor->handle_event(event);
        coordinator->dispatch_rest_callbacks();

        if (event.is_system()) {
            const auto& system = event.as_system();
            if (system.kind == platform::SystemEventKind::window_resized) {
                // The background follows the window; commit the new static
                // state and repaint once at the new size.
                background.set_bounds({0.0f, 0.0f, system.size.x,
                                       system.size.y});
                tree->commit();
                compositor->request_repaint();
            }
            const bool quit =
                system.kind == platform::SystemEventKind::quit_requested;
            const bool close =
                system.kind ==
                    platform::SystemEventKind::window_close_requested
                && system.window_id == window->window_id();
            if (quit || close) {
                compositor->request_stop();
                app->quit();
            }
            return;
        }

        if (event.is_pointer()) {
            const auto& pointer = event.as_pointer();
            if (pointer.kind == platform::PointerEventKind::down) {
                // The card springs so its center lands on the tap point.
                // Tapping again mid-flight retargets; Twell preserves the
                // incoming velocity by construction.
                card.position().set_value(
                    {pointer.position.x - k_card_width * 0.5f,
                     pointer.position.y - k_card_height * 0.5f},
                    animation::Motion::standard());
                compositor->request_repaint();
            }
            return;
        }

        if (event.is_key()) {
            const auto& key = event.as_key();
            if (key.action == platform::KeyAction::down && key.keycode == 's') {
                std::printf("\nstall test (interactive):\n");
                run_stall_test(*compositor, card, nullptr);
            }
            if (key.action == platform::KeyAction::down
                && key.keycode == 0x1B) {  // escape
                compositor->request_stop();
                app->quit();
            }
        }
    });

    const platform::Display display = window->display();
    std::printf("Calcium M2 — Spring Rectangle\n");
    std::printf("  display : %.1f Hz, scale %.1f, presentation prediction %s\n",
                display.refresh_rate_hz(), display.scale_factor(),
                display.provides_hardware_presentation_prediction()
                    ? "hardware"
                    : "extrapolated (vsync cadence)");
    std::printf("  controls: tap to spring the card, 's' stall test, "
                "ESC/close to quit\n");
    std::printf("  tracing : calcium-trace.csv\n\n");

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
        sleep_ms(10);
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

    bool exit_gate_passed = true;
    if (verify_requested) {
        const VerifyResults results =
            run_verify_sequence(*compositor, card);
        exit_gate_passed = results.retarget_no_snap
            && results.stall_frames_ok && results.stall_advance_ok
            && results.idle_ok;
        compositor->request_stop();
        app->quit();
    }

    if (seconds_to_run > 0.0) {
        std::thread timer{[&] {
            sleep_ms(static_cast<int>(seconds_to_run * 1000.0));
            compositor->request_stop();
            app->quit();
        }};
        timer.detach();
    }

    if (!verify_requested) {
        app->run();
    }
    compositor->stop();

    // The same numbers calcium-tracer reports from the CSV.
    const auto& timings = compositor->timing_recorder();
    std::printf("\nframes: %u  compositor p50 %.2f ms / p99 %.2f ms\n",
                timings.recorded_frame_count(),
                timings.percentile_total(0.50).milliseconds(),
                timings.percentile_total(0.99).milliseconds());
    std::printf("budget overruns (> %.2f ms): %u\n",
                timings.frame_budget().milliseconds(),
                timings.budget_overrun_count());

    return exit_gate_passed ? 0 : 1;
}
