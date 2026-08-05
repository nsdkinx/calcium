#pragma once

// The animation coordinator.
//
// The architecture's thesis in executable form (docs/02-architecture.md §7,
// docs/05-animation-and-twell.md §3): one Twell context per Application,
// owned by the compositor; the UI thread never calls Twell — it appends small
// POD intents to a lock-free ring, and the compositor applies them at the
// start of its next tick, at the frame's presentation timestamp. Every value
// is an analytically solved function of absolute time, so the compositor can
// skip ticks (a stalled UI thread, a heavy frame) and the animation is still
// correct at the moment it is sampled.
//
// Thread contract (docs/02-architecture.md §2.3):
//   UI thread      — create_property, enqueue_intent, commit, model/presentation reads
//   compositor     — tick_and_publish only
// Enforced by CA_ASSERT_* in debug builds.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "calcium/animation/motion.hpp"
#include "calcium/animation/motion_scheme.hpp"
#include "calcium/animation/spring_configuration.hpp"
#include "calcium/core/result.hpp"
#include "calcium/core/spsc_ring.hpp"
#include "calcium/core/time.hpp"

namespace ca::animation {

/// What the UI thread wants the compositor to do. Fixed-size POD: enqueueing
/// never allocates (P8), and the ring never runs constructors or destructors.
struct AnimationIntent {
    enum class Kind : std::uint8_t {
        animate_to_target,          ///< spring to `target` with `spring`
        set_immediate,              ///< snap model and presentation to `target`
        stop_at_presentation_value, ///< freeze where it is
    };

    Kind kind = Kind::animate_to_target;
    std::uint32_t property_index = 0;
    std::uint32_t dimensionality = 1;  ///< 1, 2, or 3 components
    double target[3] = {0.0, 0.0, 0.0};
    SpringConfiguration spring;
    double delay_seconds = 0.0;
    std::uint32_t sequence_number = 0;  ///< preserves commit ordering
};

/// The coordinator. One per Application; never copied or moved.
class AnimationCoordinator {
public:
    struct Configuration {
        std::uint32_t max_animated_properties = 4096;
        std::uint32_t max_concurrent_gestures = 64;
    };

    /// The arena is sized by asking Twell, never by guessing.
    [[nodiscard]] static core::Result<std::unique_ptr<AnimationCoordinator>> create(
        const Configuration& configuration);

    ~AnimationCoordinator();

    AnimationCoordinator(AnimationCoordinator&&) = delete;
    AnimationCoordinator& operator=(AnimationCoordinator&&) = delete;
    AnimationCoordinator(const AnimationCoordinator&) = delete;
    AnimationCoordinator& operator=(const AnimationCoordinator&) = delete;

    // === UI THREAD ONLY ===

    /// Registers a property with the coordinator, returning its index. Called
    /// at setup; the property count is fixed for the coordinator's lifetime
    /// (the snapshot arrays are sized here, so the frame path never allocates).
    /// `initial` seeds both Twell and the published snapshot, so the
    /// presentation value is correct before the first tick.
    [[nodiscard]] core::Result<std::uint32_t> register_property(
        std::uint32_t dimensionality, const double initial[3]);

    /// Appends an intent to the compositor's queue, stamped with the commit
    /// order. Never blocks; fails when the queue is full (a UI thread running
    /// away faster than vsync).
    [[nodiscard]] bool enqueue_intent(AnimationIntent intent);

    /// Reads the last published presentation snapshot (seqlock). Never blocks
    /// the compositor.
    [[nodiscard]] double presentation_value(std::uint32_t property_index,
                                             std::uint32_t component) const noexcept;

    /// True while any property is still in motion. The compositor uses this
    /// to decide whether to keep waking (docs/02-architecture.md §5: idle
    /// power).
    [[nodiscard]] bool has_active_animations() const noexcept;

    /// Whether a specific property is at rest (published snapshot).
    [[nodiscard]] bool property_at_rest(std::uint32_t property_index) const noexcept;

    /// Fires rest callbacks for properties that reached rest since the last
    /// call. Called by the UI thread at a frame boundary; callbacks never run
    /// on the compositor.
    void dispatch_rest_callbacks();

    /// Registers a callback to fire (once, on the UI thread) when the property
    /// next reaches rest.
    [[nodiscard]] core::Result<void> on_property_reach_rest(
        std::uint32_t property_index, std::function<void()> callback);

    /// Registers a completion waiter: fires `callback` (via
    /// dispatch_rest_callbacks) once every listed property is at rest.
    /// Used by Transaction::commit for animate_with_completion.
    void register_completion(std::vector<std::uint32_t> properties,
                             std::function<void()> callback);

    /// The active motion scheme; named motions resolve through it at
    /// set_value time (UI thread).
    void set_motion_scheme(MotionScheme scheme);
    [[nodiscard]] const MotionScheme& motion_scheme() const noexcept;

    /// Resolves a motion to its concrete spring (or passes decays/immediates
    /// through). UI thread.
    [[nodiscard]] Motion resolve_motion(Motion motion) const;

    // === COMPOSITOR THREAD ONLY ===

    /// Applies pending intents at `t_present` (the presentation timestamp of
    /// the frame being built), advances the analytical solutions, and
    /// publishes the presentation snapshot.
    void tick_and_publish(core::Timestamp t_present);

    /// The intent queue's sequence watermark — the number of intents applied
    /// so far (diagnostics).
    [[nodiscard]] std::uint32_t applied_intent_count() const noexcept;

private:
    struct Impl;
    explicit AnimationCoordinator(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

    // Seqlock read helpers (SPSC: compositor writes, UI thread reads).
    static double read_published_value(const Impl& impl,
                                       std::uint32_t property_index,
                                       std::uint32_t component);
    static bool read_property_at_rest(const Impl& impl,
                                      std::uint32_t property_index);
};

} // namespace ca::animation
