# Calcium — Project Structure

---

## 1. Naming conventions

Locked before the first line of code, because renaming later is a breaking change.

| Kind | Convention | Example |
|---|---|---|
| Namespace | short, lowercase, single word | `ca::graphics`, `ca::animation` |
| Class / struct | long descriptive `PascalCase` | `AnimatableProperty`, `DisplayListRecorder` |
| Free function / method | `snake_case` | `add_sublayer`, `begin_transaction` |
| Enum type | `PascalCase` | `BlendMode`, `LineBreakStrategy` |
| Enum value | `snake_case` in `enum class` | `BlendMode::source_over` |
| Member variable | `snake_case_` trailing underscore | `presentation_transform_` |
| Constant / constexpr | `snake_case` | `default_corner_radius` |
| Template parameter | `PascalCase` | `template <typename ValueType>` |
| Macro | `CA_SCREAMING_SNAKE` (rare) | `CA_ASSERT_UI_THREAD` |
| Header | `snake_case.hpp` | `animatable_property.hpp` |
| C API symbol | `ca_` + `snake_case` | `ca_layer_set_opacity` |
| C API type | `ca_` + `snake_case` + `_t` | `ca_layer_t`, `ca_spring_config_t` |

Descriptive over terse, following Apple: `ContentSizeCategoryDidChangeObserver`,
not `SizeObs`. `measure_intrinsic_content_size`, not `msr`.

**Namespace aliases** are provided and expected in application code:

```cpp
namespace gfx  = ca::graphics;
namespace anim = ca::animation;
namespace geo  = ca::geometry;
```

---

## 2. Directory layout

