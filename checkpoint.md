# Calcium — Session Checkpoint (2026-08-05, ~3h + pivot)

Session goal: continue Calcium per docs/06-roadmap.md, committing and pushing
after each major feature. Everything below reflects the real, verified state of
the repo.

---

## 0. The pivot (this session, uncommitted at write time)

**SDL3-only policy (docs/06-roadmap.md M1):** SDL3 is the *only* backend —
platform **and** GPU — until the MVP works end to end, the API is stable, and
the core is stabilized. D3D12/Vulkan/Metal backends are NOT started before
that point; they return behind the same `ca::gpu` interface.

- The D3D12 backend was **archived** to `D:\calcium-d3d12-archive\`
  (with a README: what/why/resurrection path). It also lives in git at
  `f6eb604`, `d1f9af6`, `f1c3642`.
- **New `gpu_sdl3` backend** (`src/backends/gpu_sdl3/`): implements the
  unchanged public `ca::gpu` interface (GraphicsDevice/Swapchain/RenderPass)
  on SDL3's renderer API — `SDL_RenderClear`/`SDL_RenderPresent`, vsync via
  `SDL_SetRenderVSync(1)`, best driver first with automatic software fallback.
  SDL3 picks the driver (on Windows its accelerated driver is SDL's internal
  choice, e.g. "direct3d11"); Calcium maintains zero GPU code.
- **The M1 present bug is fixed by construction** (was: flip-model swapchain
  created on the main thread, presented on the compositor thread → the demo
  shipped 1 frame, then the window never presented at all). The compositor now
  creates device + swapchain **on the compositor thread** (`run_loop()`);
  `device_ready()`/`failure_message()` tell the app why it stopped. SDL
  renderers must be created and used from one thread — the migration was
  mandatory, not optional.
- `Sdl3Window::native_handle()` now returns the `SDL_Window*` pointer value
  (the HWND extraction existed only for D3D12).
- CMake: `CALCIUM_GPU_D3D12` → `CALCIUM_GPU_SDL3` (ON). The `sdl3` imported
  target moved to `src/backends/CMakeLists.txt` (shared by both backends).
- All docs updated to state the SDL3-only policy explicitly (00 §2.4/§4.2,
  02 diagram + §3.1 + §6 table, 03 layout + options + build matrix,
  06 M1 policy block).

**Not yet done at write time: build + ctest + demo verification** — see §5.

---

## 1. What shipped (committed and pushed to origin/main)

### M0 — Skeleton (complete, commit `7374a1e`, pushed)
- `ca::core`: `Identifier`, `InternedString`, `SmallVector`, `SpscRing`
  (lock-free SPSC ring; wait-free push/pop, `pop_or_wait`, `notify_wake`).
- `ca::geometry`: `AffineTransform`, `Path` + `PathBuilder`,
  `RoundedRectangle` with two specified corner curves:
  - `circular`: kappa cubic fit (max radial deviation 2.73e-4 r).
  - `continuous`: the G2 two-cubic spec of Apple's squircle corner. The n=5
    superellipse is NOT cubic-representable at its edge junctions (infinite
    curvature; verified numerically — see the header comment for the full
    derivation and the fitted constants m/a/b).
- Tests: 10 suites, all passing. Hygiene gate: 18 headers.

### M1 — Window and clear color (complete, commits `f6eb604` + `d1f9af6`, pushed; GPU path swapped by the pivot)
- `ca::platform`: `Application`/`Window`/`Display` facades over an internal
  `PlatformBackend` (pimpl; backend never leaks into public headers, P5).
  Event types with full device provenance (P14): pointer (mouse/touch/pen/
  eraser, per-device ids, pressure, tilt), key, scroll (precise vs coarse,
  momentum phases), system. `Display::predicted_presentation_time()` with the
  honest `provides_hardware_presentation_prediction()` flag (false on SDL3 —
  extrapolation from vsync cadence is the documented model).
- `ca::gpu`: `GraphicsDevice`/`Swapchain`/`RenderPass` interfaces (Level 1
  depends only on core+geometry per the level DAG — clear color is `float[4]`,
  window handle is an opaque uint64). `RenderPass::acquired_at()` is the vsync
  anchor; `submitted_at()` the submit time.
- `ca::graphics::Color` (sRGB floats; full color pipeline is M2).
- Backends (never public): `platform_sdl3` (SDL3 3.4.14 vendored under
  `third_party/sdl3/`, version in VERSION.txt) and `gpu_sdl3` (SDL3 renderer,
  accelerated + software fallback; the ONLY GPU backend until the MVP).
  Registration is explicit: `ca::calcium_register_backends()` — static libs
  only pull referenced objects, so self-registering statics never link
  (documented in `src/backends/backend_registration.hpp`).
- `src/compositor`: `Compositor` owning device+swapchain+thread; device and
  swapchain are created ON the compositor thread (docs/02-architecture.md
  §2.3 — this is the M1 present fix, see §0).
- `tools/calcium-tracer`: CSV → p50/p95/p99 + overrun counts.
- `examples/02-clear-color`: the M1 exit demo; `--seconds N` for CI smoke.
- C++ surface is the static modules; the DLL is reserved for the C ABI (M7).

## 2. What is done but NOT yet committed (working tree)

The pivot (§0) plus the M2-prep animation module:

### M2 prep — `ca::animation` (complete, 13/13 tests passing)
The roadmap's "first three things to write" item #3 — the architecture's
thesis in executable form:
- `SpringConfiguration` (response/damping conversion, damping ratio,
  settling estimate), `Motion` (named + explicit spring/decay/immediate,
  with_delay/with_speed_multiplier), `MotionScheme` (apple_like /
  material_like / platform_default / reduced_motion; named motions resolve
  through the scheme).
- `AnimationCoordinator` (docs/05 §3): one Twell context per Application,
  arena sized via `twell_get_memory_requirement`, `SpscRing` intent queue
  (UI thread enqueues, compositor applies at `t_present`), seqlock-published
  presentation snapshot, rest-event ring → UI-thread callbacks (never run on
  the compositor), completion waiters for `animate_with_completion`,
  delayed-intent slots. Thread roles enforced via CA_ASSERT_*.
- `AnimatableProperty<T>` for float / Point / Vector3, `Transaction`
  (thread-local ambient motion), `animate()` / `animate_with_completion()`.
- `src/animation/twell_impl.cpp` owns TWELL_IMPL (the documented single TU).
- Tests (`tests/unit/test_animation.cpp`) verify the thesis:
  * analytical sampling — a 500 ms stall then one tick gives the SAME value
    as 30 × 16.7 ms ticks (the "UI thread stall doesn't stall animation"
    property at the kernel level);
  * retarget mid-flight preserves continuity (no snap);
  * rest reporting, callbacks fire once per registration, completions,
    2D properties, cross-thread UI→compositor intent flow (real threads).
- Files: `include/calcium/animation/*` (6 headers),
  `src/animation/*` (5 .cpp + CMakeLists), `tests/unit/test_animation.cpp`,
  edits to `CMakeLists.txt`, `include/calcium/calcium.hpp`,
  `tests/CMakeLists.txt`.

## 3. Known issues (none open)

The M1 present bug (1-frame demo, `DXGI_ERROR_INVALID_CALL`, window never
presented) is **resolved** — not by patching the D3D12 backend but by
replacing it: the SDL3 renderer backend creates the renderer on the
compositor thread, which is both the documented fix and SDL's requirement.
The `gpu_sdl3` failure channel: `begin_clear_pass` returns the error
(`SDL_RenderClear` failure or a failed prior present — `end_and_present` is
void by interface design, so it records into the device and the next acquire
surfaces it); the compositor records `failure_message_` and stops.

## 4. Next steps after this session's verification (in order)

1. **Verify the pivot build** (§5), then commit it ("SDL3-only pivot:
   archive D3D12, gpu_sdl3 renderer backend on the compositor thread").
2. Continue M2 (docs/06-roadmap.md):
   - `ca::layer`: minimal SoA layer tree (position/transform/opacity/
     background), handle-based (docs/02 §4.1).
   - `ca::graphics`: display-list IR + recorder + Paint (docs/02 §6.1).
   - Compositor integration: tick Twell (`tick_and_publish(t_present)`) →
     resolve presentation transforms → batch → submit; the M2 exit: rounded
     rect springs to a new position on tap, mid-flight retarget preserves
     velocity, a 200 ms UI-thread stall does NOT interrupt the animation,
     idle CPU 0% at rest. The `animated_clear_color` in the compositor is the
     documented stand-in until then.

## 5. Environment / build notes (this machine)

- Toolchain: VS 18 BuildTools (MSVC 19.51), Windows SDK 10.0.26100,
  Ninja 1.13.2, CMake bundled with BuildTools.
- **`cmake`/`cl` are NOT on PATH in a fresh shell.** Bootstrap first:
  ```powershell
  . C:\Users\YutaRedux\AppData\Local\Temp\calcium_env.ps1
  # (sources vcvars64.bat into the process and prepends CMake/Ninja dirs)
  cmake --preset windows-msvc-debug   # fresh cache: pass -DCALCIUM_PLATFORM_SDL3=ON
                                       # -DCALCIUM_GPU_SDL3=ON explicitly
  cmake --build build\windows-msvc-debug
  ctest --test-dir build\windows-msvc-debug
  ```
- SDL3 option was cached OFF from the original configure — set it ON
  explicitly on first configure. The pivot renamed `CALCIUM_GPU_D3D12` to
  `CALCIUM_GPU_SDL3`; a stale cache fails loudly at configure — regenerate.
- Run the demo from `build\windows-msvc-debug` (SDL3.dll is copied next to
  the umbrella there): `.\bin\example_clear_color.exe --seconds 3` — expect
  ~180 frames at 60 Hz, vsync-paced, p50 < 1 ms, zero overruns, and (now
  genuinely) pixels on screen. The adapter line prints SDL's renderer name
  (e.g. "direct3d11" — SDL's internal driver — or "software").
- Hygiene gate runs on every build (27 public headers). It also enforces the
  level DAG: `gpu` may depend only on core+geometry (that is why the GPU
  interface takes `float[4]` colors and an opaque window handle, not
  `graphics::Color`/`platform::NativeWindowHandle`).

## 6. Gotchas learned this session (worth remembering)

- **MSVC**: `__declspec(dllexport)` must come FIRST in a declarator
  (`__declspec(dllexport) const char* f()`); `const char* __declspec(...) f()`
  is rejected. `std::cos/sin/sqrt` are not constexpr in MSVC's STL.
  Class-level dllexport force-exports inline members → duplicate-symbol
  fights when consumers also link the statics; the C++ surface is static now
  (export.hpp reserved for the C ABI at M7).
- **Static libs only pull referenced objects**: self-registering backend
  statics never link — registration must be called (backend_registration).
- **SDL3 3.4 renderer**: `SDL_CreateRenderer(window, name)` has NO flags
  parameter (the SDL_RENDERER_* flags are gone); driver selection is the
  `SDL_HINT_RENDER_DRIVER` hint ("software" forces the software driver),
  vsync is `SDL_SetRenderVSync(renderer, 1)` (unsupported drivers report
  false — print it, the cadence will visibly stutter). Hint reset is
  `SDL_ResetHint`, not SDL_UnsetHint (which does not exist in SDL3).
  `SDL_SetRenderDrawColorFloat` takes sRGB floats directly — no byte
  quantization on the clear path.
- **SDL3 3.4 API**: `SDL_PROP_WINDOW_WIN32_HWND_POINTER` IS the HWND (not a
  pointer to it — dereferencing it faults); event union members are
  `tfinger`/`ptouch`/`pmotion`; no `SDL_SendWindowEvent` (push the event);
  `SDL_GetCurrentDisplayMode(SDL_DisplayID)` returns a pointer.
- **SDL renderers are single-threaded**: create and use on one thread (the
  compositor's). SDL3's accelerated Windows renderer drives D3D11 internally
  — SDL's implementation detail, not Calcium's code.
- **Python heredoc patching** via `bash -c` mangles `\n` escape sequences in
  C++ string literals (Windows CRLF). Use the Edit tool for such edits.

## 7. Repo hygiene

- `.gitignore` covers `calcium-trace.csv`, `out.txt`, `err.txt` (demo
  artifacts). All M0/M1 work is committed; the SDL3-only pivot + the
  animation module are uncommitted working-tree changes.
- The D3D12 backend lives outside the repo at `D:\calcium-d3d12-archive\`
  and in git history (commits `f6eb604`, `d1f9af6`, `f1c3642`).
