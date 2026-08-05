#include "compositor.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

#include "backends/backend_registration.hpp"
#include "calcium/core/thread_affinity.hpp"
#include "calcium/graphics/color.hpp"
#include "calcium/gpu/render_pass.hpp"
#include "calcium/gpu/swapchain.hpp"
#include "graphics/rasterizer.hpp"

namespace ca::compositor {

namespace {

// The window background before any layer covers it (dark, so a stale frame
// reads as intentional rather than as a white flash).
constexpr float k_window_background[4] = {0.02f, 0.02f, 0.04f, 1.0f};

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
    if ((configuration.animation == nullptr) !=
        (configuration.layer_tree == nullptr)) {
        return core::Result<std::unique_ptr<Compositor>>{
            core::ErrorCode::invalid_argument,
            "compositor needs both the coordinator and the layer tree (or "
            "neither — the M1 clear-only path)"};
    }

    // The compositor is the framework component that consumes both backends;
    // bringing it up brings them too (see backend_registration.hpp).
    calcium_register_backends();

    auto compositor = std::unique_ptr<Compositor>{new Compositor(configuration)};

    // No GPU work here: the device and swapchain are created on the compositor
    // thread, inside run_loop() — the SDL renderer must be created and used
    // from one thread, and a flip-model swapchain is affine to its creator
    // (docs/02-architecture.md §2.3).

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
    wake_cv_.notify_all();
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
    wake_cv_.notify_all();
}

void Compositor::request_repaint() noexcept {
    {
        std::lock_guard lock{wake_mutex_};
        repaint_requested_ = true;
    }
    wake_cv_.notify_all();
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
            // The renderer follows the window's size; present once at the
            // new size (a resize while idle would otherwise never repaint).
            request_repaint();
        }
        break;
    default:
        break;
    }
}

void Compositor::wait_for_wake() {
    std::unique_lock lock{wake_mutex_};
    wake_cv_.wait(lock, [this] {
        return stop_requested_.load(std::memory_order_acquire)
            || repaint_requested_;
    });
    // The repaint request is consumed by the frame that follows.
    repaint_requested_ = false;
}

