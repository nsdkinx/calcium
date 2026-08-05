#pragma once

// Animatable properties.
//
// The central abstraction of the framework (docs/04-public-api.md §2.1,
// docs/05-animation-and-twell.md §2): a value that springs between states on
// the compositor's clock. The property is a UI-thread handle to a Twell
// property registered with the AnimationCoordinator:
//
//   model_value()        — the destination; layout and app logic use this
//   presentation_value() — what is on screen right now (the last published
//                          compositor snapshot — not synchronously live, P2)
//   set_value()          — writes enter the ambient transaction; the
//                          compositor applies them at the next presentation
//                          timestamp (P3)
//
// Supported value types: float (1D), geometry::Point (2D), geometry::Vector3
// (3D). Color and Transform3D (decomposed) land with the layer milestone.

#include <cstdint>
#include <functional>

#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/animation/motion.hpp"
#include "calcium/animation/transaction.hpp"
#include "calcium/core/result.hpp"
#include "calcium/geometry/point.hpp"

namespace ca::animation {

namespace detail {

template <typename ValueType>
struct PropertyTraits;

template <>
struct PropertyTraits<float> {
    static constexpr std::uint32_t dimensionality = 1;
    static constexpr float from_components(double c0, double, double) {
        return static_cast<float>(c0);
    }
    static void to_components(float value, double out[3]) {
        out[0] = value;
    }
};

template <>
struct PropertyTraits<geometry::Point> {
    static constexpr std::uint32_t dimensionality = 2;
    static constexpr geometry::Point from_components(double c0, double c1, double) {
        return {static_cast<float>(c0), static_cast<float>(c1)};
    }
    static void to_components(geometry::Point value, double out[3]) {
        out[0] = value.x;
        out[1] = value.y;
    }
};

template <>
struct PropertyTraits<geometry::Vector3> {
    static constexpr std::uint32_t dimensionality = 3;
    static constexpr geometry::Vector3 from_components(double c0, double c1,
                                                       double c2) {
        return {c0, c1, c2};
    }
    static void to_components(geometry::Vector3 value, double out[3]) {
        out[0] = value.x;
        out[1] = value.y;
        out[2] = value.z;
    }
};

} // namespace detail

template <typename ValueType>
class AnimatableProperty {
public:
    using Traits = detail::PropertyTraits<ValueType>;

    /// Registers the property with the coordinator. The registration
    /// allocates (snapshot slots); afterwards the frame path never allocates.
    [[nodiscard]] static core::Result<AnimatableProperty<ValueType>> create(
        AnimationCoordinator& coordinator, ValueType initial_value) {
        double components[3]{};
        Traits::to_components(initial_value, components);
        auto index_result =
            coordinator.register_property(Traits::dimensionality, components);
        if (!index_result.has_value()) {
            return core::Result<AnimatableProperty<ValueType>>{
                index_result.error()};
        }
        AnimatableProperty property{
            coordinator, std::move(index_result).take_value()};
        property.model_ = initial_value;
        return core::Result<AnimatableProperty<ValueType>>{property};
    }

    /// The destination. Layout, hit-testing, and app logic use this.
    [[nodiscard]] ValueType model_value() const noexcept { return model_; }

    /// What is on screen right now: the last published compositor snapshot.
    /// Deliberately named so callers know it is not synchronously live (P2).
    [[nodiscard]] ValueType presentation_value() const noexcept {
        return Traits::from_components(
            coordinator_->presentation_value(index_, 0),
            coordinator_->presentation_value(index_, 1),
            coordinator_->presentation_value(index_, 2));
    }

    /// Springs to `target` with the ambient transaction's motion (or the
    /// coordinator's scheme default). Interrupting a running spring preserves
    /// velocity by construction (Twell's additive impulse superposition) —
    /// there is no bookkeeping to get wrong.
    void set_value(ValueType target) {
        Transaction& transaction = Transaction::current();
        if (transaction.disables_animation()) {
            set_value_immediately(target);
            return;
        }
        set_value(target, transaction.default_motion());
    }

    /// Springs to `target` with an explicit motion.
    void set_value(ValueType target, const Motion& motion) {
        model_ = target;
        Transaction::current().note_touched(*coordinator_, index_);
        const Motion resolved = coordinator_->resolve_motion(motion);
        if (resolved.kind() == Motion::Kind::immediate) {
            set_value_immediately(target);
            return;
        }
        double components[3]{};
        Traits::to_components(target, components);
        const AnimationIntent intent{
            .kind = AnimationIntent::Kind::animate_to_target,
            .property_index = index_,
            .dimensionality = Traits::dimensionality,
            .target = {components[0], components[1], components[2]},
            .spring = resolved.spring_configuration(),
            .delay_seconds = resolved.delay().seconds(),
        };
        // The queue is bounded; a failed push means the UI thread outran the
        // compositor by a whole queue. The model value is already updated, so
        // the visual consequence is a missed frame, not a stuck property.
        (void)coordinator_->enqueue_intent(intent);
    }

    /// Snaps both the model and the presentation value; no animation.
    void set_value_immediately(ValueType target) {
        model_ = target;
        double components[3]{};
        Traits::to_components(target, components);
        const AnimationIntent intent{
            .kind = AnimationIntent::Kind::set_immediate,
            .property_index = index_,
            .dimensionality = Traits::dimensionality,
            .target = {components[0], components[1], components[2]},
        };
        (void)coordinator_->enqueue_intent(intent);
    }

    /// True when the compositor reports the property at rest.
    [[nodiscard]] bool is_at_rest() const noexcept {
        return coordinator_->property_at_rest(index_);
    }

    /// Freezes the property where it is on screen.
    void stop_animation_at_presentation_value() {
        const AnimationIntent intent{
            .kind = AnimationIntent::Kind::stop_at_presentation_value,
            .property_index = index_,
            .dimensionality = Traits::dimensionality,
        };
        (void)coordinator_->enqueue_intent(intent);
    }

    /// Fires `callback` once, on the UI thread, when the property next
    /// reaches rest (driven by Twell's rest queue, deferred off the
    /// compositor — docs/05-animation-and-twell.md §3.2 step 4).
    void on_reach_rest(std::function<void()> callback) {
        (void)coordinator_->on_property_reach_rest(index_, std::move(callback));
    }

    /// The property's index in the coordinator (diagnostics).
    [[nodiscard]] std::uint32_t property_index() const noexcept { return index_; }

private:
    AnimatableProperty(AnimationCoordinator& coordinator, std::uint32_t index)
        : coordinator_(&coordinator), index_(index) {}

    AnimationCoordinator* coordinator_ = nullptr;
    std::uint32_t index_ = 0;
    ValueType model_{};
};

using AnimatableFloat = AnimatableProperty<float>;
using AnimatablePoint = AnimatableProperty<geometry::Point>;
using AnimatableVector3 = AnimatableProperty<geometry::Vector3>;

} // namespace ca::animation
