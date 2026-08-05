# Calcium — Animation Architecture and Twell Integration

The killer feature, specified concretely against Twell's actual API.

---

## 1. What Twell already provides

From `third_party/twell/twell.h` (1632 lines, complete):

| Capability | Twell symbol |
|---|---|
| Analytical springs (under/critical/overdamped closed form) | `twell_math_spring_evaluate` |
| Viscous fluid decay | `twell_math_decay_evaluate` |
| Asymptotic rubber banding | `twell_math_rubber_band` |
| Additive impulse superposition (lossless interruption) | ring buffer, `TWELL_MAX_IMPULSES = 8` |
| Model / presentation value split | `..._get_model_value` / `..._get_presentation_value` |
| Gesture kinetics with velocity history | `twell_gesture_*`, 16-sample ring |
| Momentum handoff | `twell_property_release_gesture_spring` / `_decay` |
| Coupled kinematics | `twell_property_add_driver` |
| Unit-aware rest thresholds | `twell_unit_type`, per-property epsilons |
| Rest reporting | `twell_context_tick` out-parameter queue |
| 3D spatial geometry | `twell_transform3d`, `twell_quaternion`, slerp |
| Arena allocation, zero malloc | `twell_get_memory_requirement` |
| SoA internal layout | `twell_context` scalar arrays |

Calcium adds no physics. It adds a threading model, a transaction model, a type
system, and a tree that this kernel drives.

---

## 2. The property mapping

```cpp
// src/animation/animatable_property_impl.hpp  (internal — twell_* never public)

template <typename ValueType>
class AnimatablePropertyStorage {
    twell_property_id twell_id_;      // 1D/2D/3D by ValueType
    AnimationCoordinator* coordinator_;
    ValueType cached_presentation_;   // published by compositor, read by UI thread
};
```

| Calcium type | Twell representation |
|---|---|
| `float`, `double` | `twell_property_create_with_unit` (1D) |
| `Point`, `Size`, `Vector2` | `twell_property_create_2d_with_unit` |
| `Vector3` | `twell_property_create_3d_with_unit` |
| `Color` | 4 × 1D in Oklab (L, a, b, alpha) — perceptual interpolation |
| `Transform3D` | **decomposed**: 3D translation + 3D scale + quaternion + 1D perspective |
| `EdgeInsets` | 4 × 1D |
| `RoundedRectangle` | 4 radii as 4 × 1D + bounds as 2 × 2D |

### 2.1 Why `Transform3D` is decomposed rather than animated as a matrix

Component-wise interpolation of a 4×4 matrix is wrong. Lerping between two
rotation matrices passes through non-rotation matrices, producing visible shear
and scale collapse — a 180° rotation lerps through a degenerate matrix.

Calcium decomposes into translation, scale, skew, rotation-as-quaternion, and
perspective; interpolates each in its natural space (quaternion via
`twell_quaternion_slerp`, which Twell provides); then recomposes. This is what
Core Animation does, and it is why rotating a card on iOS looks correct and
rotating one in a naive framework looks like it is being crushed.

### 2.2 Why color interpolates in Oklab

Interpolating in sRGB passes through desaturated grey — blue-to-yellow goes
through mud. Oklab is perceptually uniform, so a spring from blue to yellow
stays saturated along the whole path. The cost is a color conversion at
animation start and end, not per frame.

---

## 3. The coordinator

```cpp
namespace ca::animation {

class AnimationCoordinator {   // one per Application
public:
    struct Configuration {
        uint32_t max_animated_properties = 4096;
        uint32_t max_concurrent_gestures = 64;
    };

    // Arena is sized by asking Twell, never by guessing.
    static core::Result<std::unique_ptr<AnimationCoordinator>> create(Configuration);

    // === COMPOSITOR THREAD ONLY ===
    void tick_and_publish(core::Timestamp predicted_presentation_time);

    // === UI THREAD ONLY ===
    void enqueue_intent(const AnimationIntent&);
    void commit_pending_intents();
    [[nodiscard]] bool has_active_animations() const noexcept;   // idle gating

private:
    std::unique_ptr<std::byte[]> arena_;
    twell_context* context_;                        // compositor-owned

    // UI → compositor: lock-free, bounded, single-producer/single-consumer.
    core::LockFreeRingBuffer<AnimationIntent, 4096> intent_queue_;

    // compositor → UI: triple-buffered presentation snapshot, seqlock-published.
    TripleBuffered<PresentationSnapshot> published_snapshot_;
};

} // namespace ca::animation
```

