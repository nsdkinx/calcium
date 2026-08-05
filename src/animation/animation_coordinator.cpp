#include "calcium/animation/animation_coordinator.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <vector>

#include "calcium/core/thread_affinity.hpp"

#define TWELL_IMPL
#include "twell.h"

namespace ca::animation {

namespace {

constexpr std::uint32_t k_intent_queue_capacity = 4096;
constexpr std::uint32_t k_rest_event_capacity = 1024;
constexpr std::uint32_t k_max_resting_per_tick = 256;

// A property that is waiting out its delay before its spring starts.
struct PendingDelay {
    std::uint32_t property_index = 0;
    double target[3] = {0.0, 0.0, 0.0};
    std::uint32_t dimensionality = 1;
    SpringConfiguration spring;
    double start_time = 0.0;
    bool occupied = false;
};

} // namespace

struct AnimationCoordinator::Impl {
    // --- Compositor-owned ---
    std::unique_ptr<std::byte[]> arena;
    twell_context* context = nullptr;
    ca::core::SpscRing<AnimationIntent, k_intent_queue_capacity> intent_queue;
    std::vector<twell_property_id> twell_ids;
    std::vector<std::uint32_t> dimensionalities;
    std::vector<PendingDelay> pending_delays;
    ca::core::SpscRing<std::uint32_t, k_rest_event_capacity> rest_events;
    std::atomic<std::uint32_t> applied_intent_count_{0};

    // Twell allocates a 2D property across TWO slots (x + y) and a 3D across
    // three, so its slot indices differ from the coordinator's property
    // indices — and the resting queue reports SLOTS. These two vectors map
    // slots back to their owning property and track per-slot rest, so a 2D
    // property reports rest only when every component rests.
    std::vector<std::uint32_t> slot_to_property;
    std::vector<std::uint8_t> slot_at_rest;

    // --- Seqlock-published snapshot (compositor writes, UI reads) ---
    std::atomic<std::uint32_t> sequence_{0};
    std::vector<double> presentation_values;   // property_count * 3
    std::vector<std::uint8_t> at_rest;         // property_count

    // --- UI-thread-owned ---
    MotionScheme motion_scheme = MotionScheme::apple_like();
    std::vector<std::function<void()>> rest_callbacks;  // per property, one slot
    std::vector<std::pair<std::vector<std::uint32_t>, std::function<void()>>>
        pending_completions;
    std::atomic<std::uint32_t> next_sequence_number_{1};

    // Marks every Twell slot of a property as in motion (an animation intent
    // was applied); the property's published at-rest flag follows.
    void mark_active(std::uint32_t property_index) {
        const twell_property_id id = twell_ids[property_index];
        const std::uint32_t dimensionality = dimensionalities[property_index];
        for (std::uint32_t slot = id; slot < id + dimensionality; ++slot) {
            slot_at_rest[slot] = 0;
        }
        at_rest[property_index] = 0;
    }

