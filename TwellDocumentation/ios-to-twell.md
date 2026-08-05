# iOS to Twell

If you've spent your career building your buttery-smooth, interruptible, physics-driven interfaces in iOS using UIKit/SwiftUI, and CoreAnimation, **you already know how to use Twell.**  

Twell was designed from the ground up as "CoreAnimation at home". It takes the closed-source mathematical magic of Apple's UI frameworks and extracts it into a portable C library.

This guide maps the paradigms you know from iOS directly to Twell.

---

In iOS, if you animate a `UIView` across the screen, checking its `.frame` mid-animation gives you the final destination, not where it is currently drawing. To find its on-screen pixels, you check its `layer.presentation()`.

Twell uses the **exact same** separation. A `twell_property` represents an animatable value (like a float, Vector2, or Vector3) and has two states:

**iOS (CoreAnimation):**
```swift
// The destination
let dest = view.layer.position
// The current on-screen location
let current = view.layer.presentation()?.position
```

**Twell:**
```c
// The destination
twell_vector2 dest = twell_property_get_model_value_2d(ctx, prop_id);
// The current on-screen location
twell_vector2 current = twell_property_get_presentation_value_2d(ctx, prop_id);
```
*(Note: There is also an object-oriented Python wrapper where you can just call `prop.model_value` and `prop.presentation_value`)*

In iOS 8+, Apple introduced "additive animations". If a view is moving left and you suddenly tell it to move right, it doesn't jarringly change speed. Instead, a new animation from `(old_target - new_target) to 0` is added to the stack, blending seamlessly with the old one to preserve momentum.

Twell implements this exact **Additive state machine**. You do not need to calculate current velocity when retargeting a property. 

**iOS (UIKit):**
```swift
// Automatically handles additive momentum preservation
UIView.animate(withDuration: 0.5, delay: 0, usingSpringWithDamping: 1.0, initialSpringVelocity: 0) {
    view.center = newTarget
}
```

**Twell:**
```c
// Automatically handles additive momentum preservation
twell_spring_config ios_default = { .mass = 1.0, .stiffness = 100.0, .damping = 20.0, .initial_velocity = 0.0 };
twell_property_animate_to_target_2d(ctx, prop_id, new_target, ios_default, current_time);
```
*(Notice how `initial_velocity` is 0.0? Just like iOS, the momentum continuity comes from the mathematical superposition of the old, decaying spring, not from injecting velocity into the new one).*

## UIPanGestureRecognizer

Bridging touch input to physics is tricky. In iOS, you use a `UIPanGestureRecognizer`, track translations, and on release, extract the velocity to feed into a `CASpringAnimation` or `UIScrollView`.

In Twell, a `gesture` object does exactly what a `UIPanGestureRecognizer` does: it tracks touch history, applies a low-pass filter to smooth sensor noise, and calculates release velocity.

**iOS (UIKit):**
```swift
@objc func handlePan(_ gesture: UIPanGestureRecognizer) {
    let translation = gesture.translation(in: view)
    let velocity = gesture.velocity(in: view)
    
    if gesture.state == .changed {
        // Apply 1:1 tracking manually
        card.center = CGPoint(x: start.x + translation.x, y: start.y + translation.y)
    } else if gesture.state == .ended {
        // Handoff to spring
        let springParams = UISpringTimingParameters(dampingRatio: 0.7, initialVelocity: CGVector(dx: velocity.x, dy: velocity.y))
        // ... execute animation
    }
}
```

**Twell:**
```c
if (is_dragging) {
    // feed raw mouse/touch to the gesture tracker
    twell_gesture_add_touch_2d(ctx, drag_gesture, current_mouse_pos, current_time);
    
    // lock the property to the gesture (1:1 tracking)
    twell_property_track_gesture_2d(ctx, card_prop, drag_gesture, bounds_min, bounds_max);
} else if (just_released) {
    // seamless handoff! Twell extracts the gesture velocity and fires the spring
    twell_property_release_gesture_spring_2d(ctx, card_prop, drag_gesture, rest_target, bouncy_spring, current_time);
}
```

## UIScrollView

### Inertial scrolling
When you flick a `UIScrollView`, it coasts to a natural stop using viscous fluid decay (`UIScrollView.DecelerationRate.normal`).

In Twell, use `twell_property_release_gesture_decay` when releasing a pan gesture. Twell will extract the flick velocity and apply an exponential decay curve exactly like a scroll view.

### Rubber-banding (overscroll)
When you pull a `UIScrollView` past its content bounds, it resists asymptotically.

In Twell, this is built right into `twell_property_track_gesture`. When you lock a property to a gesture, you pass in `boundary_min` and `boundary_max`. If the user drags outside those bounds, Twell automatically applies the exact same rational function resistance used in iOS.

## CADisplayLink

In iOS, CoreAnimation ticks magically in the background. If you need to sync custom drawing to the screen refresh, you use a `CADisplayLink`.

Because Twell is headless and I/O agnostic, **you own the `CADisplayLink`**. You must tick the engine yourself inside your framework's render loop (e.g., a Raylib `while(!WindowShouldClose())` loop, a Pygame loop, or a WebGL `requestAnimationFrame`).

Just like `CACurrentMediaTime()`, Twell requires **absolute hardware time** (not delta time). 

```c
// Your framework's game/render loop
while (true) {
    // equivalent to CACurrentMediaTime()
    double time = get_absolute_time();
    
    // tick twell
    twell_context_tick(ctx, time, NULL, 0);
    
    // draw
    draw_stuff();
}
```

## CATransform3D

Twell provides a `twell_transform3d` struct that is a 1:1 layout match with `CATransform3D`. It supports the classic `m34 = -1.0 / d` perspective trick to achieve 2.5D depth.

Instead of directly animating matrices (which causes shearing), you animate a scalar property (0.0 to 1.0) and use `twell_quaternion_slerp` to generate the rotation matrix during your render loop.

## Cheat sheet

| iOS | Twell |
| --- | --- |
| `layer.position` (destination) | `twell_property_get_model_value` |
| `layer.presentation()` (current pixels) | `twell_property_get_presentation_value` |
| `CACurrentMediaTime()` | your loop's `get_absolute_time()` |
| `UIView.animate(...)` | `twell_property_animate_to_target` |
| Additive Animations | automatic via Twell's internal ring buffers |
| `CASpringAnimation` | `twell_spring_config` + `animate_to_target` |
| `UIPanGestureRecognizer` | `twell_gesture_id` |
| `gesture.translation(in: view)` | `twell_property_track_gesture` (1:1 lock) |
| `UIScrollView` rubber-banding | passed as `bounds` to `track_gesture` |
| `UIScrollView.DecelerationRate` | `twell_property_release_gesture_decay` |
| `CATransform3D` | `twell_transform3d` |
