# Calcium — Session Checkpoint (2026-08-05, ~3h)

Session goal: continue Calcium per docs/06-roadmap.md, committing and pushing after
each major feature. Everything below reflects the real, verified state of the repo.

---

## 1. What shipped (committed AND pushed to origin/main)

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

### M1 — Window and clear color (complete, commits `f6eb604` + `d1f9af6`, pushed)
- `ca::platform`: `Application`/`Window`/`Display` facades over an internal
  `PlatformBackend` (pimpl; backend never leaks into public headers, P5).
  Event types with full device provenance (P14): pointer (mouse/touch/pen/
  eraser, per-device ids, pressure, tilt), key, scroll (precise vs coarse,
  momentum phases), system. `Display::predicted_presentation_time()` with the
  honest `provides_hardware_presentation_prediction()` flag.
- `ca::gpu`: `GraphicsDevice`/`Swapchain`/`RenderPass` interfaces (Level 1
  depends only on core+geometry per the level DAG — clear color is `float[4]`,
  window handle is an opaque uint64). `RenderPass::acquired_at()` is the vsync
  anchor; `submitted_at()` the submit time.
- `ca::graphics::Color` (sRGB floats; full color pipeline is M2).
- Backends (never public): `platform_sdl3` (SDL3 3.4.14 vendored under
  `third_party/sdl3/`, version in VERSION.txt) and `gpu_d3d12`
  (waitable-swapchain loop, WARP fallback). Registration is explicit:
  `ca::calcium_register_backends()` — static libs only pull referenced
  objects, so self-registering statics never link (documented in
  `src/backends/backend_registration.hpp`).
- `src/compositor`: `Compositor` owning device+swapchain+thread.
- `tools/calcium-tracer`: CSV → p50/p95/p99 + overrun counts.
- `examples/02-clear-color`: the M1 exit demo; `--seconds N` for CI smoke.
- **Verified measurement** (before the regression below): 60 Hz display,
  ~51–60 fps vsync-paced, compositor p50 0.30 ms / p99 0.42 ms, zero budget
  overruns.
- C++ surface is the static modules; the DLL is reserved for the C ABI (M7).

## 2. What is done but NOT yet committed (working tree)

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

**The whole tree builds and all 13 test suites pass** except for one thing:
the demo regression below. The animation module is independent of it and can
be committed now (see §4 step 1).

## 3. KNOWN ISSUE — the demo's present path (IN FLIGHT, tree state affected)

**Symptom**: `example_clear_color --seconds 3` ships only 1 frame.

**Root cause — fully diagnosed**:
1. `Present(1, 0)` returns `DXGI_ERROR_INVALID_CALL` (0x887A0001) on the very
   first frame. Flip-model swapchains are **affine to the thread that created
   them**: the compositor creates the device+swapchain on the MAIN thread
   (`Compositor::create`) but presents on the COMPOSITOR thread.
2. The earlier "working" runs masked this: the loop never checked Present's
   HRESULT, and with no fence signal queued after the invalid present, the
   device survived and the loop paced on the waitable — the window in fact
   never presented anything.
3. The new per-buffer fence (signaled after Present) EXPOSED it: a fence
   signal queued after an invalid present kills the device
   (`DXGI_ERROR_DEVICE_REMOVED`, 0x887A0005) on the next frame.

**The fix (per docs/02-architecture.md §2.3: "ca::gpu::Device — Compositor
thread")**: create the device AND swapchain ON the compositor thread.

**In-flight state — `src/compositor/compositor.hpp` has ALREADY been updated**
(new API: `device_ready()`, `failure_message()`, and the `create()` doc
comment saying no GPU work happens there) **but `compositor.cpp` has NOT yet
been migrated**. The tree compiles (the new members are simply unused), but
the demo still exhibits the 1-frame issue. Also, `d3d12_device.cpp` currently
contains leftover `DBG ...` fprintf instrumentation (3 lines: present hr,
reset hr, list hr) that must be removed.

**Exactly what remains to finish the fix**:
```cpp
// src/compositor/compositor.cpp — Compositor::create:
//   remove the device/swapchain creation block (keep: window validation,
//   trace-file open, calcium_register_backends()).
// Compositor::run_loop():
//   at the top (compositor thread), before the while loop:
//     create device; on failure: failure_message_ = "..."; return;
//     create swapchain (same calls, same sizes); on failure: set message;
//     device_info_ = device_->adapter_info();
//     device_ready_ = true;
//   inside the loop: if Present fails, set failure_message_ and break
//     (begin_clear_pass/end_and_present already surface failure via Result;
//      add an explicit Present-hr check in end_and_present).
// examples/02-clear-color/main.cpp:
//   move the "adapter : ..." startup print to AFTER compositor->start()
//   (device_info is now populated by the thread); print failure_message()
//   when !device_ready().
// d3d12_device.cpp: remove the three DBG fprintf lines.
```
After that, the expected result returns: ~60 fps vsync-paced, p50 < 1 ms,
zero overruns, AND (now genuinely) pixels on screen.

## 4. Next steps after the demo fix (in order)

1. **Commit the animation module** (`ca::animation`) — it is complete and
   green; it does not depend on the demo fix.
2. **Finish the compositor-thread restructure** (§3) and verify:
   `ctest` (13 suites) + `example_clear_color --seconds 3` (~180 frames).
3. Commit the demo fix ("compositor owns the GPU: flip swapchain created on
   the compositor thread").
4. Continue M2 (docs/06-roadmap.md):
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
  cmake --preset windows-msvc-debug   # once; pass -DCALCIUM_PLATFORM_SDL3=ON
                                       # -DCALCIUM_GPU_D3D12=ON if the cache is fresh
  cmake --build build\windows-msvc-debug
  ctest --test-dir build\windows-msvc-debug
  ```
- SDL3 option was cached OFF from the original configure — set it ON
  explicitly on first configure.
- Run the demo from `build\windows-msvc-debug` (SDL3.dll is copied next to
  the umbrella there): `.\bin\example_clear_color.exe --seconds 3`.
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
- **SDL3 3.4 API**: `SDL_PROP_WINDOW_WIN32_HWND_POINTER` IS the HWND (not a
  pointer to it — dereferencing it faults); event union members are
  `tfinger`/`ptouch`/`pmotion`; no `SDL_SendWindowEvent` (push the event);
  `SDL_GetCurrentDisplayMode(SDL_DisplayID)` returns a pointer.
- **D3D12**: flip-model swapchains are thread-affine to their creator —
  create them on the compositor thread. The waitable's "slot free" signal
  does NOT protect buffer reuse; a fence signaled AFTER Present (in queue
  order) is the correct reuse/pacing guard.
- **Python heredoc patching** via `bash -c` mangles `\n` escape sequences in
  C++ string literals (Windows CRLF). Use the Edit tool for such edits.

## 7. Repo hygiene

- `.gitignore` covers `calcium-trace.csv`, `out.txt`, `err.txt` (demo
  artifacts). All M0/M1 work is committed; the animation module + the
  in-flight compositor restructure are uncommitted working-tree changes.
