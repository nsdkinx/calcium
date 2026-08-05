# Calcium — Overview

**Calcium is a GPU-composited, physics-native application framework in C++20.**

Apple-like perfection, as portable as Doom.

---

## 1. The thesis

Most GUI frameworks treat animation as a feature layered on top of a rendering
engine. Calcium inverts this. The core abstraction is:

> **A retained layer tree in which every animatable value is an analytically
> solved physical quantity, sampled at the display's presentation timestamp by
> the compositor — not stepped by the application.**

Everything else in Calcium — layout, widgets, the declarative layer, text — is
built on top of that sentence.

### Why this is the right foundation

A conventional framework animates by numerical integration: each frame, the
application advances every animation by `dt`. This has three consequences that
are individually tolerable and collectively fatal to the feeling of quality:

1. **The animation clock is the application clock.** If the UI thread misses a
   frame, animation misses a frame. Smoothness is hostage to the slowest widget
   in the tree.
2. **Interruption is lossy.** Re-targeting mid-flight requires explicitly
   carrying velocity across the boundary. Most frameworks don't, which is why
   interrupted CSS transitions feel dead.
3. **Sampling is fixed.** You get the value at `t = n·dt`, not the value at the
   moment the frame will actually be scanned out.

Twell — the animation kernel, already implemented — is analytical. A spring's
state is a closed-form function of absolute time:

```
x(t) = e^(-ζω₀t) · (A·cos(ω_d t) + B·sin(ω_d t))
```

So `evaluate(t)` is O(1) for *any* `t`, from *any* thread, in *any* order. That
converts all three problems into non-problems:

| Consequence | Calcium |
|---|---|
| Animation clock | Owned by the compositor, sampled at predicted vsync. A UI thread stall does not stall animation. |
| Interruption | Additive impulse superposition. Velocity is preserved by construction, not by bookkeeping. |
| Sampling | Evaluated at the *presentation* timestamp of the frame being built. |

This is the whole reason Calcium exists as a separate project rather than as a
patch to an existing toolkit. You cannot retrofit it: it dictates the threading
model, the transaction model, and the shape of every animatable property in the
public API.

---

## 2. Goals

### 2.1 Portability
One codebase, five first-class targets: **Windows, macOS, Linux, iOS, Android**.
No target is a port; no target has a reduced feature set. A `ca::view` written
for a desktop workstation runs on a phone with correct touch semantics, safe
areas, IME, and screen-reader support.

### 2.2 Feel
120 Hz-capable, GPU-composited, with the physical response characteristics of
iOS: critically damped springs by default, momentum handoff from gesture to
animation, asymptotic rubber banding at boundaries, and coupled kinematics
between properties. This is the differentiator. Everything else in this document
is in service of it.

### 2.3 Developer experience across a very wide range
The same framework must serve a 200-line phone app and a 2-million-line
professional desktop application (a DAW, a CAD tool, an NLE). These have opposite
needs — the former wants declarative brevity, the latter wants imperative control
over invalidation, memory, and draw order. Calcium serves both by making the
declarative layer a *thin reconciler over the imperative core*, so descending a
level is always available and never leaves the framework.

### 2.4 Vertical integration
Every third-party library is a *second-party implementation of a Calcium
interface*. Skia implements `ca::graphics::RenderingBackend`. SDL3 implements
`ca::platform::PlatformBackend`. HarfBuzz implements `ca::text::ShapingBackend`.
The public API never names, exposes, or leaks any of them. When an in-house
replacement lands, no user code changes.

### 2.5 Language reach
A stable, versioned, opaque-handle C API (`calcium.h`) that is a first-class
citizen rather than a generated afterthought — sufficient to build Python, Rust,
Go, and C# bindings that feel idiomatic in those languages.

### 2.6 No escape hatches
An escape hatch is an admission that the framework's abstraction is incomplete.
Calcium instead offers **progressive complexity disclosure**: five public,
documented, stable levels of abstraction. Any task that puts pixels on a screen
is done *inside* Calcium, just further down.

---

## 3. Non-goals

Stating these keeps the architecture honest.

- **Not a game engine.** No ECS, no physics bodies, no asset pipeline, no audio.
  Twell simulates *UI* kinematics, not rigid-body dynamics.
- **Not a web renderer.** No HTML, CSS, or DOM compatibility. The layout model
  is SwiftUI's negotiation, not flow layout.