    // Marks every Twell slot of a property as resting (set_immediate /
    // stop_at_presentation); the property's published at-rest flag follows.
    void mark_at_rest(std::uint32_t property_index) {
        const twell_property_id id = twell_ids[property_index];
        const std::uint32_t dimensionality = dimensionalities[property_index];
        for (std::uint32_t slot = id; slot < id + dimensionality; ++slot) {
            slot_at_rest[slot] = 1;
        }
        at_rest[property_index] = 1;
    }
};

// ---------------------------------------------------------------------------
// Seqlock helpers (SPSC: compositor writes, UI thread reads)
// ---------------------------------------------------------------------------



// ---------------------------------------------------------------------------
// Coordinator
// ---------------------------------------------------------------------------

core::Result<std::unique_ptr<AnimationCoordinator>> AnimationCoordinator::create(
    const Configuration& configuration) {
    const size_t arena_size = twell_get_memory_requirement(
        configuration.max_animated_properties, configuration.max_concurrent_gestures);
    auto impl = std::make_unique<Impl>();
    impl->arena = std::make_unique<std::byte[]>(arena_size);
    impl->context = twell_context_create(
        impl->arena.get(), arena_size, configuration.max_animated_properties,
        configuration.max_concurrent_gestures);
    if (impl->context == nullptr) {
        return core::Result<std::unique_ptr<AnimationCoordinator>>{
            core::ErrorCode::out_of_memory, "Twell arena allocation failed"};
    }
    // Slot bookkeeping is sized by the Twell context's property capacity.
    impl->slot_to_property.resize(configuration.max_animated_properties, 0);
    impl->slot_at_rest.resize(configuration.max_animated_properties, 1);
    return core::Result<std::unique_ptr<AnimationCoordinator>>{
        std::unique_ptr<AnimationCoordinator>{
            new AnimationCoordinator(std::move(impl))}};
}

AnimationCoordinator::AnimationCoordinator(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

AnimationCoordinator::~AnimationCoordinator() = default;

std::uint32_t AnimationCoordinator::applied_intent_count() const noexcept {
    return impl_->applied_intent_count_.load(std::memory_order_relaxed);
}

core::Result<std::uint32_t> AnimationCoordinator::register_property(
    std::uint32_t dimensionality, const double initial[3]) {
    CA_ASSERT_UI_THREAD();
    if (dimensionality != 1 && dimensionality != 2 && dimensionality != 3) {
        return core::Result<std::uint32_t>{
            core::ErrorCode::invalid_argument, "dimensionality must be 1, 2, or 3"};
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>(impl_->twell_ids.size());

    twell_property_id id = 0;
    switch (dimensionality) {
    case 1:
        id = twell_property_create_with_unit(impl_->context, initial[0],
                                             TWELL_UNIT_PIXELS);
        break;
    case 2:
        id = twell_property_create_2d_with_unit(
            impl_->context, twell_vector2{initial[0], initial[1]},
            TWELL_UNIT_PIXELS);
        break;
    default:
        id = twell_property_create_3d_with_unit(
            impl_->context, twell_vector3{initial[0], initial[1], initial[2]},
            TWELL_UNIT_PIXELS);
        break;
    }
    if (id == TWELL_INVALID_ID) {
        return core::Result<std::uint32_t>{
            core::ErrorCode::out_of_memory,
            "Twell property capacity exhausted (max_animated_properties)"};
    }

    impl_->twell_ids.push_back(id);
    impl_->dimensionalities.push_back(dimensionality);
    impl_->pending_delays.push_back({});
    impl_->presentation_values.push_back(initial[0]);
    impl_->presentation_values.push_back(initial[1]);
    impl_->presentation_values.push_back(initial[2]);
    impl_->at_rest.push_back(1);
    impl_->rest_callbacks.emplace_back();

    // Own the property's Twell slots (1 slot for 1D, 2 for 2D, 3 for 3D).
    for (std::uint32_t slot = id; slot < id + dimensionality; ++slot) {
        impl_->slot_to_property[slot] = index;
        impl_->slot_at_rest[slot] = 1;
    }

    // Published with the snapshot's next even sequence so the new slot is
    // visible coherently.
    impl_->sequence_.fetch_add(2, std::memory_order_release);
    return core::Result<std::uint32_t>{index};
}

bool AnimationCoordinator::enqueue_intent(AnimationIntent intent) {
    CA_ASSERT_UI_THREAD();
    // The sequence number is assigned here so commits from one transaction
    // apply contiguously and in order on the compositor (P3).
    intent.sequence_number =
        impl_->next_sequence_number_.fetch_add(1, std::memory_order_relaxed);
    return impl_->intent_queue.try_push(intent);
}

bool AnimationCoordinator::property_at_rest(
    std::uint32_t property_index) const noexcept {
    return property_index < impl_->at_rest.size()
               ? read_property_at_rest(*impl_, property_index)
               : false;
}

void AnimationCoordinator::set_motion_scheme(MotionScheme scheme) {
    CA_ASSERT_UI_THREAD();
    impl_->motion_scheme = scheme;
}

const MotionScheme& AnimationCoordinator::motion_scheme() const noexcept {
    return impl_->motion_scheme;
}

Motion AnimationCoordinator::resolve_motion(Motion motion) const {
    return impl_->motion_scheme.resolve(motion);
}

// Seqlock read helpers. The compositor publishes within a few tens of
// nanoseconds; after eight attempts a torn read is astronomically unlikely,
// and the fallback value is at worst one publish old.

double AnimationCoordinator::read_published_value(
    const Impl& impl, std::uint32_t property_index, std::uint32_t component) {
    const std::size_t slot = static_cast<std::size_t>(property_index) * 3 + component;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = impl.sequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0) {
            continue;  // compositor is mid-publish
        }
        const double value = impl.presentation_values[slot];
        const std::uint32_t after = impl.sequence_.load(std::memory_order_acquire);
        if (after == before) {
            return value;
        }
    }
    return impl.presentation_values[slot];
}

bool AnimationCoordinator::read_property_at_rest(const Impl& impl,
                                                 std::uint32_t property_index) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = impl.sequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0) {
            continue;
        }
        const std::uint8_t value = impl.at_rest[property_index];
        const std::uint32_t after = impl.sequence_.load(std::memory_order_acquire);
        if (after == before) {
            return value != 0;
        }
    }
    return impl.at_rest[property_index] != 0;
}