### 3.1 Intents

The UI thread never calls Twell. It appends a small POD describing what it wants,
and the compositor applies it at the start of its next tick — at which point it
has a valid `absolute_time` to hand Twell.

```cpp
struct AnimationIntent {
    enum class Kind : uint8_t {
        animate_to_target, animate_decay, set_immediate,
        begin_gesture_tracking, release_to_spring, release_to_decay,
        add_property_link, remove_property_link, stop_at_presentation_value
    };

    Kind              kind;
    twell_property_id property;
    Dimensionality    dimensionality;
    double            target[3];
    SpringConfiguration spring;
    double            deceleration_rate;
    double            boundary_min[3], boundary_max[3];
    twell_gesture_id  gesture;
    uint32_t          sequence_number;    // preserves commit ordering
};
```

Two properties matter:

- **POD, fixed-size, no pointers.** The queue is a flat ring buffer; enqueueing
  cannot allocate, so the UI thread's commit path has no allocator on it (P8).
- **Ordered by `sequence_number`.** Intents from a single transaction apply
  contiguously, which is what makes a commit atomic from the compositor's
  perspective (P3).

### 3.2 The compositor tick, in full

```cpp
void AnimationCoordinator::tick_and_publish(core::Timestamp t_present) {
    CA_ASSERT_COMPOSITOR_THREAD();
    const double t = t_present.seconds_since_epoch();

    // 1. Apply everything the UI thread committed since last tick, in order.
    //    Applying at t_present means an interrupting spring's initial velocity
    //    is sampled at the exact instant the frame will be seen.
    AnimationIntent intent;
    while (intent_queue_.try_dequeue(intent)) {
        apply_intent(intent, t);
    }

    // 2. Advance the analytical solution. Resting properties are reported.
    twell_property_id resting[256];
    const uint32_t resting_count =
        twell_context_tick(context_, t, resting, 256);

    // 3. Publish a coherent snapshot for the UI thread to read (seqlock).
    auto& snapshot = published_snapshot_.begin_write();
    for (auto& entry : registered_properties_) {
        entry.write_presentation_value_into(snapshot, context_);
    }
    published_snapshot_.end_write();

    // 4. Rest callbacks are deferred to the UI thread — user code never runs
    //    on the compositor.
    for (uint32_t i = 0; i < resting_count; ++i) {
        post_rest_notification_to_ui_thread(resting[i]);
    }
}
```

Step 4 is a firm rule: **no application code ever executes on the compositor
thread.** A user callback that takes 3 ms would drop a frame directly, defeating
the entire architecture.

---

## 4. Idle power

Twell's rest queue gives this nearly free, and it matters as much on a laptop as
on a phone.

```cpp
bool AnimationCoordinator::has_active_animations() const noexcept {
    return active_property_count_ > 0 || active_gesture_count_ > 0;
}
```

The compositor's run loop:

```
if (coordinator.has_active_animations() || packet_is_dirty) {
    request_next_vsync_callback();      // animating: 120 Hz
} else {
    suspend_until_input_or_invalidation();   // fully at rest: 0 Hz
}
```

A fully settled UI consumes no CPU and no GPU. When the last property reports
rest, the compositor stops waking. This is why an idle Calcium application should
not appear in a battery-usage report at all.

---

## 5. The motion scheme (P9)

