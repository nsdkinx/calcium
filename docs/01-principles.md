# Calcium — Architectural Principles

Fourteen principles. Each states a rule, the reason for it, and — most
importantly — the **consequence**: what it forces to be true elsewhere in the
system. A principle with no consequence is a slogan.

---

## P1. The presentation value is the only truth on screen

Every animatable quantity has two values: the **model value** (where it logically
is) and the **presentation value** (where it physically is this frame). Rendering
reads presentation values exclusively. Hit-testing, layout, and application logic
read model values.

**Why.** This is the single distinction that separates "animated" frameworks from
frameworks that *feel* alive. It is Core Animation's central insight and Twell
already implements it.

**Consequence.** Two coordinate resolutions exist at all times, and the framework
must never confuse them. Layout is computed against model geometry; if layout ran
against presentation geometry, an in-flight animation would re-trigger layout
every frame and the system would thrash. A public API that exposes only one of
the two is wrong.

**Corollary — hit testing.** Hit-testing has a deliberate choice per layer:
`HitTestPolicy::model` (default — the button is where it logically is, so a
mid-flight tap does what the user intended) or `HitTestPolicy::presentation`
(what's under the finger, for direct manipulation of moving objects). Getting
this wrong is the source of the "I tapped it and nothing happened" bug class.

---

## P2. The animation clock belongs to the compositor, not the application

The application never advances animation. It declares intent — "this property is
now springing to 400" — and the compositor samples the analytical solution at the
predicted presentation timestamp of the frame it is building.

**Why.** Decouples smoothness from application throughput. This is only possible
because Twell is analytical rather than integrated.

**Consequence.** This is the highest-leverage constraint in the document.

- Twell's context is owned by the compositor thread.
- The UI thread cannot call `twell_context_tick`. It cannot read presentation
  values synchronously either. It *commits intent*.
- Therefore all animation mutation must be **transactional** (P3).
- Therefore reading a presentation value from the UI thread returns a
  last-published snapshot, explicitly named as such in the API.
- A blocking UI thread produces stale *input response*, never stale *animation*.

---

## P3. Mutation is transactional and atomic

State changes are accumulated into a `ca::animation::Transaction` and committed
as one unit. A commit either applies entirely to a frame or not at all.

**Why.** Prevents tearing (half a layout with half an animation). Gives one
natural place to attach default animation parameters to a batch of changes —
Core Animation's implicit-animation model, made explicit.

**Consequence.** Every setter on a layer is a *deferred write into the current
transaction*, not an immediate store. An implicit transaction opens at the start
of each event-loop turn and commits at its end, so simple code needs no explicit
transaction. Nesting is allowed; only the outermost commit publishes.

---

## P4. Retained core, declarative shell

The core is a retained, mutable, stable-identity layer tree with explicit
invalidation. The declarative API is a reconciler that diffs a description into
mutations on that tree.

**Why.** Retained trees give incremental invalidation, stable accessibility
identity, and predictable memory — required by heavy desktop applications.
Declarative shells give brevity — required by everything else. Pure-declarative
frameworks that rebuild descriptions every frame cannot serve a 50,000-node CAD
viewport.

**Consequence.** The declarative layer must be *strictly additive*: it may only
call public Level-3 APIs. If the reconciler needs a private hook, that is a bug
in Level 3's API, and the fix is to make the hook public — not to give the
reconciler special access. This is what keeps "descend one level" always viable.

---

## P5. Every dependency is a backend behind a Calcium-owned interface

No third-party type appears in any public header. Not in a signature, not in a
member, not in a `#include`, not behind an `#ifdef`.

**Why.** Vertical integration, ABI stability, and the ability to de-vendor
without an API break.

**Consequence.** Public headers may not include `SkCanvas.h`, `SDL.h`, or
`hb.h` — enforced mechanically in CI, not by review. Every backend boundary is a
pure-virtual interface in `ca::*::backend`, and each concrete backend is a
separate static library that the public headers never see. `ca::graphics::Color`
is a Calcium type that *converts* to `SkColor4f` inside the Skia backend's
translation unit.

---

## P6. The renderer boundary is a data format, not a call interface

The backend consumes a recorded, immutable, serializable **display list IR**. It
does not receive a stream of virtual calls mirroring drawing operations.

**Why.** A virtual-call interface shaped around one library's API cannot be
implemented by a differently shaped library. An IR with written semantics can.

**Consequence.** This is what makes P5 real rather than aspirational.

- The IR is specified in prose: winding rules, blend equations, gradient
  interpolation color space, stroke join geometry, dash phase, clip
  antialiasing behavior, text baseline rounding policy.
- A golden-image conformance suite defines correctness.
- The IR is serializable, which buys three things nearly for free: record/replay
  debugging, deterministic golden-image tests, and out-of-process rendering.

---

## P7. Progressive complexity disclosure replaces escape hatches

Five public levels. Each is stable, documented, and complete. Descending gives
more control and more responsibility; it never leaves the framework.

| Level | Name | You get | You give up |
|---|---|---|---|
| 5 | Declarative | Terse composition, automatic state binding | Fine control over invalidation |
| 4 | Widgets | Ready controls, platform behavior, a11y | Custom visual structure |
| 3 | Layers + views | Retained tree, explicit animation, layout | Automatic reconciliation |
| 2 | Drawing | Direct display-list recording, paths, paints, text runs | Layer composition, caching |
| 1 | GPU | Custom render passes, shaders, textures, framebuffers | Portability guarantees, backend abstraction |

**Consequence.** Every level must be *reachable from the level above without
leaving the object graph*. A Level-5 declarative node must be able to hand out its
Level-3 layer; a Level-3 layer must accept a Level-2 draw callback; a Level-2
recorder must accept a Level-1 custom pass. Any gap in that chain forces an
escape hatch, which the framework has forbidden.

---

## P8. Zero heap allocation in the steady-state frame path

After warm-up, a frame that changes nothing structural performs zero
`malloc`/`free`.

**Why.** Allocator behavior is the most common source of frame-time outliers, and
it is nondeterministic across platforms.

**Consequence.** Arena allocators for per-frame scratch (reset by pointer bump),
free-list pools for layers and nodes, small-vector inline storage for children and
draw commands, interned strings for identifiers and font names, and a debug-mode
allocation sentinel that hard-fails on any frame-path allocation. Twell's
bring-your-own-arena model is exactly the right shape and sets the house style.

---

## P9. Feel is specified, not improvised

Motion is defined by a small set of **named, semantic** spring configurations in
a design-system-level table. Application code names an intent; it does not supply
magic numbers.

**Why.** Consistency across an application is what reads as "polish". Scattered
literal stiffness values guarantee inconsistency.

**Consequence.** `ca::animation::MotionScheme` is a first-class, themeable,
platform-adaptive object. `ca::animation::Motion::standard()` resolves through it.
Raw `SpringConfiguration` remains available at Level 3 for the cases that need it,
but the ergonomic path is semantic.

---

## P10. Text is a first-class subsystem, not a drawing call

Shaping, bidi, line breaking, font fallback, and hit-testing are one owned
subsystem with a caching architecture designed alongside the renderer.

**Why.** Text is the hardest correctness problem in any GUI framework and the
most expensive per-frame cost in text-heavy applications.

**Consequence.** Apple-quality typography — fractional advances (no integer
rounding of glyph positions), optical kerning from `GPOS`, stem darkening in the
rasterizer, gamma-correct blending in linear space, and hinting policy that
prefers fidelity over grid-fitting — has to be a design input at the start.
Retrofitting fractional advances into a pipeline that assumed integer glyph
positions is a rewrite.

---

## P11. Layout is a three-phase negotiation with no global constraint solver

1. Parent proposes a size to child.
2. Child returns its desired size.
3. Parent positions the child.

**Why.** It is O(n) per pass, cache-friendly, easy to reason about, requires no
solver, and produces the behavior developers already understand from SwiftUI.

**Consequence.** Layout must be *pure*: given the same proposal and the same
model state, a node returns the same size, with no side effects. Purity is what
permits caching, parallel measurement of independent subtrees, and speculative
layout. The framework must not offer a "layout that also mutates state" affordance,
because it destroys all three.

---

## P12. Identity is explicit and stable

Every layer, view, and declarative node has a stable identity independent of its
position in its parent's child list.

**Why.** Required by animation (matching a node across frames to continue an
animation), by reconciliation (diffing without destroying state), and by
accessibility (a11y node identity must survive re-layout).

**Consequence.** Reordering a list must not reset in-flight animations or
accessibility focus. This forces explicit keys in the declarative API for
dynamic collections — a small ergonomic cost that is not optional.

---

## P13. The C API is the ABI

`calcium.h` is a hand-designed, opaque-handle, versioned C interface — designed,
not generated. The C++ API is a header-only ergonomic layer over the same
primitives where practical.

**Why.** A single stable ABI serves every language binding *and* solves C++'s ABI
fragility across compilers and standard-library versions.

**Consequence.** Every capability must be expressible without templates,
exceptions, or RTTI across the boundary. Errors are return codes with a
thread-local detail string. Callbacks carry a `void* user_data`. Handles are
opaque `uint64_t`-backed structs with generation counters, so a stale handle is
*detected*, not dereferenced.

---

## P14. Accessibility and input are architectural, not additive

The semantic tree, focus management, keyboard navigation, IME composition, and
pointer/touch/pen/scroll routing are core concerns present from v0.1.

**Why.** Every one of them requires participation from the layer tree, the widget
layer, and the platform layer simultaneously. None can be bolted on.

**Consequence.** `ca::view::View` carries semantic properties from the first
commit, even before any platform a11y bridge exists. Input events carry full
device provenance — pointer type, pressure, tilt, precise vs. coarse scroll,
modifier state, timestamp — from the start, because a hover-capable stylus and a
coarse finger require different hit-test slop and different affordances, and
retrofitting provenance means revisiting every event handler ever written.