double AnimationCoordinator::presentation_value(std::uint32_t property_index,
                                                std::uint32_t component) const noexcept {
    return read_published_value(*impl_, property_index, component);
}

bool AnimationCoordinator::has_active_animations() const noexcept {
    // The active count is derived from the published at-rest flags; reading
    // them under the seqlock keeps the answer coherent.
    for (std::size_t index = 0; index < impl_->at_rest.size(); ++index) {
        if (!read_property_at_rest(*impl_, static_cast<std::uint32_t>(index))) {
            return true;
        }
    }
    return false;
}

void AnimationCoordinator::register_completion(
    std::vector<std::uint32_t> properties, std::function<void()> callback) {
    CA_ASSERT_UI_THREAD();
    impl_->pending_completions.emplace_back(std::move(properties),
                                            std::move(callback));
}

void AnimationCoordinator::dispatch_rest_callbacks() {
    CA_ASSERT_UI_THREAD();

    // Drain the rest events the compositor posted.
    std::uint32_t property_index = 0;
    while (impl_->rest_events.try_pop(property_index)) {
        if (property_index < impl_->rest_callbacks.size() &&
            impl_->rest_callbacks[property_index]) {
            auto callback = std::move(impl_->rest_callbacks[property_index]);
            callback();
        }
    }

    // Evaluate pending completion waiters (animate_with_completion): every
    // property the transaction touched must be at rest.
    std::erase_if(impl_->pending_completions, [this](auto& waiter) {
        for (const std::uint32_t index : waiter.first) {
            if (!read_property_at_rest(*impl_, index)) {
                return false;
            }
        }
        waiter.second();
        return true;
    });
}

core::Result<void> AnimationCoordinator::on_property_reach_rest(
    std::uint32_t property_index, std::function<void()> callback) {
    CA_ASSERT_UI_THREAD();
    if (property_index >= impl_->rest_callbacks.size()) {
        return core::Result<void>{
            core::ErrorCode::invalid_argument, "unknown property index"};
    }
    impl_->rest_callbacks[property_index] = std::move(callback);
    return core::Result<void>::success();
}

