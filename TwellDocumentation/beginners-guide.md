# Twell 101

Hi! If you've ever felt that standard CSS transitions (like `ease-in-out` or cubic beziers) feel a bit lifeless, stiff, or "snappy" when interrupted, you're in the right place. 

This guide is designed for beginners. We'll start by explaining the core physics concepts used in Twell in plain English, and then walk through the public API to show you how to start building fluid interfaces.

---

## Part 1: Physics stuff

Before we write code, let's understand the "physics" that Twell simulates. Twell doesn't draw anything on the screen; it just does math to calculate *where* things should be over time.

### Critically damped springs
**What it is:** Imagine a spring that is damped *just enough* so it never bounces. It quickly accelerates towards its target, and then smoothly decelerates as it arrives, stopping exactly on the target without overshooting.
**When to use it:** This is the workhorse of UI animation. Use it for almost everything: opening menus, sliding panels, hover effects, or toggling switches. It feels natural, fast, and responsive without being distracting. Btw, **this is the default iOS UI animation style!**

### Underdamped springs
**What it is:** A responsive bouncy spring. It shoots past its target, pulls back, and wobbles a bit before finally settling down. 
**When to use it:** Use this when you want a playful, energetic feel. It's perfect for pop-ups, notification bubbles, or when a user drops a draggable item and it snaps back into its resting place.

### Viscous fluid decay (aka Inertial scrolling)
**What it is:** Imagine sliding a puck on ice. It starts with a certain speed and slowly coasts to a halt due to friction. 
**When to use it:** This is exactly how scrolling works on a smartphone. You flick the screen, and the list keeps moving, slowing down naturally. Use this for scrollable lists, maps, or any object the user can "throw" across the screen.

### Rubber-banding
**What it is:** When you pull a rubber band, it stretches easily at first, but gets harder and harder to stretch the further you pull.
**When to use it:** This is used for boundary limits (overscroll). When a user scrolls to the top of a list and keeps dragging, the list should move, but with increasing resistance. When they let go, an *Underdamped Spring* pulls it back to the edge.

### Coupled kinematics (driven properties)
**What it is:** Connecting one physical property to another. If property A moves, property B reacts automatically.
**When to use it:** Imagine a bottom sheet you can drag up. As you drag the sheet up (Property A), you want the main app background (Property B) to shrink down smoothly. You link them together, so the physics of your finger rippling through the whole UI.

---

## Part 2: How to Twell

Twell introduces a few concepts to ensure animations are perfectly smooth and never glitch when you interrupt them.

### Bring Your Own Memory
Twell never asks the operating system for memory (malloc). Instead, when you start your app, you give Twell a big chunk of memory (an "arena") and say, "Use this space for all your math things." This ensures Twell is lightning fast and works perfectly in embedded systems or game consoles. (The Python wrapper handles this for you under the hood btw.)

### Units and resting (When does it stop?)
Mathematically, a spring never truly stops - it just gets infinitely smaller.  
To save CPU cycles, Twell uses Resting Thresholds (epsilons). When an animation gets slow enough and close enough to the target, Twell forces it to stop and marks it as "resting".
When you create a property, you can give it a **Unit Type** (like `Pixels`, `Normalized`, `Degrees`, or `Radians`). Twell automatically configures the best resting thresholds for that unit:

| Unit | Distance epsilon | Velocity epsilon |
| --- | --- | --- |
| Pixels | 0.1 px | 0.5 px/s |
| Normalized | 0.0001 | 0.001 /s |
| Degrees | 0.05° | 0.1°/s |
| Radians | 0.001 rad | 0.005 rad/s |

A property is "at rest" when **both** its distance from the target *and* its velocity drop below those thresholds. At that moment Twell snaps the presentation value to the target, marks the property as resting, and reports it in the list returned by `tick()` — that's your signal to free or reuse the property. Resting is per-property: override it with `twell_property_set_rest_thresholds`, or change the defaults for all properties created afterwards with `twell_math_set_rest_thresholds`. (Pixels are treated as *points*: on a 2x display you may want a distance epsilon of ~0.05 so animations don't stop a pixel early.)

