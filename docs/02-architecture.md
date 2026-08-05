# Calcium — Architecture

---

## 1. The stack

```
┌───────────────────────────────────────────────────────────────────────┐
│  LEVEL 5   ca::compose      Declarative reconciler                    │
│            Body/State/Binding. Diffs descriptions into Level 3.       │
├───────────────────────────────────────────────────────────────────────┤
│  LEVEL 4   ca::widget       Controls                                  │
│            Button, TextField, ScrollView, List, Slider, Menu…         │
│            Platform behavior + semantics. Built only on Level 3.      │
├───────────────────────────────────────────────────────────────────────┤
│  LEVEL 3   ca::view         Retained tree, layout, gestures, a11y     │
│            ca::layout       Three-phase negotiation                   │
│            ca::animation    Twell-backed properties, transactions     │
│            ca::layer        Composited layer tree                     │
├───────────────────────────────────────────────────────────────────────┤
│  LEVEL 2   ca::graphics     Display-list recording                    │
│            ca::text         Shaping, line breaking, glyph runs        │
│            ca::geometry     Points, rects, transforms, paths          │
├───────────────────────────────────────────────────────────────────────┤
│  LEVEL 1   ca::gpu          Devices, passes, pipelines, textures      │
│            ca::platform     Windows, displays, input, lifecycle       │
├───────────────────────────────────────────────────────────────────────┤
│  LEVEL 0   ca::core         Arenas, handles, IDs, strings, logging    │
│            (not public API surface — foundation)                      │
└───────────────────────────────────────────────────────────────────────┘
                                   │
              ┌────────────────────┴────────────────────┐
              │        BACKENDS (never public)          │
              │  skia · sdl3 · appkit · uikit · android │
              │  win32 · harfbuzz · icu · d3d12 · metal │
              │  vulkan · gl                            │
              └─────────────────────────────────────────┘
```

**The dependency rule is absolute: a level may depend only on levels below it.**
`ca::widget` may not include a `ca::compose` header. `ca::view` may not include a
`ca::widget` header. This is enforced by a CI script that parses every `#include`
in the tree, not by convention.

---

## 2. Threading model

Three long-lived threads plus a pool. This is the part of the architecture that
directly delivers the smoothness claim, so it is worth reading closely.

```
┌─────────────────────────────────────────────────────────────────────┐
│ PLATFORM THREAD  (OS main thread — mandatory on macOS/iOS/Android)  │
│   OS event pump · window lifecycle · IME · a11y · display links     │
│   Normalizes events, posts to UI thread queue. Never blocks on it.   │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ lock-free event queue
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│ UI THREAD                                                            │
│   Event dispatch · gesture recognition · declarative reconcile ·     │
│   layout · display-list recording · commits transactions            │
│   Owns the MODEL tree. Application code runs here exclusively.       │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ committed FramePacket (triple-buffered)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│ COMPOSITOR THREAD                                                    │
│   OWNS THE TWELL CONTEXT.                                            │
│   Per vsync: predict presentation time → twell_context_tick(t) →     │
│   resolve presentation transforms → cull → batch → GPU submit        │
│   Can re-render the LAST packet with NEW animation values.           │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────────────┐
│ WORKER POOL  (N = cores − 2)                                         │
│   Glyph raster · image decode · path tessellation · SDF gen ·        │
│   shader warm-up · parallel subtree measurement                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.1 Why the compositor owning Twell is the whole trick

Consider the UI thread taking 40 ms on a 120 Hz display — a heavy CAD
re-layout, five frames' worth of budget.

**Conventional framework:** no new frame for 40 ms. Every animation freezes.
Five frames dropped, visibly.

**Calcium:** the compositor still wakes on every vsync. It has last frame's
`FramePacket`, which is structurally still valid — the same layers, the same
draw commands. It calls `twell_context_tick(ctx, predicted_presentation_time)`,
gets fresh presentation values, resolves new transforms, and submits. **Five
frames ship on time with correct, smoothly advancing animation.** What lags is
only the response to *new input*.

This is exactly how iOS stays smooth under load, and it is only possible because
Twell evaluates analytically at an arbitrary absolute time. A numerical
integrator cannot be sampled this way — it must be stepped in order, by one
owner, which couples it back to the application clock.

### 2.2 Packet re-composition validity

The compositor may re-composite a stale packet **only** when the change is
confined to compositor-resolvable properties: transform, opacity, corner radius,
shadow parameters, filter parameters, clip rect, and background color. Anything
that would change *recorded geometry* — text content, path shape, layout size,
child structure — requires a new packet from the UI thread.

Each layer therefore declares its animatable properties as either
**compositor-resolvable** or **record-affecting**. Animating a
compositor-resolvable property never invalidates a packet; animating a
record-affecting property marks the layer for re-record. This distinction is
public, documented, and visible in the profiler, because it is exactly the
knowledge a performance-sensitive developer needs.

### 2.3 Thread-safety contract

| Object | Rule |
|---|---|
| `ca::view::View`, `ca::layer::Layer` | UI thread only |
| `ca::animation::AnimatableProperty` | Written on UI thread via transaction; read on compositor |
| Twell context | Compositor thread only. UI thread never calls into it. |
| `ca::graphics::DisplayList` | Recorded on UI thread, immutable once sealed, read on compositor |
| `ca::gpu::Device` | Compositor thread; resource upload permitted from workers via transfer queue |
| `ca::text::FontManager` | Internally synchronized; safe from any thread |

Debug builds assert thread affinity on every public entry point. This catches the
entire class of "works on my machine, tears on a slower one" bugs at the moment
of the mistake.

---

## 3. Frame pipeline

Pipelined across three vsync intervals. At 120 Hz each stage has 8.33 ms and the
stages overlap, so total latency is ~2–3 frames while throughput is one frame
per vsync.

```
vsync N        vsync N+1        vsync N+2        vsync N+3
   │               │                │                │
   ├─ UI: build ───┤                │                │
   │  packet A     ├─ COMP: A ──────┤                │
   │               ├─ UI: build ────┤                │
   │               │  packet B      ├─ GPU: A ───────┤  → scanout A
   │               │                ├─ COMP: B ──────┤
   │               │                ├─ UI: packet C  ├─ GPU: B → scanout B