- **Not native-widget-wrapping.** Calcium renders its own controls. Platform
  controls are integrated only where the OS mandates it (IME candidate windows,
  system text-selection menus, video surfaces).
- **Not retained-mode-only or immediate-mode-only.** Both, deliberately layered.
- **Not a scripting host.** Bindings call in; Calcium does not embed a VM.

---

## 4. Honest constraints

A specification that only lists ambitions is marketing. These are the places
where the stated goals meet physics, and what Calcium actually promises.

### 4.1 "120 fps without a single dropped frame" is not achievable as an absolute

No userspace framework on a general-purpose preemptive OS can guarantee zero
dropped frames. Shader compilation stalls, page faults, thermal throttling,
compositor handoff, and the OS scheduler are all outside Calcium's control.

**What Calcium promises instead, which is stronger in practice:**

1. **A frame budget that is met by construction.** At 120 Hz the budget is
   8.33 ms. Calcium targets UI thread ≤ 4 ms, compositor ≤ 2 ms, GPU ≤ 6 ms
   (pipelined, so these overlap).
2. **Zero heap allocation in the steady-state frame path.** Arena allocators,
   per-frame scratch arenas reset in O(1), pre-sized pools.
3. **Zero shader compilation after startup.** All pipeline permutations are
   enumerated and warmed during initialization; a pipeline cache miss at frame
   time is a hard error in debug builds, not a stall.
4. **Bounded, non-visual degradation.** Because animation is analytical and lives
   on the compositor, a UI thread overrun drops *interaction latency*, not
   *animation smoothness*. The frame still ships, with animation evaluated at
   the correct timestamp. This is the single most important quality property in
   the entire framework.
5. **Measurable, not asserted.** `calcium-tracer` records per-stage timings for
   every frame with a hard CI gate on regression. The claim is a test, not a
   README line.

### 4.2 SDL3 is not sufficient for first-class mobile

SDL3 is excellent for windowing, input, and desktop bring-up. It does not
adequately cover iOS/Android application lifecycle, IME composition,
accessibility bridges, safe-area insets, dynamic type, or dark-mode
notifications. Calcium therefore ships **native platform backends** (`AppKit`,
`UIKit`, `Android`, `Win32`) alongside the SDL3 one. SDL3 is the portable
bring-up path and the Linux path, not the mobile path.

### 4.3 De-vendoring has a difficulty gradient

Replacing dependencies is realistic in this order, and not otherwise:

| Dependency | Difficulty | Realistic horizon |
|---|---|---|
| SDL3 | Moderate — platform code is broad but shallow | Achievable early; native backends already do this |
| Skia | Hard — path rasterization, blending, GPU backends, color management | Multi-year |
| HarfBuzz + ICU | Hardest — shaping, bidi, line breaking, normalization, font formats | Last. Decades of accumulated correctness. |

The architecture must permit all three. Only the first should be scheduled.

### 4.4 Interface-shaped-by-Skia is a real risk

The failure mode of "abstract the renderer" is an interface that is a transcript
of Skia's API, which is not portable to a different implementation. Calcium
mitigates this concretely:

- The backend boundary is a **recorded display list IR**, not a call interface.
- The IR's semantics — fill rules, blend equations, gradient interpolation space,
  color pipeline, stroke geometry, dash phase — are **specified in writing** in
  `docs/spec/display-list.md`, independent of any implementation.
- A **golden-image conformance suite** validates any backend against that spec.
  A backend is correct when it passes, not when it links.

### 4.5 Accessibility cannot be retrofitted

A framework that renders its own controls owes the platform a semantic tree.
VoiceOver, Narrator/UIA, TalkBack, and AT-SPI all require a stable, queryable,
node-identified hierarchy with roles, values, and actions. This constrains the
core architecture — it is a primary reason the core is a *retained* tree with
stable identity. `ca::accessibility` is present in the v0.1 architecture even
though platform bridges land later.

---

## 5. Reading order

| Document | Contents |
|---|---|
| `01-principles.md` | The 14 architectural principles, with their consequences |
| `02-architecture.md` | The five-level stack, threading model, frame pipeline |
| `03-project-structure.md` | Modules, directory layout, dependency enforcement |
| `04-public-api.md` | API design at every level, with code |
| `05-animation-and-twell.md` | Twell integration, the compositor animation clock |
| `06-roadmap.md` | Build sequencing, milestones, what to write first |
