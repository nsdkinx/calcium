# Calcium — Build Sequence

The architecture is the easy part. Sequencing determines whether it ships.

The ordering principle: **build the thin vertical slice that proves the
differentiator first.** Not the foundation layer by layer — a spring-animated
rectangle on a GPU-composited window, end to end, as early as possible. That
single demo validates the threading model, the transaction model, the Twell
integration, and the frame pipeline simultaneously. Everything after it is
widening a proven spine.

---

## M0 — Skeleton (weeks 1–2)

- CMake project, five-platform toolchain files, `CMakePresets.json`
- `ca::core`: arena allocator, typed generation-checked handles, `Result`,
  `InternedString`, `SmallVector`, thread-affinity assertions, monotonic clock
- `ca::geometry`: point, size, rect, insets, `AffineTransform`, `Transform3D`
  with decompose/recompose, quaternion
- **The header hygiene CI gate** (`tools/ci/check_header_hygiene.py`)
- Test harness, golden-image comparison infrastructure

Writing the hygiene gate before writing any backend is deliberate. Retrofitting
P5 after three months of Skia types leaking into headers is a rewrite.

**Exit:** `Transform3D` round-trips decompose→recompose within 1e-6; hygiene gate
green in CI.

---

## M1 — Window and clear color (weeks 3–5)

> **Backend policy — SDL3 only, through the MVP.** SDL3 is the *only* backend —
> platform *and* GPU — until the MVP works end to end, the public API is
> stable, and Calcium's core is stabilized. The `ca::gpu` interface is
> implemented by the SDL3 renderer (`gpu_sdl3`, accelerated with a software
> fallback); the D3D12 / Vulkan / Metal GPU backends are NOT started before
> that point. They slot in behind the same interface when they return.

- `ca::platform`: `Application`, `Window`, `Display`, event types with full
  device provenance
- SDL3 backend for Windows/macOS/Linux
- `ca::gpu`: `GraphicsDevice`, swapchain, render pass; the SDL3 renderer
  sufficient to clear (the only GPU backend until the MVP — see the policy
  above)
- Compositor thread, vsync loop, **`predicted_presentation_time()` on all three
  platforms**
- `calcium-tracer` skeleton recording per-stage timings from day one

**Exit:** a window on three platforms clearing to an animated color at the
display's native refresh rate, with per-frame timings recorded. Presentation-time
prediction verified against measured scanout.

Getting prediction right here — before anything depends on it — is the difference
between smooth and almost-smooth later. It is also nearly impossible to debug once
a full tree sits on top of it.

---

## M2 — The vertical slice (weeks 6–10) ★

**The milestone that de-risks the entire project.**

> **M2 status (2026-08-06):** shipped. The vertical slice runs on SDL3 only —
> the display-list IR, recorder, and rasterizer emit into the `gpu::DrawPass`
> (`src/graphics/rasterizer.cpp`), and the layer tree's SoA `FramePacket` is
> what the compositor re-composites. Skia remains deferred behind the same
> rasterizer seam by the SDL3-only backend policy (M1). The exit demo
> `examples/03-spring-rectangle --verify` measures all four criteria green on
> Windows; the remaining M2 items are the conformance suite, coverage AA, and
> the tracer-backed frame-budget CI gate.

- `ca::animation`: `AnimationCoordinator` wrapping Twell, `AnimatableProperty`,
  `Transaction`, `Motion`, `MotionScheme`, intent queue, triple-buffered
  presentation snapshot
- `ca::layer`: minimal SoA layer tree, transform/opacity/position/background
- `ca::graphics`: display list IR + recorder, `Paint`, `Color`, solid fills and
  rounded rects
- Skia rasterizer backend consuming the IR
- Compositor: tick Twell → resolve transforms → batch → submit

**Exit — all four must hold:**
1. A rounded rect springs to a new position on tap, on all three desktop platforms.
2. Tapping mid-flight retargets with **visibly preserved velocity** (no snap, no
   velocity reset).
3. **Artificially stalling the UI thread for 200 ms does not interrupt the
   animation.** The compositor keeps shipping frames with advancing physics.
4. Idle CPU is 0% once the spring reaches rest.

Item 3 is the proof that the central architectural bet works. If it fails, the
threading model needs rework — and it is far cheaper to discover that at week 10
than at week 60.

---

## M3 — Text (weeks 11–16)

- `ca::text`: `Font`, `FontDescriptor`, `FontManager`, `GlyphRun`, `Paragraph`,
  `ParagraphBuilder`
- HarfBuzz shaping backend, ICU bidi + line breaking
- Font providers: CoreText, DirectWrite, FontConfig
- Glyph atlas with worker-thread rasterization
- **Fractional advances, stem darkening, gamma-correct blending, optical sizing
  from the first commit** (P10 — retrofitting these is a rewrite)
- `docs/spec/text-metrics.md`

**Exit:** side-by-side screenshot comparison against native rendering on each
platform, at 12 pt and 34 pt, light and dark. Bidi and complex-script (Arabic,
Devanagari, Thai) correctness cases pass.

---

## M4 — Views, layout, input (weeks 17–24)

- `ca::view`: `View`, `ViewController`, responder chain, focus manager
- `ca::layout`: three-phase negotiation, stacks, grid, alignment guides,
  layout priority, memoization
- Gesture recognizers: tap, pan, pinch, long-press, plus the arbiter
- `GestureKinetics` momentum handoff wired to Twell
- `ca::accessibility`: semantic tree and node model (bridges deferred, model not)
- `calcium-inspector`: live layer tree and layout debugger

**Exit:** the bottom-sheet example from `04-public-api.md` §2.4 runs, with rubber
banding, flick-velocity destination selection, and a coupled scaling backdrop.