```

### Stage 1 — UI thread (budget 4 ms)

```
1. Drain platform events                          ~0.1 ms
2. Gesture recognition, hit test (model geometry)  ~0.1 ms
3. Dispatch to handlers; application code runs     varies
4. Reconcile declarative tree (Level 5 only)       ~0.5 ms
5. Layout: three-phase, dirty subtrees only        ~1.0 ms
6. Record display lists for dirty layers           ~1.5 ms
7. Open transaction, write animation intents
8. Commit → publish FramePacket
```

### Stage 2 — Compositor thread (budget 2 ms)

```
1. Acquire newest FramePacket (or reuse previous)
2. t_present = display.predicted_presentation_time()
3. twell_context_tick(ctx, t_present)              ~0.05 ms
4. Resolve presentation transforms (SIMD, flat SoA) ~0.2 ms
5. Cull against viewport and occluders             ~0.2 ms
6. Batch: sort by pipeline+texture, merge draws    ~0.5 ms
7. Encode GPU command buffer                       ~0.8 ms
8. Submit with presentation timestamp
```

### Stage 3 — GPU (budget 6 ms)

```
1. Upload dirty vertex/uniform/texture data
2. Render offscreen passes (filters, masks, cached layers)
3. Main pass: opaque front-to-back, then alpha back-to-front
4. Present
```

### 3.1 The critical detail: presentation time prediction

Twell must be ticked at the timestamp when the frame will actually be **scanned
out**, not when it is composited. Getting this wrong is the difference between
"smooth" and "almost smooth but subtly wrong", and it is invisible in a
screenshot.

```
t_present = t_last_vsync + (frames_in_flight + 1) × vsync_interval
```

Each platform provides the real thing where available:
`CVDisplayLink`/`CADisplayLink.targetTimestamp` (macOS/iOS),
`Choreographer.FrameCallback` frame deadline (Android),
`DXGI_FRAME_STATISTICS` (Windows),
`VK_GOOGLE_display_timing` or presentation-feedback (Linux/Wayland).

The `ca::platform::Display` interface requires `predicted_presentation_time()`.
A backend that cannot provide it must extrapolate from measured vsync cadence and
say so, so the degradation is visible rather than silent.

---

## 4. The layer tree

Two parallel trees, deliberately.

**`ca::view::View`** — the semantic tree. Layout participation, gesture handling,
accessibility, focus. One view owns one or more layers.

**`ca::layer::Layer`** — the composited tree. Geometry, transform, opacity, mask,
filters, recorded display list. This is what the compositor sees.

Separating them means a scroll view can be one view owning a hundred layers, and
a decorative gradient can be a layer with no view. It mirrors
`UIView`/`CALayer`, which has held up for fifteen years of production use.

### 4.1 Layer memory layout

Layers live in a pooled, index-addressed **structure of arrays**, not as
individually heap-allocated nodes:

```cpp
struct LayerStorage {
    std::vector<LayerIdentifier>   identity;
    std::vector<uint32_t>          parent_index;
    std::vector<geometry::Rect>    model_bounds;
    std::vector<TwellHandle>       transform_property;   // → Twell
    std::vector<TwellHandle>       opacity_property;     // → Twell
    std::vector<DisplayListRef>    recorded_content;
    std::vector<LayerFlags>        flags;
    // …
};
```

**Why.** Transform resolution is a tight loop over every layer, every frame. SoA
makes it SIMD-friendly and cache-dense: 100,000 layers' transforms resolve in
well under a millisecond. AoS with pointer-chasing does not, and that is the
difference between a professional application feeling responsive and not.

`Layer` in the public API is a lightweight handle — a `{index, generation}` pair
— not a pointer to storage. Handles are validated on every access in debug and
release, so use-after-free becomes a diagnosable error rather than a crash.

---

## 5. Layout: three-phase negotiation

Exactly SwiftUI's model.

```cpp
// Phase 1 — parent proposes. Any dimension may be nullopt = "unspecified".
struct SizeProposal {
    std::optional<float> width;
    std::optional<float> height;