```cpp
namespace ca::animation {

struct MotionScheme {
    SpringConfiguration standard;
    SpringConfiguration emphasized;
    SpringConfiguration snappy;
    SpringConfiguration gentle;
    SpringConfiguration playful;

    double scroll_deceleration_rate;
    double fast_scroll_deceleration_rate;
    double rubber_band_tension;
    double flick_velocity_threshold;

    [[nodiscard]] static MotionScheme apple_like();
    [[nodiscard]] static MotionScheme material_like();
    [[nodiscard]] static MotionScheme platform_default();
    [[nodiscard]] static MotionScheme reduced_motion();   // honors OS setting
};

} // namespace ca::animation
```

The default table, tuned to match iOS characteristics:

| Motion | mass | stiffness | damping | ζ | Response | Use |
|---|---|---|---|---|---|---|
| `standard` | 1.0 | 250 | 31.6 | 1.00 | ~0.40 s | Everything by default |
| `emphasized` | 1.0 | 200 | 24.0 | 0.85 | ~0.50 s | Hero transitions, sheets |
| `snappy` | 1.0 | 480 | 44.0 | 1.00 | ~0.28 s | Toggles, buttons, selection |
| `gentle` | 1.0 | 120 | 21.9 | 1.00 | ~0.60 s | Large surfaces, backgrounds |
| `playful` | 1.0 | 300 | 18.0 | 0.52 | ~0.55 s | Notifications, pull-to-refresh |

Scroll constants:

| Constant | Value | Notes |
|---|---|---|
| `scroll_deceleration_rate` | 0.998 | matches `UIScrollView` normal |
| `fast_scroll_deceleration_rate` | 0.99 | matches `UIScrollView` fast |
| `rubber_band_tension` | 0.55 | matches iOS overscroll resistance |
| `flick_velocity_threshold` | 250 pt/s | above this, velocity decides destination |

### 5.1 Reduced motion

`MotionScheme::reduced_motion()` replaces springs with short critically damped
motions (ζ = 1, response ≈ 0.15 s) and disables parallax property links. It does
**not** disable animation entirely — instant state changes are disorienting and
also fail accessibility guidance. The OS setting is observed and the scheme swaps
live, animating the swap itself.

---

## 6. Rest thresholds and display scale

Twell's pixel defaults (ε_d = 0.1, ε_v = 0.5) assume points. On a 3× display,
0.1 pt is 0.3 device pixels — an animation can visibly stop short of its target.

Calcium scales the thresholds by the display's scale factor when a property is
attached to a window:

```cpp
void AnimatablePropertyStorage::update_rest_thresholds_for_display(
    const platform::Display& display)
{
    const double scale = display.scale_factor();
    twell_property_set_rest_thresholds(
        context_, twell_id_,
        /* displacement */ 0.1 / scale,     // ≈0.033 pt on a 3× display
        /* velocity     */ 0.5 / scale);
}
```

This is a small detail with a disproportionate effect: it is the difference
between animations that land and animations that almost land.

---

## 7. Gesture kinetics and momentum handoff

```cpp
namespace ca::animation {

class GestureKinetics {
public:
    void add_sample(geometry::Point position, core::Timestamp);

    [[nodiscard]] geometry::Point velocity() const;         // pt/s, from the 16-sample ring
    [[nodiscard]] geometry::Point projected_landing_position(double deceleration_rate) const;

    void reset();
private:
    twell_gesture_id twell_id_;
};

} // namespace ca::animation
```

`projected_landing_position` is what makes snapping feel intentional: rather than
deciding the destination from the release *position*, decide it from where the
content *would have landed* under decay, then spring to the nearest snap point.
This is how `UIScrollView` paging and Picker wheels behave, and it is the
difference between "it snapped somewhere" and "it snapped where I threw it".

```
projected = current + velocity × (deceleration_rate / (1 − deceleration_rate)) × dt
```

### 7.1 Handoff sequence