This is the second showcase milestone, and the one that proves the gesture half of
the physics story.

---

## M5 — Widgets (weeks 25–36)

- `Button`, `Label`, `TextField`, `Slider`, `ToggleSwitch`, `Checkbox`,
  `SegmentedControl`, `ProgressIndicator`
- `ScrollView` with Twell decay + rubber banding
- `ListView` with recycling, stable identity, animated diffs
- `Popover`, `Menu`, `ContextMenu`, `Sheet`
- `Theme` including `MotionScheme`; platform-default themes
- IME composition via `TextInputClient`
- Text selection, cursor affinity, grapheme navigation

**Exit:** a text editor example with selection, IME, undo, and 120 fps scrolling
through 100,000 lines.

---

## M6 — Mobile (weeks 37–46)

- UIKit platform backend: lifecycle, safe areas, dynamic type, dark mode
- Android platform backend: JNI, `Choreographer`, lifecycle, IME, `SurfaceView`
- Touch semantics: slop, delayed-touch-began, scroll-vs-tap disambiguation
- Accessibility bridges: VoiceOver, TalkBack, UIA, AT-SPI
- Reduced-motion and high-contrast observation

**Exit:** the bottom-sheet and editor examples run on physical iOS and Android
devices at 120 Hz (ProMotion / high-refresh Android), with screen readers
functional.

Deliberately after widgets: mobile platform work is broad and shallow, and doing
it against a complete widget set surfaces the real integration gaps (IME,
accessibility, safe areas) rather than hypothetical ones.

---

## M7 — C API and bindings (weeks 47–54)

- `calcium.h`: full hand-designed surface, generation-checked handles
- `docs/spec/c-abi.md`: versioning policy, handle rules, error contract
- `calcium-bindgen`
- Python (first, most demanding of ergonomics), then Rust, C#, Go
- ABI stability test suite

**Exit:** the counter example and the bottom sheet in all four languages, with
`calcium.h` frozen for 0.x.

---

## M8 — Declarative layer (weeks 55–64)

- `ca::compose`: `Element`, `Component`, `State`, `Binding`, `Environment`
- Reconciler with keyed diffing
- Modifier chain
- Transitions, matched-geometry (shared element) transitions
- `LazyVerticalStack` with virtualization

**Constraint, enforced by CI:** `calcium_compose` links against `calcium_widget`
and `calcium_view` **public headers only**. If the reconciler needs a hook that
does not exist publicly, the fix is to make it public (P4).

**Exit:** counter and bottom sheet re-expressed declaratively in under a third of
the Level-3 line count, with identical measured frame timings.

---

## M9 — Professional desktop (weeks 65–78)

- Dockable panels, split views, tabs, tear-off windows
- Multi-window, multi-display with per-display scale factors
- Virtualized canvas: 50,000+ node scenes with spatial indexing
- Custom render pass integration (Level 1) proven in a real example
- Color management: Display P3, HDR, per-window color space
- High-precision input: stylus pressure and tilt, precise trackpad scroll
- Performance CI gates on the frame budget

**Exit:** `examples/09-professional-app` — a DAW-shaped application with a
timeline, docked panels, a 50,000-node canvas, and a custom GPU waveform pass,
holding the frame budget on all five platforms.

---

## M10 — De-vendoring (year 2+)

In dependency-difficulty order (`00-overview.md` §4.3):

1. **Native platform backends replace SDL3** — already partly done for mobile in
   M6; extend to Win32/AppKit desktop. Achievable.
2. **`ca::raster` replaces Skia** — path rasterization, blending, GPU backends,
   color management. Multi-year. The golden-image conformance suite is what makes
   it verifiable rather than a leap of faith.
3. **`ca::shape` + `ca::unicode` replace HarfBuzz + ICU** — last, and honestly
   optional. These encode decades of accumulated correctness and the return on
   replacing them is low.

The architecture must permit all three from day one. Only the first should be
scheduled.

---

## Continuous, from M0

| Track | Gate |
|---|---|
| Header hygiene | Every commit. Non-negotiable. |
| Frame budget | Per-stage p50/p99 recorded per commit; regression fails CI |
| Golden images | Every backend against `docs/spec/display-list.md` |
| Allocation sentinel | Debug builds fail on any steady-state frame-path allocation |
| Thread affinity | Debug assertions on every public entry point |
| Fuzzing | Path parser, text shaper input, display-list deserializer |
| Memory | ASan, UBSan, TSan on all three desktop platforms |

---

## Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Compositor re-composition can't hold under real load | **Critical** | Prove at M2 exit criterion 3, before anything depends on it |
| Display-list IR ends up Skia-shaped | High | Written spec + conformance suite from M2; review the IR against a hypothetical second implementation |
| Text quality falls short of native | High | M3 side-by-side gate; fractional advances non-negotiable from first commit |
| Scope: five platforms × six subsystems | High | Vertical slices, not horizontal layers. Every milestone ships a runnable demo. |
| Twell's 8-impulse budget insufficient | Low | Measured in M2; Twell is in-house and the constant is trivially raised |
| C ABI ossifies too early | Medium | Freeze only at M7, after four levels are proven in real use |
| Accessibility retrofit | High | Semantic model in M4, bridges in M6 — model before bridges, never after |

---

## The first three things to write

1. **`tools/ci/check_header_hygiene.py`** — before any code it polices exists.
   Cheapest high-value infrastructure in the project.
2. **`include/calcium/geometry/transform_3d.hpp`** with decompose/recompose —
   correct transform interpolation is load-bearing for the entire animation story
   and is pure, testable math with no dependencies.
3. **`src/animation/animation_coordinator.cpp`** — the Twell wrapper, the intent
   queue, the triple-buffered snapshot. This is the architecture's thesis in
   executable form. Write it early, stress it hard.