```
calcium/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
│
├── docs/
│   ├── 00-overview.md … 06-roadmap.md
│   ├── spec/
│   │   ├── display-list.md        ← normative IR semantics
│   │   ├── color-pipeline.md      ← color spaces, blending, gamma
│   │   ├── text-metrics.md        ← baselines, advances, fallback order
│   │   ├── layout-negotiation.md  ← exact three-phase rules
│   │   ├── motion-scheme.md       ← named spring table per platform
│   │   ├── input-semantics.md     ← gesture arbitration, hit-test slop
│   │   └── c-abi.md               ← versioning, handle, error rules
│   └── guides/
│
├── include/calcium/               ← PUBLIC HEADERS. No 3rd-party includes. Ever.
│   ├── calcium.hpp                ← umbrella C++
│   ├── calcium.h                  ← umbrella C ABI
│   │
│   ├── core/
│   │   ├── arena_allocator.hpp
│   │   ├── handle.hpp             ← {index, generation} typed handles
│   │   ├── identifier.hpp         ← stable identity (P12)
│   │   ├── interned_string.hpp
│   │   ├── result.hpp             ← Result<T, Error>, no exceptions across ABI
│   │   ├── small_vector.hpp
│   │   ├── thread_affinity.hpp    ← CA_ASSERT_UI_THREAD etc.
│   │   └── time.hpp               ← Timestamp, Duration, monotonic clock
│   │
│   ├── geometry/
│   │   ├── point.hpp  size.hpp  rect.hpp  inset.hpp
│   │   ├── affine_transform.hpp   ← 2D 3x2
│   │   ├── transform_3d.hpp       ← 4x4, CATransform3D-shaped (Twell-backed)
│   │   ├── quaternion.hpp
│   │   ├── path.hpp
│   │   ├── path_builder.hpp
│   │   └── rounded_rectangle.hpp  ← incl. continuous (squircle) corners
│   │
│   ├── graphics/
│   │   ├── color.hpp              ← color-space-aware
│   │   ├── color_space.hpp
│   │   ├── paint.hpp
│   │   ├── gradient.hpp
│   │   ├── blend_mode.hpp
│   │   ├── image.hpp
│   │   ├── image_filter.hpp
│   │   ├── stroke_style.hpp
│   │   ├── display_list.hpp       ← immutable sealed IR
│   │   ├── display_list_recorder.hpp
│   │   ├── drawing_context.hpp    ← Level 2 primary surface
│   │   └── shadow.hpp
│   │
│   ├── text/
│   │   ├── font.hpp  font_descriptor.hpp  font_manager.hpp
│   │   ├── font_feature.hpp       ← OpenType features
│   │   ├── typography_settings.hpp← tracking, optical sizing, stem darkening
│   │   ├── attributed_string.hpp
│   │   ├── text_attributes.hpp
│   │   ├── glyph_run.hpp
│   │   ├── text_shaper.hpp
│   │   ├── paragraph.hpp          ← multi-line layout result
│   │   ├── paragraph_builder.hpp
│   │   └── text_selection.hpp     ← cursor affinity, grapheme navigation
│   │
│   ├── gpu/
│   │   ├── graphics_device.hpp
│   │   ├── render_pass.hpp
│   │   ├── pipeline_state.hpp
│   │   ├── shader_module.hpp      ← Calcium shader IR
│   │   ├── texture.hpp  buffer.hpp  sampler.hpp
│   │   └── custom_render_pass.hpp ← Level 1 user entry point
│   │
│   ├── platform/
│   │   ├── application.hpp
│   │   ├── window.hpp
│   │   ├── display.hpp            ← predicted_presentation_time()
│   │   ├── event.hpp
│   │   ├── pointer_event.hpp      ← full device provenance (P14)
│   │   ├── key_event.hpp  keyboard.hpp
│   │   ├── scroll_event.hpp       ← precise vs coarse, momentum phase
│   │   ├── text_input_client.hpp  ← IME composition
│   │   ├── clipboard.hpp  drag_and_drop.hpp
│   │   ├── file_dialog.hpp
│   │   ├── application_lifecycle.hpp ← mobile background/foreground
│   │   └── safe_area.hpp
│   │
│   ├── animation/
│   │   ├── animatable_property.hpp
│   │   ├── spring_configuration.hpp
│   │   ├── motion.hpp             ← semantic named motions (P9)
│   │   ├── motion_scheme.hpp
│   │   ├── decay_configuration.hpp
│   │   ├── transaction.hpp
│   │   ├── animation_coordinator.hpp ← owns twell_context
│   │   ├── property_link.hpp      ← twell driver links
│   │   ├── gesture_kinetics.hpp   ← momentum handoff
│   │   ├── rubber_band.hpp
│   │   └── keyframe_track.hpp     ← for the cases springs don't fit
│   │
│   ├── layer/
│   │   ├── layer.hpp
│   │   ├── layer_tree.hpp
│   │   ├── content_layer.hpp  text_layer.hpp  image_layer.hpp
│   │   ├── gradient_layer.hpp  shape_layer.hpp
│   │   ├── mask_layer.hpp  filter_layer.hpp
│   │   ├── scroll_layer.hpp
│   │   └── layer_delegate.hpp     ← draw callback → Level 2
│   │
│   ├── layout/
│   │   ├── size_proposal.hpp
│   │   ├── layout_context.hpp
│   │   ├── alignment.hpp  alignment_guide.hpp
│   │   ├── edge_insets.hpp
│   │   ├── stack_layout.hpp  grid_layout.hpp  flow_layout.hpp
│   │   ├── layout_priority.hpp
│   │   └── custom_layout.hpp      ← user layout protocol
│   │
│   ├── view/
│   │   ├── view.hpp
│   │   ├── view_controller.hpp
│   │   ├── gesture_recognizer.hpp
│   │   ├── tap_gesture_recognizer.hpp  pan_gesture_recognizer.hpp
│   │   ├── pinch_gesture_recognizer.hpp  long_press_gesture_recognizer.hpp
│   │   ├── gesture_arbiter.hpp    ← conflict resolution
│   │   ├── focus_manager.hpp  responder_chain.hpp
│   │   └── hit_test_policy.hpp
│   │
│   ├── accessibility/
│   │   ├── accessibility_node.hpp
│   │   ├── accessibility_role.hpp
│   │   ├── accessibility_action.hpp
│   │   ├── accessibility_tree.hpp
│   │   └── accessibility_notification.hpp
│   │
│   ├── widget/
│   │   ├── button.hpp  label.hpp  text_field.hpp  text_editor.hpp
│   │   ├── scroll_view.hpp  list_view.hpp  table_view.hpp
│   │   ├── slider.hpp  toggle_switch.hpp  checkbox.hpp
│   │   ├── segmented_control.hpp  progress_indicator.hpp
│   │   ├── popover.hpp  menu.hpp  context_menu.hpp
│   │   ├── split_view.hpp  tab_view.hpp  navigation_stack.hpp
│   │   ├── sheet.hpp              ← the draggable bottom sheet
│   │   ├── theme.hpp  style.hpp   ← incl. MotionScheme
│   │   └── control_state.hpp
│   │
│   └── compose/
│       ├── element.hpp            ← declarative node description
│       ├── build_context.hpp
│       ├── state.hpp  binding.hpp  observable.hpp
│       ├── reconciler.hpp
│       ├── modifier.hpp           ← chainable modifiers
│       ├── environment.hpp        ← implicit context propagation
│       └── composed_view.hpp      ← bridge → Level 3
│
├── src/                           ← implementation; may include 3rd-party
│   ├── core/ geometry/ graphics/ text/ gpu/ platform/ animation/
│   ├── layer/ layout/ view/ accessibility/ widget/ compose/
│   ├── c_api/                     ← calcium.h implementation
│   └── backends/
│       ├── raster_skia/
│       ├── platform_sdl3/  platform_appkit/  platform_uikit/
│       ├── platform_android/  platform_win32/
│       ├── shaper_harfbuzz/  unicode_icu/
│       ├── font_coretext/  font_directwrite/  font_fontconfig/
│       └── gpu_metal/  gpu_d3d12/  gpu_vulkan/  gpu_gl/
│
├── third_party/
│   └── twell/twell.h              ← the animation kernel (in-house, vendored)
│
├── tests/
│   ├── unit/
│   ├── golden/                    ← display-list conformance images
│   ├── conformance/               ← backend equivalence suite
│   ├── performance/               ← frame-budget CI gates
│   └── fuzz/                      ← path, text, display-list parsers
│
├── tools/
│   ├── calcium-inspector/         ← live layer tree + layout debugger
│   ├── calcium-tracer/            ← per-stage frame timing
│   ├── calcium-shaderc/           ← shader IR compiler
│   └── calcium-bindgen/           ← binding generator from calcium.h
│
├── bindings/
│   ├── python/  rust/  go/  csharp/
│
└── examples/
    ├── 01-hello-rectangle/        ← Level 3, ~40 lines
    ├── 02-spring-playground/
    ├── 03-bottom-sheet/           ← the Twell showcase
    ├── 04-scroll-momentum/
    ├── 05-declarative-counter/    ← Level 5, ~20 lines
    ├── 06-custom-layout/
    ├── 07-custom-shader/          ← Level 1
    ├── 08-text-editor/
    └── 09-professional-app/       ← docked panels, timeline, 50k-node canvas
```