### Model and Presentation values
When animating, an object actually has *two* states:
1. **Model value** is the **destination**. Where the object *logically* is or is going. If you click a button to open a menu, the menu's model value instantly becomes "Open".
2. **Presentation value** is the **journey**. Where the object *physically* is on the screen right now. It takes time for the presentation value to catch up to the model value. 

When you tell your graphics framework to draw a rectangle, you always draw it at the **Presentation value**.

### Additive state machine (for continous interruptions)
If a menu is halfway open, and the user suddenly clicks "Close", what happens? In bad UI frameworks, the menu instantly snaps, or its speed awkwardly resets.  
Twell uses an "additive state machine." This means if you interrupt an animation, Twell perfectly preserves the current velocity and momentum. The old animation smoothly blends into the new one. It feels incredibly fluid, and Twell handles it automatically.

---

## Part 3: Writing code

Twell is framework-agnostic. To use it, you just need a loop (like a game loop or a render loop) that runs every frame.

First, you create a context. In C, you must provide the memory manually. In Python, the wrapper already does it for you.

**In C:**
```c
#define TWELL_IMPL
#include "twell.h"

// Size the arena with twell_get_memory_requirement(), never by guessing:
// 128 properties + 32 gestures needs 189,888 bytes. This 256KB buffer is
// comfortably above that; if the arena is too small, context creation
// returns NULL.
uint8_t twell_arena[256 * 1024];

// Create the context (max 128 properties, max 32 gestures)
twell_context* ctx = twell_context_create(twell_arena, sizeof(twell_arena), 128, 32);
```

**In Python:**
```python
from twell import Context

ctx = Context(max_properties=128, max_gestures=32)
```

Let's create a property.  
A property is a number (1D, 2D, or 3D) that Twell tracks. You create it, optionally specifying the unit (Pixels is the default), and get an ID or object back.

**In C:**
```c
// Create a 2D position property (e.g., X and Y coordinates for a card)
twell_vector2 start_pos = { 100.0, 100.0 };
twell_property_id card_pos = twell_property_create_2d_with_unit(ctx, start_pos, TWELL_UNIT_PIXELS);
```

**In Python:**
```python
from twell import Vector2
start_pos = Vector2(100.0, 100.0)
card_pos = ctx.create_property_2d(start_pos, unit=UnitType.PIXELS)
```

In your app's main loop, you must tell Twell what time it is, tick the engine, and then ask for the presentation value to draw your objects. The `tick` function also returns a list of properties that just finished animating, which is useful if you want to delete them later!

**In C:**
```c
while (app_is_running) {
    // get the current absolute time in seconds (e.g., GetTime() in Raylib)
    double current_time = get_absolute_time();
    
    // tick the physics engine (we provide an array to catch resting properties)
    twell_property_id resting_props[16];
    uint32_t resting_count = twell_context_tick(ctx, current_time, resting_props, 16);
    
    // get the presentation value
    twell_vector2 render_pos = twell_property_get_presentation_value_2d(ctx, card_pos);
    
    // draw!
    draw_rectangle(render_pos.x, render_pos.y, 50, 50);
}
```

**In Python:**
```python
while app_is_running:
    current_time = get_absolute_time()
    
    # tick returns a list of properties that went to sleep this frame
    resting_props = ctx.tick(current_time)
    
    # get presentation value
    render_pos = card_pos.presentation_value
    
    # draw!
    draw_rectangle(render_pos.x, render_pos.y, 50, 50)
```

To make the card move, you configure a spring and tell the property to animate to a new target.

**In C:**
```c
twell_spring_config spring = {
    .mass = 1.0,
    .stiffness = 250.0,
    .damping = 18.0, 
    .initial_velocity = 0.0 // Twell sets this for you if you're interrupting an animation
};

// when a user clicks a button:
twell_vector2 new_target = { 500.0, 500.0 };
twell_property_animate_to_target_2d(ctx, card_pos, new_target, spring, current_time);
```

**In Python:**
```python
from twell import SpringConfig
spring = SpringConfig(mass=1.0, stiffness=250.0, damping=18.0)

new_target = Vector2(500.0, 500.0)
card_pos.animate_to_target(new_target, spring, current_time)
```

Twell will instantly handle the physics. As your loop continues to run `tick`, the presentation value will smoothly bounce its way to `500.0, 500.0`.

