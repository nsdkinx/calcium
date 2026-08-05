# Calcium — Session Checkpoint (2026-08-06, M2 shipped)

Session goal: continue Calcium per docs/06-roadmap.md — M2, the vertical
slice. Everything below reflects the real, verified state of the repo.

---

## 1. What shipped this session (commits, pushed to origin/main)

### The M2 vertical slice — ALL FOUR EXIT CRITERIA MEASURED GREEN

**`ca::graphics` (Level 2, new module):**
- `Paint` — solid colors (`solid_color`, `with_alpha_multiplied_by`); the
  gradient/shader surface lands with the color pipeline.
- `DisplayList` — the sealed IR: tagged byte records (u8 cmd, u8 paint index,
  u16 payload size, payload) + interned paint table. M2 record set:
  save/restore/concat_transform(AffineTransform)/clip_rect/fill_rect/
  fill_rounded_rectangle. The encoding IS the contract (backends parse it);
  accessors for tests + raw sequential walk for the rasterizer. Deep
  equality, shared-impl copies.
- `DisplayListRecorder : DrawingContext` — the Level-3 → Level-2 doorway
  (P7). `set_global_alpha` folds into paints at record time, so the
  rasterizer's state is exactly CTM + clip.
- `src/graphics/rasterizer.{hpp,cpp}` (internal) — the IR walker: CTM stack,
  device-space clip stack (in lockstep with the pass's), culling (alpha 0 /
  off-clip). Rounded-rect tessellation with BOTH corner curves: circular
  (quarter-arc fans) and continuous (the G2 two-cubic squircle spec, sampled
  from the fitted m/a/b constants). No coverage AA in M2 (documented).

**`ca::gpu` (Level 1) — the draw pass:**
- `DrawPass` (draw_pass.hpp): clear / push_clip / pop_clip / fill_rect /
  fill_polygon — device space, straight-alpha sRGB. Deliberately primitive:
  all geometry logic lives in the rasterizer, so D3D12/ca::raster backends
  reimplement a handful of virtuals, not corner math.
- `gpu_sdl3`: Sdl3DrawPass — SDL_RenderClear/FillRect/GeometryRaw, clip stack
  in the pass (SDL has one render clip), blend mode BLEND set once per
  renderer (straight-alpha inputs, premultiplied blend formula), reserved
  vertex scratch (P8: no steady-state allocation).

**`ca::layer` (Level 3, new module):**
- `Layer` facade (handle + tree pointer; anchor = top-left in M2) over
  `LayerTree`: pooled storage via the existing `core::HandlePool`
  (generation-checked), SoA pools for bounds/radius/color/transform/property
  indices/DLs/parents, per-layer property objects in a side `std::deque` so
  `position()`/`opacity()` references survive growth. `commit()` publishes an
  immutable `FramePacket` (true SoA — the compositor's resolve loop walks
  parallel arrays, the point of docs/02 §4.1) via atomic shared_ptr;
  animating position/opacity never needs a commit (docs/02 §2.2).

**Compositor (M2 loop):** tick_and_publish(t_present) → newest packet →
resolve presentation values from the coordinator snapshot → layer background
fill + recorded DL through the rasterizer → present. **Idle power**: when
`has_active_animations()` is false and no work is pending, the compositor
blocks on a wake condition (no vsync pacing at all) — wake sources: stop,
repaint request (the setNeedsDisplay contract), resize. `request_repaint()`,
`frame_count()` added. M1 clear-only path retained (02-clear-color untouched).

**Exit demo `examples/03-spring-rectangle`:** background layer (attribute
path) + card layer (attribute fill + recorded DL through the recorder). Tap
to spring (retarget mid-flight = velocity preserved), 's' = interactive stall
test, `--verify` runs the automated gate. Measured on this machine:
retarget moved 82.6 px of 277.6 in 100 ms (no snap) / **12 frames shipped
through the 200 ms UI stall, 18.6 px advanced** / **0 frames in the 1 s after
rest**. `frames: 72, p50 0.17 ms / p99 0.37 ms, zero overruns`.

**Tests: 16/16 passing** (new: test_display_list, test_rasterizer with a
recording fake pass, test_layer with a real compositor thread ticking the
coordinator). Hygiene gate green (38 public headers).

## 2. Bug found and fixed this session (pre-existing, in the M2-prep module)

