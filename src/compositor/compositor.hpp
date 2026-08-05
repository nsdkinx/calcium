// The compositor (internal — Level 3 machinery, not public API).
//
// Owns the GPU device, the swapchain, and the vsync-paced frame loop on its
// own thread (docs/02-architecture.md §2): per vsync it predicts the
// presentation time, resolves the frame's presentation values, and submits.
// In M1 the presentation value is the animated clear color (an analytic
// function of the presentation timestamp); at M2 Twell drives it and this
// loop's shape does not change — only the source of the values does.
//
// The loop is:
//
//   wait on the swapchain's pacing handle   → one vsync anchor
//   t_present = display.predicted_presentation_time()
//   color = frame_value(t_present)          → M1: analytic hue cycle
//   record → submit → present
//
// Per-frame timings are recorded into a FrameTimingRecorder and optionally
// appended to a CSV log for calcium-tracer (the CI budget gate consumes the
// same recorder; docs/06-roadmap.md "Continuous, from M0").

#pragma once

#include <atomic>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>

#include "calcium/core/time.hpp"
#include "calcium/gpu/graphics_device.hpp"
#include "calcium/platform/event.hpp"
#include "calcium/platform/window.hpp"

namespace ca::compositor {

class Compositor {
public:
    struct Configuration {
        platform::Window* window = nullptr;
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
    /// the next vsync anchor at the latest.
    void stop();

    /// Asks the loop to stop; the platform event listener calls this on quit.
    void request_stop() noexcept;

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

    /// Handles the platform events the compositor cares about (resize, close).
    void handle_event(const platform::Event& event);

private:
    explicit Compositor(Configuration configuration);
    void run_loop();
    void record_frame(core::Duration ui, core::Duration compositor,
                      core::Duration gpu);

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
    std::uint64_t frame_count_ = 0;
    std::ofstream trace_file_;
};

} // namespace ca::compositor