void AnimationCoordinator::tick_and_publish(core::Timestamp t_present) {
    CA_ASSERT_COMPOSITOR_THREAD();
    const double t = t_present.seconds();

    // 1. Apply everything the UI thread committed since the last tick, in
    //    order. Applying at t_present means an interrupting spring's initial
    //    velocity is sampled at the exact instant the frame will be seen.
    AnimationIntent intent;
    while (impl_->intent_queue.try_pop(intent)) {
        const auto id = impl_->twell_ids[intent.property_index];
        const auto target = twell_vector3{intent.target[0], intent.target[1],
                                          intent.target[2]};

        switch (intent.kind) {
        case AnimationIntent::Kind::animate_to_target: {
            const double start_time = t + intent.delay_seconds;
            if (intent.delay_seconds <= 0.0) {
                const twell_spring_config spring{
                    .mass = intent.spring.mass,
                    .stiffness = intent.spring.stiffness,
                    .damping = intent.spring.damping,
                    .initial_velocity = intent.spring.initial_velocity,
                };
                switch (intent.dimensionality) {
                case 1:
                    twell_property_animate_to_target(impl_->context, id,
                                                     intent.target[0], spring, t);
                    break;
                case 2:
                    twell_property_animate_to_target_2d(
                        impl_->context, id, twell_vector2{target.x, target.y},
                        spring, t);
                    break;
                default:
                    twell_property_animate_to_target_3d(impl_->context, id,
                                                        target, spring, t);
                    break;
                }
                impl_->mark_active(intent.property_index);
            } else {
                // Delayed: stash until the compositor clock reaches start_time.
                auto& pending = impl_->pending_delays[intent.property_index];
                pending.occupied = true;
                pending.property_index = intent.property_index;
                pending.dimensionality = intent.dimensionality;
                pending.target[0] = intent.target[0];
                pending.target[1] = intent.target[1];
                pending.target[2] = intent.target[2];
                pending.spring = intent.spring;
                pending.start_time = start_time;
            }
            break;
        }
        case AnimationIntent::Kind::set_immediate:
            switch (intent.dimensionality) {
            case 1:
                twell_property_set_value_immediate(impl_->context, id,
                                                   intent.target[0]);
                break;
            case 2:
                twell_property_set_value_immediate_2d(
                    impl_->context, id, twell_vector2{target.x, target.y});
                break;
            default:
                twell_property_set_value_immediate_3d(impl_->context, id, target);
                break;
            }
            impl_->pending_delays[intent.property_index].occupied = false;
            impl_->mark_at_rest(intent.property_index);
            break;
        case AnimationIntent::Kind::stop_at_presentation_value: {
            // Freeze where it is: read the presentation value and snap to it.
            // Read and write per dimensionality — a 3D read on a 1D property
            // would pull unallocated neighbor ids.
            switch (intent.dimensionality) {
            case 1:
                twell_property_set_value_immediate(
                    impl_->context, id,
                    twell_property_get_presentation_value(impl_->context, id));
                break;
            case 2: {
                const twell_vector2 current =
                    twell_property_get_presentation_value_2d(impl_->context, id);
                twell_property_set_value_immediate_2d(impl_->context, id, current);
                break;
            }
            default: {
                const twell_vector3 current =
                    twell_property_get_presentation_value_3d(impl_->context, id);
                twell_property_set_value_immediate_3d(impl_->context, id, current);
                break;
            }
            }
            impl_->pending_delays[intent.property_index].occupied = false;
            impl_->mark_at_rest(intent.property_index);
            break;
        }
        }
        impl_->applied_intent_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // 2. Start any delays whose time has come.
    for (auto& pending : impl_->pending_delays) {
        if (!pending.occupied || t < pending.start_time) {
            continue;
        }
        const twell_spring_config spring{
            .mass = pending.spring.mass,
            .stiffness = pending.spring.stiffness,
            .damping = pending.spring.damping,
            .initial_velocity = pending.spring.initial_velocity,
        };
        const auto id = impl_->twell_ids[pending.property_index];
        switch (pending.dimensionality) {
        case 1:
            twell_property_animate_to_target(impl_->context, id,
                                             pending.target[0], spring, t);
            break;
        case 2:
            twell_property_animate_to_target_2d(
                impl_->context, id,
                twell_vector2{pending.target[0], pending.target[1]}, spring, t);
            break;
        default:
            twell_property_animate_to_target_3d(
                impl_->context, id,
                twell_vector3{pending.target[0], pending.target[1],
                              pending.target[2]},
                spring, t);
            break;
        }
        pending.occupied = false;
        impl_->mark_active(pending.property_index);
    }

    // 3. Advance the analytical solutions. Resting properties are reported.
    std::array<twell_property_id, k_max_resting_per_tick> resting{};
    const std::uint32_t resting_count = twell_context_tick(
        impl_->context, t, resting.data(), k_max_resting_per_tick);

    // 4. Publish a coherent snapshot for the UI thread (seqlock).
    const std::uint32_t odd = impl_->sequence_.load(std::memory_order_relaxed) + 1;
    impl_->sequence_.store(odd, std::memory_order_release);

    const std::size_t property_count = impl_->twell_ids.size();
    for (std::size_t index = 0; index < property_count; ++index) {
        const auto id = impl_->twell_ids[index];
        switch (impl_->dimensionalities[index]) {
        case 1:
            impl_->presentation_values[index * 3] =
                twell_property_get_presentation_value(impl_->context, id);
            break;
        case 2: {
            const twell_vector2 value =
                twell_property_get_presentation_value_2d(impl_->context, id);
            impl_->presentation_values[index * 3] = value.x;
            impl_->presentation_values[index * 3 + 1] = value.y;
            break;
        }
        default: {
            const twell_vector3 value =
                twell_property_get_presentation_value_3d(impl_->context, id);
            impl_->presentation_values[index * 3] = value.x;
            impl_->presentation_values[index * 3 + 1] = value.y;
            impl_->presentation_values[index * 3 + 2] = value.z;
            break;
        }
        }
    }

    // 5. Rest events: the resting queue reports TWELL SLOTS (a 2D property
    //    spans two slots); map each back to its property and report rest only
    //    when every component slot rests. Callbacks are posted, never
    //    invoked on the compositor — user code must not run here (docs
    //    05-animation-and-twell.md §3.2 step 4).
    for (std::uint32_t i = 0; i < resting_count; ++i) {
        const std::uint32_t slot = static_cast<std::uint32_t>(resting[i]);
        if (slot >= impl_->slot_to_property.size()) {
            continue;
        }
        const std::uint32_t property_index = impl_->slot_to_property[slot];
        impl_->slot_at_rest[slot] = 1;
        bool all_components_rest = true;
        const twell_property_id id = impl_->twell_ids[property_index];
        const std::uint32_t dimensionality =
            impl_->dimensionalities[property_index];
        for (std::uint32_t s = id; s < id + dimensionality; ++s) {
            if (!impl_->slot_at_rest[s]) {
                all_components_rest = false;
                break;
            }
        }
        if (all_components_rest && !impl_->at_rest[property_index]) {
            impl_->at_rest[property_index] = 1;
            impl_->rest_events.try_push(property_index);
        }
    }

    impl_->sequence_.store(odd + 1, std::memory_order_release);
}

} // namespace ca::animation