---

## 3. Module targets and dependency enforcement

Each level is a separate CMake target with `PUBLIC`/`PRIVATE` link visibility
carrying the dependency rule into the build system:

```cmake
add_library(calcium_core       STATIC …)
add_library(calcium_geometry   STATIC …)   # → core
add_library(calcium_graphics   STATIC …)   # → geometry
add_library(calcium_text       STATIC …)   # → graphics
add_library(calcium_gpu        STATIC …)   # → core
add_library(calcium_platform   STATIC …)   # → core, geometry
add_library(calcium_animation  STATIC …)   # → core, geometry  [+ twell PRIVATE]
add_library(calcium_layer      STATIC …)   # → graphics, animation
add_library(calcium_layout     STATIC …)   # → geometry
add_library(calcium_view       STATIC …)   # → layer, layout, platform, a11y
add_library(calcium_widget     STATIC …)   # → view, text
add_library(calcium_compose    STATIC …)   # → widget
add_library(calcium            SHARED …)   # umbrella + C API

# Twell is PRIVATE to animation. Its symbols never reach a public header.
target_link_libraries(calcium_animation PRIVATE twell)
```

Backends are separate targets linked only into the umbrella:

```cmake
option(CALCIUM_RASTER_SKIA      "Skia rasterizer backend"    ON)
option(CALCIUM_PLATFORM_SDL3    "SDL3 platform backend"      ON)
option(CALCIUM_PLATFORM_NATIVE  "Native platform backends"   ON)
option(CALCIUM_SHAPER_HARFBUZZ  "HarfBuzz shaping backend"   ON)
option(CALCIUM_UNICODE_ICU      "ICU unicode backend"        ON)
```