But what if you want the user to drag the card with their mouse?
You create a `gesture`, feed it mouse coordinates, and link it to the property.

**In C:**
```c
twell_gesture_id drag_gesture = twell_gesture_create(ctx);

// inside your main loop, when the mouse is pressed and dragging:
twell_vector2 mouse_pos = get_mouse_position();
twell_gesture_add_touch_2d(ctx, drag_gesture, mouse_pos, current_time);

// lock the property to the gesture (1:1 tracking) with rubber-band boundaries
twell_vector2 bounds_min = { 0.0, 0.0 };
twell_vector2 bounds_max = { 800.0, 600.0 };
twell_property_track_gesture_2d(ctx, card_pos, drag_gesture, bounds_min, bounds_max);
```

When the user lets go, you release the tracking and hand it off to a spring. Twell automatically grabs the speed of the mouse flick and transfers it to the spring for perfect momentum!

```c
// when the mouse is released:
twell_property_release_gesture_spring_2d(ctx, card_pos, drag_gesture, target_center, spring, current_time);
```

**In Python:**
```python
drag_gesture = ctx.create_gesture()

# Inside loop while dragging:
mouse_pos = get_mouse_position() # e.g. a Vector2
drag_gesture.add_touch(mouse_pos, current_time)

bounds_min = Vector2(0.0, 0.0)
bounds_max = Vector2(800.0, 600.0)
card_pos.track_gesture(drag_gesture, bounds_min, bounds_max)

# on release:
card_pos.release_gesture_spring(drag_gesture, target_center, spring, current_time)
```

### Driven Properties (coupled kinematics)
To link two properties, just add a driver. A driver makes one property (the **driven** one) continuously follow another (the **driver** one) — no springs involved.

Here's what happens on every `tick()`:

1. Twell reads the driver's **presentation value** (its on-screen position, not its target), so the coupling tracks what the user actually sees — exactly like a scroll-linked effect on iOS.
2. It normalizes that value between `driver_min` and `driver_max` to get `u` in `[0, 1]`. If `clamp` is true, values outside the range clamp to the ends; otherwise the mapping extrapolates beyond them.
3. It optionally reshapes `u` with the mapping curve: `LINEAR` passes it through, `EASE_IN` squares it (`u²`), `EASE_OUT` uses `u(2 - u)`.
4. It scales the result between `driven_min` and `driven_max`, and sets the driven property **immediately** — synchronously, in the same tick.

That's the "bottom sheet" example from Part 1: as the sheet's Y property moves from 600 to 0, a `bg_scale` property is remapped from 1.0 to 0.8 — the background shrinks in perfect sync with your finger.

**In C:**
```c
twell_property_id bg_scale = twell_property_create(ctx, 1.0);

// make bg_scale shrink from 1.0 to 0.8 as a vertical scroll (card_pos_y) moves from 600 to 0.
// Note: each driver link maps one scalar to one scalar (the driver value, the
// ranges, and the curve all describe a single number). To couple a 2D or 3D
// property, attach one driver per component handle: Twell lays 2D/3D
// properties out as adjacent handles, so `prop_id + 1` and `prop_id + 2`
// address the Y and Z components. In practice, keep driven properties 1D.
// One caution: a driven property is overwritten synchronously every tick,
// which clears any spring that was running on it — don't animate a driven
// property; drivers own their values completely.
twell_property_add_driver(
    ctx, 
    bg_scale,           // driven property ID
    card_pos_y,         // driver property ID
    600.0, 0.0,         // driver min/max
    1.0, 0.8,           // driven min/max
    TWELL_MAP_LINEAR,   // mapping curve
    true                // clamp
);
```

**In Python:**
```python
from twell import MappingCurve

bg_scale = ctx.create_property(1.0)
bg_scale.add_driver(
    card_pos_y,
    600.0, 0.0,
    1.0, 0.8,
    MappingCurve.LINEAR,
    True
)
```

## So...

1. Don't animate by manually adding to X and Y every frame. 
2. Instead, set **Targets** using Springs. Let Twell do the math.
3. For user input, map touch data to **Gestures**, track them 1:1, and hand them off to Springs when released.
4. Draw whatever `twell_property_get_presentation_value` tells you to draw.