void Compositor::run_loop() {
    core::register_current_thread_role(core::ThreadRole::compositor);

    // Bring the GPU up ON the compositor thread: the SDL renderer must be
    // created and used from one thread, and a flip-model swapchain is affine
    // to the thread that created it (docs/02-architecture.md §2.3 — the M1
    // present bug was exactly this). A failure stops the loop before any
    // frame; device_ready_/failure_message_ tell the application why.
    {
        auto device_result = ca::gpu::GraphicsDevice::create(
            ca::gpu::GraphicsDevice::Configuration{
                .enable_debug_layer = false,
                .enable_gpu_timing = false,
            });
        if (!device_result.has_value()) {
            failure_message_ =
                std::string(device_result.error().description());
            return;
        }
        device_ = std::move(device_result).take_value();

        const platform::Window& window = *configuration_.window;
        auto swapchain_result = device_->create_swapchain(
            static_cast<ca::gpu::WindowHandle>(window.native_handle()),
            window.content_size() * window.scale_factor());
        if (!swapchain_result.has_value()) {
            failure_message_ =
                std::string(swapchain_result.error().description());
            return;
        }
        swapchain_ = std::move(swapchain_result).take_value();
        device_info_ = device_->adapter_info();
        device_ready_ = true;
    }

    const bool m2_path = configuration_.animation != nullptr;
    bool idle = false;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (idle) {
            // Everything is at rest: stop waking entirely — the idle-CPU-0%
            // story (docs/02-architecture.md §7). A wake means work.
            wait_for_wake();
            idle = false;
            continue;
        }

        // The presentation timestamp: when this frame will scan out. Twell
        // ticks at exactly this moment, so the animation values in this
        // frame are solved for the instant it is seen (docs/02 §3.1).
        const platform::Display display = configuration_.window->display();
        const core::Timestamp t_present = display.predicted_presentation_time();

        if (m2_path) {
            // 1. Apply the UI thread's intents and advance the analytical
            //    solutions to t_present; publish the snapshot (docs/02 §7).
            configuration_.animation->tick_and_publish(t_present);

            // 2. Resolve the frame: the newest committed packet, the fresh
            //    presentation values, and the layer content, rasterized into
            //    the draw pass (docs/02 §3 stage 2 — steps 4–7; merging and
            //    batching land with the frame-pipeline work).
            const auto packet = configuration_.layer_tree->latest_packet();
            if (packet == nullptr || packet->layer_count() == 0) {
                // No content yet: present the window background so the
                // surface is never stale.
                auto pass_result =
                    device_->begin_clear_pass(*swapchain_, k_window_background);
                if (!pass_result.has_value()) {
                    failure_message_ =
                        std::string(pass_result.error().description());
                    std::fprintf(stderr, "pass failed: %s\n",
                                 std::string(pass_result.error().description())
                                     .c_str());
                    break;  // the swapchain is gone (window closed under us)
                }
                auto pass = std::move(pass_result).take_value();
                pass->end_and_present();

                const core::Duration compositor_time =
                    pass->submitted_at() - pass->acquired_at();
                record_frame(core::Duration::zero(), compositor_time,
                             compositor_time);
            } else {
                auto pass_result = device_->begin_draw_pass(*swapchain_);
                if (!pass_result.has_value()) {
                    failure_message_ =
                        std::string(pass_result.error().description());
                    std::fprintf(stderr, "pass failed: %s\n",
                                 std::string(pass_result.error().description())
                                     .c_str());
                    break;  // the swapchain is gone (window closed under us)
                }
                auto pass = std::move(pass_result).take_value();
                pass->clear(k_window_background);

                // The resolve loop: parallel SoA rows (docs/02 §4.1), each
                // resolving its presentation transform from the coordinator's
                // snapshot, then drawing the layer's background attribute and
                // its recorded content. Rows are in draw order (parents
                // before children, siblings in insertion order).
                const layer::FramePacket& frame = *packet;
                const auto bounds = frame.bounds();
                const auto corner_radii = frame.corner_radii();
                const auto background_colors = frame.background_colors();
                const auto transforms = frame.transforms();
                const auto position_indices = frame.position_property_indices();
                const auto opacity_indices = frame.opacity_property_indices();
                const auto display_lists = frame.display_lists();
                const animation::AnimationCoordinator& coordinator =
                    *configuration_.animation;

                for (std::size_t i = 0; i < frame.layer_count(); ++i) {
                    const geometry::Point position{
                        static_cast<float>(
                            coordinator.presentation_value(
                                position_indices[i], 0)),
                        static_cast<float>(
                            coordinator.presentation_value(
                                position_indices[i], 1)),
                    };
                    const float opacity = static_cast<float>(
                        coordinator.presentation_value(opacity_indices[i], 0));
                    const geometry::AffineTransform layer_transform =
                        geometry::AffineTransform::make_translation(
                            position.x, position.y)
                            .concatenating(transforms[i]);

                    // The layer's own background: a compositor-resolvable
                    // attribute (docs/02 §2.2), so animating it never needs
                    // a re-record.
                    const graphics::Color& background = background_colors[i];
                    if (background.alpha > 0.0f) {
                        const geometry::RoundedRectangle fill =
                            geometry::RoundedRectangle::uniform(
                                bounds[i], corner_radii[i]);
                        const float color[4] = {
                            background.red, background.green, background.blue,
                            background.alpha};
                        graphics::rasterizer::fill_rounded_rectangle(
                            *pass, fill, layer_transform, color, opacity);
                    }

                    // The layer's recorded content (P7 — Level 3 → Level 2).
                    const graphics::DisplayList& list = display_lists[i];
                    if (!list.is_empty()) {
                        graphics::rasterizer::draw(list, *pass, layer_transform,
                                                   opacity);
                    }
                }

                pass->end_and_present();

                const core::Duration compositor_time =
                    pass->submitted_at() - pass->acquired_at();
                record_frame(core::Duration::zero(), compositor_time,
                             compositor_time);
            }

            // 3. Idle decision: when everything is at rest and no work is
            //    pending, stop waking. A repaint request arriving between
            //    frames is honored by one more frame, then the idle check
            //    runs again.
            {
                std::lock_guard lock{wake_mutex_};
                if (repaint_requested_) {
                    repaint_requested_ = false;
                } else if (!configuration_.animation->has_active_animations()) {
                    idle = true;
                }
            }
        } else {
            // M1 path: the analytic clear color (kept so the M1 demo and its
            // timing contract keep running unchanged).
            const graphics::Color color = [&] {
                constexpr double k_seconds_per_cycle = 4.0;
                const double hue = std::fmod(t_present.seconds() / k_seconds_per_cycle, 1.0);
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
                return graphics::Color::srgb(static_cast<float>(r),
                                             static_cast<float>(g),
                                             static_cast<float>(b), 1.0f);
            }();
            const float clear_color[4] = {color.red, color.green, color.blue,
                                          color.alpha};

            auto pass_result = device_->begin_clear_pass(*swapchain_, clear_color);
            if (!pass_result.has_value()) {
                failure_message_ =
                    std::string(pass_result.error().description());
                std::fprintf(stderr, "pass failed: %s\n",
                             std::string(pass_result.error().description()).c_str());
                break;  // the swapchain is gone (window closed under us)
            }
            auto pass = std::move(pass_result).take_value();
            pass->end_and_present();

            const core::Duration compositor_time =
                pass->submitted_at() - pass->acquired_at();
            record_frame(core::Duration::zero(), compositor_time,
                         compositor_time);
        }
    }
}

void Compositor::record_frame(core::Duration ui, core::Duration compositor,
                              core::Duration gpu) {
    timing_.record_frame(ui, compositor, gpu);
    if (trace_file_.is_open()) {
        trace_file_ << frame_count_.load(std::memory_order_relaxed) << ','
                    << ui.microseconds() << ','
                    << compositor.microseconds() << ','
                    << gpu.microseconds() << '\n';
    }
    frame_count_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace ca::compositor