### 3.1 The header hygiene gate

A CI script that is not optional:

```
tools/ci/check_header_hygiene.py
  1. Every file in include/calcium/ — assert no #include resolves outside
     include/calcium/ or the C++ standard library.
  2. Assert no identifier matching (Sk|SDL_|hb_|u_|ICU|twell_) appears
     anywhere in include/.
  3. Assert the level dependency DAG is acyclic and downward-only.
  4. Assert every public header compiles standalone (IWYU-style check).
```

If this script passes, P5 holds mechanically. If it is not in CI, P5 degrades
into an aspiration within about three months of real development. This is the
cheapest high-value piece of infrastructure in the project.

---

## 4. Build matrix

| Platform | Compiler | GPU backend | Platform backend | Font provider |
|---|---|---|---|---|
| Windows 10+ | MSVC 19.3x, clang-cl | D3D12 → Vulkan → GL | Win32, SDL3 | DirectWrite |
| macOS 12+ | Apple Clang 14+ | Metal | AppKit, SDL3 | CoreText |
| Linux | GCC 12+, Clang 15+ | Vulkan → GL | SDL3 (X11/Wayland) | FontConfig |
| iOS 15+ | Apple Clang 14+ | Metal | UIKit | CoreText |
| Android 8+ (API 26) | NDK r26 Clang | Vulkan → GLES3 | Android (JNI) | NDK/FontConfig |

C++20 required: concepts, `std::span`, designated initializers, `constexpr`
containers, three-way comparison, `std::source_location` for diagnostics. Modules
are deliberately **not** used — tooling support across five platforms and five
compilers is not yet uniform enough, and the cost of being wrong is a build system
nobody can debug.

---

## 5. Standards for public API code

- **No exceptions across the API boundary.** `ca::core::Result<T, Error>` is the
  return channel. Internally, exceptions are permitted in non-frame-path code.
- **No RTTI in the frame path.** Type discrimination is via explicit tags.
- **No virtual calls in the per-layer inner loop.** Backend interfaces are
  virtual at the *batch* level (once per frame, per pass), never per draw.
- **`[[nodiscard]]` on every `Result`** and every builder-style return.
- **Rule of zero** where possible; explicit copy/move where resources are owned.
- **`constexpr` all geometry.** Points, sizes, rects, transforms, and colors are
  compile-time constructible.
- **Designated initializers for configuration structs.** Reads like Objective-C
  keyword arguments, which is the ergonomic effect being chased:
  `SpringConfiguration{.mass = 1.0, .stiffness = 250.0, .damping = 18.0}`.
- **Ownership is spelled in the type.** `std::unique_ptr` for exclusive,
  `std::shared_ptr` only for genuinely shared immutable resources (fonts, images,
  sealed display lists), raw handles for tree nodes.