```
[finger down]
  → PanGestureRecognizer::began
  → property.begin_tracking_gesture(kinetics, lower, upper)
  → twell_property_track_gesture_2d(...)

[finger moves]
  → each platform event: kinetics.add_sample(position, timestamp)
  → twell_gesture_add_touch_2d(...)
  → COMPOSITOR: presentation value tracks 1:1, rubber-banded past bounds

[finger up]
  → kinetics.velocity() → decide destination
  → property.end_tracking_with_spring(kinetics, target, motion)
  → twell_property_release_gesture_spring_2d(...)
  → Twell seeds the spring's initial velocity from gesture history
```

The velocity is never zero at release and never re-derived by application code.
That continuity is the whole feeling.

---

## 8. Coupled kinematics

```cpp
// Collapsing header driven by scroll offset. No per-frame code.
auto header_height_link = anim::PropertyLink::connect(
    scroll_view->content_offset().y_component(),
    header_layer.height_property(),
    { .source_lower_bound = 0.0,   .source_upper_bound = 120.0,
      .target_lower_bound = 200.0, .target_upper_bound = 64.0,
      .curve = anim::MappingCurve::ease_out, .clamps_to_bounds = true });

// Same source, second target: title fades as the header collapses.
auto title_opacity_link = anim::PropertyLink::connect(
    scroll_view->content_offset().y_component(),
    header_title_layer.opacity(),
    { .source_lower_bound = 0.0, .source_upper_bound = 80.0,
      .target_lower_bound = 1.0, .target_upper_bound = 0.0,
      .curve = anim::MappingCurve::linear, .clamps_to_bounds = true });
```

Twell reads the source's **presentation** value, so linked properties track what
the user actually sees, glued to the finger — not the logical target. This is
exactly the iOS scroll-linked behavior.

### 8.1 Two constraints inherited from Twell, surfaced in the API

Twell's documentation is explicit about these, and Calcium must not paper over
them:

1. **A driven property is overwritten every tick**, which clears any spring on it.
   So a property may not be both linked and independently animated. `PropertyLink::connect`
   returns `CA_ERROR_INVALID_ARGUMENT` if the target already has an animation, and
   `AnimatableProperty::set_value` on a linked property fails loudly in debug.
2. **Driver links are scalar → scalar.** Multi-dimensional coupling requires one
   link per component. Calcium exposes `.x_component()` / `.y_component()` /
   `.z_component()` accessors on multi-dimensional properties, which map to
   Twell's adjacent component handles, and documents 1D targets as the norm.

### 8.2 Impulse budget

`TWELL_MAX_IMPULSES = 8` per property. Eight superposed impulses is far beyond
what any real interaction produces (a frantic user retargeting a spring generates
maybe three), but a runaway loop calling `set_value` every frame would exhaust it.
Twell's ring buffer evicts the oldest, which degrades gracefully. Calcium adds a
debug warning when a property exceeds four concurrent impulses, because that
almost always indicates the application is animating in a loop where it should be
setting a target once.

---

## 9. Where springs are the wrong tool

Honesty about scope. Springs are the right default for *state transitions*. They
are the wrong tool for:

- **Continuous looping animation** (spinners, pulsing, indeterminate progress) —
  use `KeyframeTrack` with a periodic driver.
- **Timeline-authored motion** (a designer-authored multi-stage sequence with
  exact timings) — use `KeyframeTrack` with explicit easing.
- **Exact-duration choreography** (a video export overlay that must last 500 ms
  exactly) — springs have an asymptotic settling time; use a keyframe track.

`ca::animation::KeyframeTrack` exists for these and is a first-class citizen, not
a legacy fallback. It shares the same transaction, threading, and presentation
model — it is simply evaluated by a different kernel that Calcium owns rather than
by Twell.

```cpp
class KeyframeTrack {
public:
    KeyframeTrack& add_keyframe(double normalized_time, double value, EasingCurve);
    KeyframeTrack& set_duration(core::Duration);
    KeyframeTrack& set_repeat_behavior(RepeatBehavior);
    KeyframeTrack& set_autoreverses(bool);
};
```
