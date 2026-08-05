#pragma once

// Time.
//
// Twell's clock is `double` seconds on a monotonic timeline, and every animation
// value derives from it. Calcium uses exactly the same representation rather than
// converting at the boundary: a units mismatch here would show up as subtly wrong
// motion that no test would catch.
//
// `Timestamp` is monotonic. It is never wall-clock time — a user changing the
// system clock must not make animations jump.

#include <compare>
#include <cstdint>

namespace ca::core {

/// A signed span of time, in seconds.
class Duration {
public:
    constexpr Duration() noexcept = default;

    [[nodiscard]] static constexpr Duration from_seconds(double seconds) noexcept {
        return Duration{seconds};
    }
    [[nodiscard]] static constexpr Duration from_milliseconds(double ms) noexcept {
        return Duration{ms / 1000.0};
    }
    [[nodiscard]] static constexpr Duration from_microseconds(double us) noexcept {
        return Duration{us / 1'000'000.0};
    }
    [[nodiscard]] static constexpr Duration zero() noexcept { return Duration{0.0}; }

    [[nodiscard]] constexpr double seconds() const noexcept { return seconds_; }
    [[nodiscard]] constexpr double milliseconds() const noexcept {
        return seconds_ * 1000.0;
    }
    [[nodiscard]] constexpr double microseconds() const noexcept {
        return seconds_ * 1'000'000.0;
    }

    /// Refresh interval as a duration, e.g. `Duration::from_hertz(120.0)`.
    [[nodiscard]] static constexpr Duration from_hertz(double hertz) noexcept {
        return Duration{hertz > 0.0 ? 1.0 / hertz : 0.0};
    }

    [[nodiscard]] constexpr Duration operator+(Duration other) const noexcept {
        return Duration{seconds_ + other.seconds_};
    }
    [[nodiscard]] constexpr Duration operator-(Duration other) const noexcept {
        return Duration{seconds_ - other.seconds_};
    }
    [[nodiscard]] constexpr Duration operator*(double scale) const noexcept {
        return Duration{seconds_ * scale};
    }
    [[nodiscard]] constexpr Duration operator/(double divisor) const noexcept {
        return Duration{divisor != 0.0 ? seconds_ / divisor : 0.0};
    }

    [[nodiscard]] constexpr auto operator<=>(const Duration&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Duration&) const noexcept = default;

private:
    constexpr explicit Duration(double seconds) noexcept : seconds_(seconds) {}
    double seconds_ = 0.0;
};

/// A point on the monotonic timeline, in seconds.
///
/// This is the value handed to `twell_context_tick`. On the compositor it is the
/// *predicted presentation time* of the frame being built — the moment the frame
/// will be scanned out, not the moment it is composited (docs/02-architecture.md
/// §3.1). Ticking at composite time instead produces motion that is consistently
/// slightly wrong and invisible in a screenshot.
class Timestamp {
public:
    constexpr Timestamp() noexcept = default;

    /// Reads the platform's monotonic clock.
    [[nodiscard]] static Timestamp now() noexcept;

    [[nodiscard]] static constexpr Timestamp from_seconds(double seconds) noexcept {
        return Timestamp{seconds};
    }

    /// The value to pass to Twell.
    [[nodiscard]] constexpr double seconds() const noexcept { return seconds_; }

    [[nodiscard]] constexpr Duration operator-(Timestamp earlier) const noexcept {
        return Duration::from_seconds(seconds_ - earlier.seconds_);
    }
    [[nodiscard]] constexpr Timestamp operator+(Duration delta) const noexcept {
        return Timestamp{seconds_ + delta.seconds()};
    }
    [[nodiscard]] constexpr Timestamp operator-(Duration delta) const noexcept {
        return Timestamp{seconds_ - delta.seconds()};
    }

    [[nodiscard]] constexpr auto operator<=>(const Timestamp&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Timestamp&) const noexcept = default;

private:
    constexpr explicit Timestamp(double seconds) noexcept : seconds_(seconds) {}
    double seconds_ = 0.0;
};

/// Rolling frame-timing statistics, used by calcium-tracer and the CI budget gate.
///
/// Present from M0 because "120 fps with no dropped frames" must be a measurement
/// rather than an assertion (docs/00-overview.md §4.1).
class FrameTimingRecorder {
public:
    static constexpr std::uint32_t sample_capacity = 256;

    void record_frame(Duration ui_thread, Duration compositor, Duration gpu) noexcept;

    [[nodiscard]] Duration median_total() const noexcept;
    [[nodiscard]] Duration percentile_total(double percentile) const noexcept;
    [[nodiscard]] std::uint32_t recorded_frame_count() const noexcept {
        return recorded_count_;
    }
    [[nodiscard]] std::uint32_t budget_overrun_count() const noexcept {
        return budget_overrun_count_;
    }

    void set_frame_budget(Duration budget) noexcept { budget_ = budget; }
    [[nodiscard]] Duration frame_budget() const noexcept { return budget_; }

    void reset() noexcept;

private:
    struct Sample {
        double ui_seconds = 0.0;
        double compositor_seconds = 0.0;
        double gpu_seconds = 0.0;
    };

    Sample samples_[sample_capacity]{};
    std::uint32_t write_index_ = 0;
    std::uint32_t recorded_count_ = 0;
    std::uint32_t budget_overrun_count_ = 0;
    Duration budget_ = Duration::from_hertz(120.0);
};

} // namespace ca::core