    static SizeProposal unspecified();
    static SizeProposal zero();      // → minimum size
    static SizeProposal infinity();  // → maximum size
};

// Phase 2 — child answers. Never a range; one concrete size.
geometry::Size measure(const SizeProposal&) const;

// Phase 3 — parent places children in its own coordinate space.
void perform_layout(const geometry::Size& final_size);
```

Rules that make it tractable:

- `measure` is **pure and side-effect free** (P11). Enforced in debug by hashing
  node state before and after.
- Results are memoized on `(proposal, layout_generation)`; an unchanged proposal
  is free.
- Independent sibling subtrees above a cost threshold measure in parallel on the
  worker pool. Purity is what makes this safe.
- `zero()` and `infinity()` proposals give minimum and maximum intrinsic sizes
  without a separate intrinsic-sizing protocol.

---

## 6. Backend interfaces

Six boundaries. Every one is a pure-virtual interface in a `backend` namespace,
selected at runtime, and never named in a public header.

| Interface | Responsibility | Shipping impl | Future in-house |
|---|---|---|---|
| `graphics::backend::Rasterizer` | Display list → GPU commands | Skia | `ca::raster` |
| `platform::backend::Platform` | Windows, input, lifecycle | SDL3 + native | native only |
| `text::backend::Shaper` | Unicode → positioned glyphs | HarfBuzz | `ca::shape` |
| `text::backend::UnicodeServices` | Bidi, breaking, normalization | ICU | `ca::unicode` |
| `gpu::backend::GraphicsDevice` | Command submission | Metal/D3D12/Vulkan/GL | — (these *are* the platform) |
| `text::backend::FontProvider` | Font enumeration, fallback | CoreText/DirectWrite/FontConfig | — |

Note the honest asymmetry: `gpu` and `FontProvider` backends are *platform APIs*
and will never be replaced, which is correct. `Rasterizer`, `Shaper`, and
`UnicodeServices` wrap third-party libraries and are the genuine de-vendoring
targets, in that order of increasing difficulty.

### 6.1 The display list IR

The contract that makes backend replacement real (P6). An immutable, sealed,
serializable buffer of tagged commands plus a side table of paints, paths, and
glyph runs:

```
DisplayList
├── command buffer: tagged variable-length records
│     save_state / restore_state / concat_transform
│     clip_rect / clip_rounded_rect / clip_path
│     fill_path / stroke_path / fill_rect / fill_rounded_rect
│     draw_glyph_run / draw_image / draw_layer
│     begin_filter / end_filter / custom_pass
├── paint table   (interned; deduplicated)
├── path table    (interned; deduplicated)
└── glyph run table
```

Semantics are specified in `docs/spec/display-list.md`: non-zero vs. even-odd
winding, blend equations in premultiplied linear space, gradient interpolation
color space, stroke join and cap geometry, dash phase, clip antialiasing
composition, and glyph position rounding policy. **The golden-image conformance
suite is the executable form of that spec.**

---

## 7. Where Twell sits

Twell is `ca::animation`'s implementation and nothing above Level 3 knows its
name.

```
Application intent (UI thread)
        │
        ▼
ca::animation::Transaction          batches intents
        │  commit
        ▼
ca::animation::AnimationCoordinator  owns twell_context + arena
        │
        ▼
twell_property_animate_to_target()   analytical impulse pushed
        │
        ▼
COMPOSITOR: twell_context_tick(t_present)
        │
        ▼
twell_property_get_presentation_value()  → resolved transforms → GPU
```

Concretely:

- One Twell context per `Application`, arena sized via
  `twell_get_memory_requirement()` and grown by adding a second context when
  exhausted (contexts are cheap; the arena is not resizable in place).
- `AnimatableProperty<T>` wraps a `twell_property_id`. `T` maps to 1D/2D/3D
  Twell properties; a 4×4 `Transform` decomposes into translation (3D), scale
  (3D), rotation (quaternion, slerped), so it interpolates correctly instead of
  matrix-lerping into a shear.
- Twell's resting queue from `twell_context_tick` drives completion callbacks and
  lets the compositor **stop waking** when the tree is fully at rest — this is
  the idle-power story, and it comes free from a facility Twell already has.
- `twell_property_add_driver` is exposed as `ca::animation::PropertyLink`, which
  is how scroll-linked effects (parallax, collapsing headers, bottom sheets with
  scaling backdrops) are expressed without any per-frame application code.
- Twell's unit-aware rest thresholds map to Calcium's unit types, with the
  display scale factored in so a 3× display doesn't stop an animation a visible
  fraction of a point early.

Full detail in `05-animation-and-twell.md`.
