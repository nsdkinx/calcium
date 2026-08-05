#include "calcium/core/time.hpp"

#include <algorithm>
#include <chrono>

namespace ca::core {

Timestamp Timestamp::now() noexcept {
    // steady_clock, never system_clock: a user adjusting the system clock must
    // not make in-flight animations jump.
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point origin = Clock::now();
    const auto elapsed = Clock::now() - origin;
    const auto seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
    return Timestamp::from_seconds(seconds);
}

void FrameTimingRecorder::record_frame(Duration ui_thread, Duration compositor,
                                       Duration gpu) noexcept {
    samples_[write_index_] = Sample{ui_thread.seconds(), compositor.seconds(),
                                    gpu.seconds()};
    write_index_ = (write_index_ + 1) % sample_capacity;
    if (recorded_count_ < sample_capacity) {
        ++recorded_count_;
    }

    // Stages are pipelined, so the frame is late when the slowest stage exceeds
    // the budget, not when their sum does (docs/02-architecture.md section 3).
    const double slowest = std::max({ui_thread.seconds(), compositor.seconds(),
                                     gpu.seconds()});
    if (slowest > budget_.seconds()) {
        ++budget_overrun_count_;
    }
}

Duration FrameTimingRecorder::median_total() const noexcept {
    return percentile_total(0.5);
}

Duration FrameTimingRecorder::percentile_total(double percentile) const noexcept {
    if (recorded_count_ == 0) {
        return Duration::zero();
    }

    double totals[sample_capacity];
    for (std::uint32_t index = 0; index < recorded_count_; ++index) {
        const Sample& sample = samples_[index];
        totals[index] = std::max({sample.ui_seconds, sample.compositor_seconds,
                                  sample.gpu_seconds});
    }

    const auto count = static_cast<std::size_t>(recorded_count_);
    const double clamped = std::clamp(percentile, 0.0, 1.0);
    auto rank = static_cast<std::size_t>(clamped * static_cast<double>(count - 1));
    std::nth_element(totals, totals + rank, totals + count);
    return Duration::from_seconds(totals[rank]);
}

void FrameTimingRecorder::reset() noexcept {
    write_index_ = 0;
    recorded_count_ = 0;
    budget_overrun_count_ = 0;
}

} // namespace ca::core
