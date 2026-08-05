// The compositor (internal — Level 3 machinery, not public API).
//
// Owns the GPU device, the swapchain, and the vsync-paced frame loop on its
// own thread (docs/02-architecture.md §2). Per vsync it predicts the
// presentation time, ticks the animation coordinator at that instant, and
// resolves the frame: fresh presentation values against the latest committed
// layer-tree packet, rasterized into a draw pass and submitted. This is the
// M2 shape — M1's analytic clear color was the stand-in until the coordinator
// and the layer tree landed; the loop's timing contract did not change.
//
// The loop (M2):
//
//   animate mode (has active animations or pending work):
//     t_present = display.predicted_presentation_time()
//     coordinator.tick_and_publish(t_present)
//     packet = layer_tree.latest_packet()       → resolve → rasterize → submit
//   idle mode (everything at rest): block on the wake condition — no vsync
//     pacing at all. This is the idle-CPU-0% story (docs/02 §7): the
//     compositor stops waking when the tree is fully at rest, and the
//     animation coordinator's rest queue is what gates it.
//
// Wake sources: request_stop (quit), request_repaint (the application after
// mutating static layer state or enqueueing an animation intent — the
// setNeedsDisplay contract), and handle_event (resize → one repaint).
//
// Per-frame timings are recorded into a FrameTimingRecorder and optionally
// appended to a CSV log for calcium-tracer (the CI budget gate consumes the
// same recorder; docs/06-roadmap.md "Continuous, from M0").

#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/core/time.hpp"
#include "calcium/gpu/graphics_device.hpp"
#include "calcium/layer/layer_tree.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

namespace ca::compositor {

class Compositor {
public:
    struct Configuration {
        platform::Window* window = nullptr;
        /// The animation coordinator this window's tree registers with.
        /// Null = the M1 clear-only path (no tick, no layers).
        animation::AnimationCoordinator* animation = nullptr;
        /// The window's layer tree. Null = the M1 clear-only path.
        layer::LayerTree* layer_tree = nullptr;
        /// Budget per stage; overruns are counted and reported.
        core::Duration frame_budget = core::Duration::from_hertz(120.0);
        /// Path for the per-frame CSV (calcium-tracer's input); empty = off.
        std::string_view trace_file_path;
    };

    /// Validates the configuration (no GPU work happens here: a flip-model
    /// swapchain is affine to the thread that creates it, and that thread is
    /// the compositor's own — docs/02-architecture.md §2.3).
    [[nodiscard]] static core::Result<std::unique_ptr<Compositor>> create(
        const Configuration& configuration);

    ~Compositor();

    Compositor(Compositor&&) = delete;
    Compositor& operator=(Compositor&&) = delete;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    /// Spawns the compositor thread. Idempotent.
    void start();

    /// Joins the compositor thread. Safe from any thread; the thread stops at
    /// the next wake at the latest.
    void stop();

    /// Asks the loop to stop; the platform event listener calls this on quit.
    void request_stop() noexcept;

    /// Asks the loop to render a frame. Call after mutating static layer
    /// state and committing the tree, or after enqueueing an animation
    /// intent — the setNeedsDisplay contract. Safe from any thread.
    void request_repaint() noexcept;

    /// Handles the platform events the compositor cares about (resize, close).
    void handle_event(const platform::Event& event);

    /// The frame timings measured so far (the tracer's summary source).
    [[nodiscard]] const core::FrameTimingRecorder& timing_recorder() const noexcept {
        return timing_;
    }

    /// The GPU adapter the device bound to; valid once the compositor
    /// thread has brought the device up.
    [[nodiscard]] const ca::gpu::GraphicsDevice::AdapterInfo& device_info() const noexcept {
        return device_info_;
    }
    /// True once the compositor thread has created the device and swapchain.
    [[nodiscard]] bool device_ready() const noexcept { return device_ready_; }
    /// Why the compositor thread stopped (device/swapchain creation failure,
    /// present failure); empty when it stopped cleanly.
    [[nodiscard]] std::string_view failure_message() const noexcept {
        return failure_message_;
    }

    /// Frames presented so far (diagnostics and the demo's stall-test gate).
    [[nodiscard]] std::uint64_t frame_count() const noexcept {
        return frame_count_.load(std::memory_order_relaxed);
    }

private:
    explicit Compositor(Configuration configuration);
    void run_loop();
    void record_frame(core::Duration ui, core::Duration compositor,
                      core::Duration gpu);
    /// Blocks while idle (no vsync pacing); returns once a wake reason is
    /// present. Consumes the repaint request.
    void wait_for_wake();

    Configuration configuration_;
    std::unique_ptr<ca::gpu::GraphicsDevice> device_;
    std::unique_ptr<ca::gpu::Swapchain> swapchain_;
    ca::gpu::GraphicsDevice::AdapterInfo device_info_;
    std::atomic<bool> device_ready_{false};
    std::string failure_message_;
    core::FrameTimingRecorder timing_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> thread_running_{false};
    std::atomic<std::uint64_t> frame_count_{0};
    std::ofstream trace_file_;

    // Idle wake channel (single CV; all wake sources notify it).
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    bool repaint_requested_ = false;
};

} // namespace ca::compositor