**Twell slot vs. property index in the coordinator's rest mapping.** Twell
allocates a 2D property across TWO slots (x + y) and 3D across three, so slot
indices ≠ coordinator property indices — but the resting queue reports slots,
and the coordinator mapped them straight onto `at_rest[]`. With 1D-only
properties (all of the original test_animation) the mapping is identity and
the bug hides; the layer tree registers 2D positions and it broke: the card's
spring converged to the target but never reported rest (the rest event landed
on a neighbor property's flag). Fix: `slot_to_property` + `slot_at_rest` in
the coordinator Impl, sized by `max_animated_properties`; a property reports
rest only when ALL its component slots rest (`mark_active`/`mark_at_rest`
helpers). This is exactly the class of bug the M2 vertical slice exists to
flush out.

## 3. Design decisions worth remembering (M2)

- **The DrawPass stays primitive** (rects, convex polygons, clip, clear) —
  tessellation and state live in `src/graphics/rasterizer.cpp` so every
  backend reuses the geometry logic verbatim.
- **The packet is the re-composition unit** (docs/02 §2.2): position/opacity
  resolve per frame from the coordinator snapshot; everything else requires
  commit() + request_repaint().
- **The Application does NOT own the coordinator**: Application is Level 1
  and never names Level-3 types (the level DAG is enforced by CI). The
  bootstrap (demo/C-ABI at M7) creates it. The two
  `max_animated_properties`-style config fields moved out of
  Application::Configuration.
- **M2 runs the model on the platform thread** (main thread plays the UI
  role); the dedicated UI thread + event queue lands later.
- **`Rect{origin, size}`** — aggregate init is NOT from_edges; the rasterizer
  tests tripped on this twice.

## 4. Next steps (in order)

1. M2 remainder (docs/06): `docs/spec/display-list.md` + the golden-image
   conformance suite; coverage AA; the tracer-backed frame-budget CI gate.
2. M3 — text (docs/06 M3): `ca::text` with fractional advances from the
   first commit, HarfBuzz/ICU backends (the SDL3-only policy gates nothing
   here — shapers were never SDL).
3. The event-queue milestone: platform thread → UI thread lock-free queue,
   which lands the dedicated UI thread (docs/02 §2) and un-merges the roles
   the demo currently merges.

## 5. Environment / build notes (this machine)

- Toolchain: VS 18 BuildTools (MSVC 19.51), Windows SDK 10.0.26100,
  Ninja 1.13.2, CMake bundled with BuildTools.
- **`cmake`/`cl` are NOT on PATH in a fresh shell.** Bootstrap first:
  ```powershell
  . C:\Users\YutaRedux\AppData\Local\Temp\calcium_env.ps1
  cmake --preset windows-msvc-debug   # SDL3 options are cached ON
  cmake --build build\windows-msvc-debug
  ctest --test-dir build\windows-msvc-debug
  ```
- Run the M2 demo from `build\windows-msvc-debug`:
  `.\bin\example_spring_rectangle.exe --verify` (exit gate) or interactive
  (tap / 's' / ESC). M1: `.\bin\example_clear_color.exe --seconds 3`.
- Hygiene gate runs on every build (38 public headers) and enforces the
  level DAG.

## 6. Gotchas learned (cumulative)

- **Twell slot allocation**: 2D/3D properties span 2/3 slots; the resting
  queue reports slots, and `twell_property_id` is the FIRST slot. Anything
  mapping Twell ids ↔ Calcium property indices must go through
  `slot_to_property`.
- **`Rect` aggregates as {origin, size}**, not edges — use `Rect::from_edges`.
- **MSVC**: `__declspec(dllexport)` must come FIRST in a declarator;
  `std::cos/sin/sqrt` not constexpr in MSVC's STL; no C compound literals in
  C++ mode (`(float[4]){...}` is rejected — use named arrays/std::array).
- **Static libs only pull referenced objects** — backend registration must be
  called (backend_registration.hpp).
- **SDL3 3.4 renderer**: no flags param on `SDL_CreateRenderer` (the driver
  hint is `SDL_HINT_RENDER_DRIVER`), vsync via `SDL_SetRenderVSync`,
  `SDL_ResetHint` not UnsetHint, `SDL_RenderGeometryRaw` takes `SDL_FColor`
  and a float xy array with a common stride; `SDL_SetRenderClipRect` is the
  single render clip (a stack must live in the pass). No rounded-rect
  primitive — tessellate. `SDL_BLENDMODE_BLEND` is premultiplied-blend with
  straight-alpha inputs.
- **SDL renderers are single-threaded** — create and use on one thread (the
  compositor's).
- **Python heredoc patching** via `bash -c` mangles `\n` escape sequences in
  C++ string literals (Windows CRLF). Use the Edit tool for such edits.
- **Presentation value vs model value**: the snapshot is only as fresh as the
  last tick — a test reading presentation immediately after enqueueing must
  tick first.

## 7. Repo hygiene

- `.gitignore` covers `calcium-trace.csv`, `out.txt`, `err.txt` (demo
  artifacts). All M0/M1/M2-prep work plus this session's M2 slice is
  committed; the D3D12 backend lives outside the repo at
  `D:\calcium-d3d12-archive\` and in git history (`f6eb604`, `d1f9af6`,
  `f1c3642`).
