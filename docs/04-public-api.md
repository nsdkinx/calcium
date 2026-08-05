# Calcium — Public API Design

Design targets: Apple-descriptive naming, `snake_case` methods, short lowercase
namespaces, no third-party types, and every level reachable from the one above.

---

## 1. Foundation types (Level 0 / geometry)

```cpp
namespace ca::core {

// Typed, generation-checked handle. A stale handle is detected, not dereferenced.
template <typename Tag>
class Handle {
public:
    constexpr Handle() = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] constexpr uint32_t index() const noexcept;
    [[nodiscard]] constexpr uint32_t generation() const noexcept;
    constexpr bool operator==(const Handle&) const noexcept = default;
private:
    uint32_t index_ = invalid_index;
    uint32_t generation_ = 0;
};

// Error channel. No exceptions cross the API boundary.
template <typename ValueType>
class [[nodiscard]] Result {
public:
    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] const ValueType& value() const&;
    [[nodiscard]] ValueType&& take_value() &&;
    [[nodiscard]] Error error() const noexcept;
    [[nodiscard]] ValueType value_or(ValueType fallback) const;
    explicit operator bool() const noexcept { return has_value(); }
};

class Timestamp {   // monotonic, seconds as double — matches Twell's clock
public:
    static Timestamp now() noexcept;
    [[nodiscard]] double seconds_since_epoch() const noexcept;
    [[nodiscard]] Duration operator-(Timestamp) const noexcept;
};

} // namespace ca::core
```

```cpp
namespace ca::geometry {

struct Point { float x = 0.0f, y = 0.0f; /* constexpr arithmetic */ };
struct Size  { float width = 0.0f, height = 0.0f;
               [[nodiscard]] constexpr bool is_empty() const noexcept; };

struct Rect {
    Point origin;
    Size  size;

    [[nodiscard]] static constexpr Rect from_edges(float left, float top,
                                                   float right, float bottom);
    [[nodiscard]] constexpr float min_x() const noexcept;
    [[nodiscard]] constexpr float max_x() const noexcept;
    [[nodiscard]] constexpr Point center() const noexcept;
    [[nodiscard]] constexpr Rect inset_by(const EdgeInsets&) const noexcept;
    [[nodiscard]] constexpr Rect union_with(const Rect&) const noexcept;
    [[nodiscard]] constexpr bool contains_point(Point) const noexcept;
};

// 4x4, deliberately CATransform3D-shaped, Twell-interoperable.
struct Transform3D {
    double m11, m12, m13, m14;
    double m21, m22, m23, m24;
    double m31, m32, m33, m34;
    double m41, m42, m43, m44;

    [[nodiscard]] static constexpr Transform3D identity() noexcept;
    [[nodiscard]] static Transform3D make_translation(double tx, double ty, double tz) noexcept;
    [[nodiscard]] static Transform3D make_scale(double sx, double sy, double sz) noexcept;
    [[nodiscard]] static Transform3D make_rotation(const Quaternion&) noexcept;
    [[nodiscard]] static Transform3D make_perspective(double eye_distance) noexcept;

    [[nodiscard]] Transform3D concatenating(const Transform3D&) const noexcept;
    [[nodiscard]] std::optional<Transform3D> inverted() const noexcept;
    [[nodiscard]] Point apply_to_point(Point) const noexcept;

    // Decomposition is what makes transform *animation* correct: interpolating
    // components avoids the shear artifacts of naive matrix lerp.
    struct DecomposedComponents {
        Vector3 translation, scale, skew;
        Quaternion rotation;
        Vector4 perspective;
    };
    [[nodiscard]] DecomposedComponents decompose() const noexcept;
    [[nodiscard]] static Transform3D recompose(const DecomposedComponents&) noexcept;
};

// Continuous-curvature corners (squircles), which is a large part of why
// Apple UI reads as "smooth" — G2 continuity instead of G1 circular arcs.
enum class CornerCurve { circular, continuous };

struct RoundedRectangle {
    Rect  bounds;
    float top_leading_radius = 0.0f, top_trailing_radius = 0.0f;
    float bottom_leading_radius = 0.0f, bottom_trailing_radius = 0.0f;
    CornerCurve corner_curve = CornerCurve::continuous;

    [[nodiscard]] static RoundedRectangle uniform(
        Rect, float radius, CornerCurve = CornerCurve::continuous);
    [[nodiscard]] static RoundedRectangle capsule(Rect);
    [[nodiscard]] Path to_path() const;
};

} // namespace ca::geometry
```

---

## 2. Level 3 — the retained core

This is the heart of the framework. Everything above is sugar; everything below is
plumbing.

### 2.1 Animatable properties

```cpp
namespace ca::animation {

struct SpringConfiguration {
    double mass = 1.0;
    double stiffness = 250.0;
    double damping = 18.0;      // near-critical for mass=1, k=250
    double initial_velocity = 0.0;

    // Designers think in duration and bounce, not in stiffness.
    [[nodiscard]] static SpringConfiguration with_response_and_damping_ratio(
        double response_seconds, double damping_ratio);
    [[nodiscard]] static SpringConfiguration critically_damped(double response_seconds);

    [[nodiscard]] double damping_ratio() const noexcept;
    [[nodiscard]] double settling_duration_estimate() const noexcept;
};

// P9: name the intent, not the numbers. Resolved through the active MotionScheme.
class Motion {
public:
    [[nodiscard]] static Motion standard();     // the everyday critically damped
    [[nodiscard]] static Motion emphasized();   // slight overshoot, hero moments
    [[nodiscard]] static Motion snappy();       // fast, no overshoot
    [[nodiscard]] static Motion gentle();       // slow, soft
    [[nodiscard]] static Motion playful();      // clearly underdamped
    [[nodiscard]] static Motion immediate();    // no animation

    [[nodiscard]] static Motion spring(SpringConfiguration);
    [[nodiscard]] static Motion decay(DecayConfiguration);

    [[nodiscard]] Motion with_delay(core::Duration) const;
    [[nodiscard]] Motion with_speed_multiplier(double) const;
};

// The central abstraction. Backed 1:1 by a Twell property.
template <typename ValueType>
class AnimatableProperty {
public:
    // Model value: the destination. Layout, hit-testing, and app logic use this.
    [[nodiscard]] ValueType model_value() const;

    // Presentation value: on screen right now. Last published compositor
    // snapshot — the name says so, because it is not synchronously live (P2).
    [[nodiscard]] ValueType presentation_value() const;

    // Writes enter the ambient transaction; they do not store immediately (P3).
    void set_value(ValueType);                       // uses transaction's motion
    void set_value(ValueType, const Motion&);        // explicit override
    void set_value_immediately(ValueType);           // no animation, snaps both

    // Direct manipulation: 1:1 tracking with asymptotic rubber banding at edges.
    void begin_tracking_gesture(GestureKinetics&, ValueType lower, ValueType upper);
    void end_tracking_with_spring(GestureKinetics&, ValueType target, const Motion&);
    void end_tracking_with_decay(GestureKinetics&, double deceleration_rate = 0.998);

    [[nodiscard]] bool is_at_rest() const;
    [[nodiscard]] ValueType current_velocity() const;
    void stop_animation_at_presentation_value();      // freeze where it is
    void on_reach_rest(std::function<void()>);        // driven by Twell's rest queue
};

using AnimatableFloat       = AnimatableProperty<float>;
using AnimatablePoint       = AnimatableProperty<geometry::Point>;
using AnimatableTransform3D = AnimatableProperty<geometry::Transform3D>;
using AnimatableColor       = AnimatableProperty<graphics::Color>;

// Coupled kinematics — Twell driver links. Zero per-frame application code.
class PropertyLink {
public:
    struct Configuration {
        double source_lower_bound, source_upper_bound;
        double target_lower_bound, target_upper_bound;
        MappingCurve curve = MappingCurve::linear;
        bool clamps_to_bounds = true;
    };
    [[nodiscard]] static PropertyLink connect(const AnimatableFloat& source,
                                              AnimatableFloat& target,
                                              Configuration);
    void disconnect();
};

// P3: atomic batched mutation, with an ambient default motion.
class Transaction {
public:
    [[nodiscard]] static Transaction begin();
    [[nodiscard]] static Transaction begin_with_motion(const Motion&);
    void set_default_motion(const Motion&);
    void set_disables_animation(bool);
    void add_completion_handler(std::function<void()>);
    void commit();                       // also on destruction
    [[nodiscard]] static Transaction& current();
};

// Convenience wrapper — the form nearly all application code uses.
void animate(const Motion&, std::function<void()> mutations);
void animate_with_completion(const Motion&,
                             std::function<void()> mutations,
                             std::function<void()> completion);

} // namespace ca::animation
```

Usage — the reason the whole architecture exists:

```cpp
// Interrupt a spring mid-flight. Velocity is preserved automatically by Twell's
// additive impulse superposition; there is no bookkeeping to get wrong.
anim::animate(anim::Motion::standard(), [&] {
    card.position().set_value({400.0f, 200.0f});
    card.opacity().set_value(1.0f);
});
```

### 2.2 Layers

```cpp
namespace ca::layer {

using LayerHandle = core::Handle<struct LayerTag>;

class Layer {
public:
    [[nodiscard]] static Layer create();

    // --- Geometry (model space) ---
    void set_bounds(geometry::Rect);
    void set_anchor_point(geometry::Point);      // normalized, default {0.5, 0.5}
    [[nodiscard]] geometry::Rect bounds() const;

    // --- Compositor-resolvable animatables (§2.2 of 02-architecture) ---
    // Animating these NEVER invalidates a recorded display list.
    [[nodiscard]] animation::AnimatablePoint&       position();
    [[nodiscard]] animation::AnimatableTransform3D& transform();
    [[nodiscard]] animation::AnimatableFloat&       opacity();
    [[nodiscard]] animation::AnimatableFloat&       corner_radius();
    [[nodiscard]] animation::AnimatableColor&       background_color();
    [[nodiscard]] animation::AnimatableFloat&       shadow_opacity();
    [[nodiscard]] animation::AnimatableFloat&       shadow_radius();
    [[nodiscard]] animation::AnimatablePoint&       shadow_offset();

    // --- Tree ---
    void add_sublayer(Layer);
    void insert_sublayer_at_index(Layer, size_t);
    void insert_sublayer_above(Layer, const Layer& sibling);
    void remove_from_superlayer();
    [[nodiscard]] std::span<const LayerHandle> sublayers() const;

    // --- Content: the doorway to Level 2 (P7) ---
    void set_delegate(LayerDelegate*);           // draw callback
    void set_display_list(graphics::DisplayList); // pre-recorded
    void set_needs_display();
    void set_needs_display_in_rect(geometry::Rect);

    // --- Compositing ---
    void set_mask_layer(Layer);
    void set_clips_sublayers(bool);
    void set_blend_mode(graphics::BlendMode);
    void set_image_filter(graphics::ImageFilter);
    void set_rasterization_policy(RasterizationPolicy);
    void set_corner_curve(geometry::CornerCurve);

    // --- Coordinate conversion (explicit about which resolution — P1) ---
    [[nodiscard]] geometry::Point convert_point_to_layer(
        geometry::Point, const Layer& target, CoordinateResolution) const;

    // --- Identity (P12) ---
    void set_identifier(core::Identifier);
    [[nodiscard]] core::Identifier identifier() const;
};

// Level 3 → Level 2 bridge. This is why no escape hatch is needed.
class LayerDelegate {
public:
    virtual ~LayerDelegate() = default;
    virtual void draw_layer_content(const Layer&, graphics::DrawingContext&) = 0;
};

// Caching hint. Explicit, because implicit caching is unpredictable and
// professional applications need to control their VRAM budget.
enum class RasterizationPolicy {
    never,              // re-record every change
    when_beneficial,    // heuristic
    always,             // cache to texture, cheap to transform
    always_with_scale   // cache at a fixed scale, for zoomable canvases
};

} // namespace ca::layer
```

### 2.3 Views, layout, gestures

```cpp
namespace ca::view {

class View {
public:
    // --- Layout (P11: measure is pure) ---
    [[nodiscard]] virtual geometry::Size measure(const layout::SizeProposal&) const;
    virtual void perform_layout(geometry::Size final_size);
    void set_needs_layout();
    [[nodiscard]] layout::LayoutPriority layout_priority() const;

    // --- Content ---
    virtual void draw(graphics::DrawingContext&);     // → Level 2
    [[nodiscard]] layer::Layer& backing_layer();      // → Level 3 layers

    // --- Hierarchy ---
    void add_subview(std::unique_ptr<View>);
    void remove_from_superview();
    [[nodiscard]] std::span<const std::unique_ptr<View>> subviews() const;

    // --- Input ---
    void add_gesture_recognizer(std::unique_ptr<GestureRecognizer>);
    virtual bool handle_pointer_event(const platform::PointerEvent&);
    virtual bool handle_key_event(const platform::KeyEvent&);
    virtual bool handle_scroll_event(const platform::ScrollEvent&);
    void set_hit_test_policy(HitTestPolicy);          // P1 corollary
    [[nodiscard]] virtual View* hit_test(geometry::Point) ;

    // --- Focus ---
    void set_accepts_focus(bool);
    [[nodiscard]] bool is_focused() const;
    virtual void did_become_focused();
    virtual void did_lose_focus();

    // --- Accessibility: present from v0.1, not retrofitted (P14) ---
    void set_accessibility_role(accessibility::Role);
    void set_accessibility_label(std::string_view);
    void set_accessibility_value(std::string_view);
    void set_accessibility_hint(std::string_view);
    void add_accessibility_action(accessibility::Action, std::function<void()>);
    void set_accessibility_traits(accessibility::Traits);
};

// Gesture recognizers, with explicit arbitration — the part most frameworks
// under-specify and then spend years patching.
class GestureRecognizer {
public:
    enum class State { possible, began, changed, ended, cancelled, failed };
    [[nodiscard]] State state() const;

    void require_failure_of(GestureRecognizer&);
    void set_cancels_touches_in_view(bool);
    void set_delays_touches_began(bool);
    void set_allowed_pointer_types(platform::PointerTypeMask);
};

class PanGestureRecognizer : public GestureRecognizer {
public:
    [[nodiscard]] geometry::Point translation_in_view(const View&) const;

    // Kinetics live here — the handoff object Twell consumes directly.
    [[nodiscard]] animation::GestureKinetics& kinetics();

    void set_minimum_number_of_touches(int);
    void set_pan_axis_constraint(AxisConstraint);
};

} // namespace ca::view
```

### 2.4 A complete Level-3 example: the draggable bottom sheet

The canonical Twell showcase. Rubber banding, momentum handoff, and coupled
backdrop scaling, with no per-frame application code at all.

```cpp
class BottomSheetViewController : public ca::view::ViewController {
public:
    void view_did_load() override {
        auto pan = std::make_unique<view::PanGestureRecognizer>();
        pan->set_pan_axis_constraint(AxisConstraint::vertical);
        pan->set_action([this](view::PanGestureRecognizer& recognizer) {
            handle_sheet_pan(recognizer);
        });
        sheet_view_->add_gesture_recognizer(std::move(pan));

        // Backdrop scales as the sheet rises. One declaration, then physics
        // handles it every frame on the compositor thread. This tracks the
        // sheet's PRESENTATION value, so it stays glued to the finger.
        backdrop_link_ = anim::PropertyLink::connect(
            sheet_view_->backing_layer().position().y_component(),
            backdrop_view_->backing_layer().scale_component(),
            { .source_lower_bound = collapsed_offset_,
              .source_upper_bound = expanded_offset_,
              .target_lower_bound = 1.00,
              .target_upper_bound = 0.92,
              .curve              = anim::MappingCurve::ease_out,
              .clamps_to_bounds   = true });
    }

private:
    void handle_sheet_pan(view::PanGestureRecognizer& recognizer) {
        auto& position = sheet_view_->backing_layer().position();

        switch (recognizer.state()) {
        case State::began:
        case State::changed:
            // 1:1 tracking. Past the bounds, Twell applies asymptotic rubber
            // banding automatically — the sheet resists instead of stopping dead.
            position.begin_tracking_gesture(recognizer.kinetics(),
                                            expanded_offset_, collapsed_offset_);
            break;

        case State::ended: {
            // Flick velocity decides the destination; that same velocity is
            // handed to the spring, so there is no discontinuity at release.
            const double velocity = recognizer.kinetics().velocity().y;
            const bool should_expand =
                velocity < -flick_velocity_threshold ||
                (velocity < flick_velocity_threshold &&
                 position.presentation_value().y < midpoint_offset_);

            position.end_tracking_with_spring(
                recognizer.kinetics(),
                {position.model_value().x,
                 should_expand ? expanded_offset_ : collapsed_offset_},
                anim::Motion::standard());
            break;
        }
        case State::cancelled:
            position.end_tracking_with_spring(recognizer.kinetics(),
                                              {position.model_value().x, collapsed_offset_},
                                              anim::Motion::gentle());
            break;
        default: break;
        }
    }
};
```

---

## 3. Level 2 — drawing

Reached from any layer or view without leaving the framework.

M2 ships the subset — the signatures below are the design target and remain
stable as the rest lands:

- `DrawingContext`: save/restore, 2D affine concatenation, rect and rounded-
  rect fills, global alpha (folded into paints at record time). `Transform3D`
  concatenation, paths, text, images, and filters land with their milestones.
- `Paint`: solid colors only (`solid_color`, `with_alpha_multiplied_by`);
  gradients and shaders land with the color pipeline.
- `DisplayListRecorder` + sealed `DisplayList`: the IR's M2 record set and
  encoding are the contract (docs/02 §6.1).

```cpp
namespace ca::graphics {

class DrawingContext {
public:
    // --- State stack ---
    void save_state();
    void restore_state();
    void concatenate_transform(const geometry::Transform3D&);
    void clip_to_rect(geometry::Rect, AntialiasMode = AntialiasMode::enabled);
    void clip_to_rounded_rectangle(const geometry::RoundedRectangle&);
    void clip_to_path(const geometry::Path&, FillRule = FillRule::non_zero);
    void set_global_alpha(float);

    // --- Fills and strokes ---
    void fill_rect(geometry::Rect, const Paint&);
    void fill_rounded_rectangle(const geometry::RoundedRectangle&, const Paint&);
    void fill_path(const geometry::Path&, const Paint&, FillRule = FillRule::non_zero);
    void stroke_path(const geometry::Path&, const Paint&, const StrokeStyle&);
    void fill_ellipse_in_rect(geometry::Rect, const Paint&);

    // --- Text (Level 2 owns positioned glyph runs, not string layout) ---
    void draw_glyph_run(const text::GlyphRun&, geometry::Point baseline_origin,
                        const Paint&);
    void draw_paragraph(const text::Paragraph&, geometry::Point origin);

    // --- Images ---
    void draw_image(const Image&, geometry::Rect destination,
                    const ImageSamplingOptions& = {});
    void draw_image_nine_patch(const Image&, geometry::Rect destination,
                               geometry::EdgeInsets caps);

    // --- Effects ---
    void draw_shadow(const geometry::Path&, const Shadow&);
    void begin_filter_layer(const ImageFilter&, geometry::Rect bounds);
    void end_filter_layer();
    void begin_blend_layer(BlendMode, float alpha);
    void end_blend_layer();

    // --- Doorway to Level 1 (P7) ---
    void execute_custom_render_pass(gpu::CustomRenderPass&);

    [[nodiscard]] geometry::Rect current_clip_bounds() const;
    [[nodiscard]] float display_scale_factor() const;
};

// Paint: colors, gradients, shaders, with explicit color-space semantics.
class Paint {
public:
    [[nodiscard]] static Paint solid_color(Color);
    [[nodiscard]] static Paint linear_gradient(geometry::Point start, geometry::Point end,
                                               std::span<const GradientStop>,
                                               GradientInterpolationSpace =
                                                   GradientInterpolationSpace::oklab);
    [[nodiscard]] static Paint radial_gradient(geometry::Point center, float radius,
                                               std::span<const GradientStop>);
    [[nodiscard]] static Paint angular_gradient(geometry::Point center,
                                                float start_angle_radians,
                                                std::span<const GradientStop>);
    [[nodiscard]] static Paint image_pattern(const Image&, TileMode, TileMode);
    [[nodiscard]] static Paint custom_shader(const gpu::ShaderModule&,
                                             std::span<const std::byte> uniforms);

    Paint& with_blend_mode(BlendMode);
    Paint& with_alpha(float);
    Paint& with_dither(bool);
};

// Color is color-space-aware. Defaulting to sRGB while blending in linear space
// is the difference between correct gradients and muddy ones.
class Color {
public:
    [[nodiscard]] static constexpr Color srgb(float r, float g, float b, float a = 1.0f);
    [[nodiscard]] static constexpr Color display_p3(float r, float g, float b, float a = 1.0f);
    [[nodiscard]] static constexpr Color oklch(float l, float c, float h, float a = 1.0f);
    [[nodiscard]] static Color from_hex(uint32_t argb);

    [[nodiscard]] Color converted_to(ColorSpace) const;
    [[nodiscard]] Color with_alpha_multiplied_by(float) const;
    [[nodiscard]] Color blended_with(Color, float fraction,
                                     ColorSpace interpolation = ColorSpace::oklab) const;
};

} // namespace ca::graphics
```

### 3.1 Text (Level 2, `ca::text`)

Apple-quality typography requires these to be design inputs, not options (P10).

```cpp
namespace ca::text {

struct TypographySettings {
    // Fractional advances: glyph positions are NOT rounded to integers. This is
    // the single biggest difference between "renders text" and "renders text
    // beautifully", and it must be true from the first commit.
    bool  uses_fractional_advances     = true;
    bool  uses_optical_kerning         = true;   // GPOS kern feature
    float stem_darkening_amount        = 0.35f;  // compensates linear-space gamma
    bool  uses_optical_sizing          = true;   // opsz variable axis
    float tracking_adjustment          = 0.0f;   // extra letter spacing, points
    HintingStyle hinting_style         = HintingStyle::none;  // fidelity > grid-fit
    SubpixelPositioning subpixel_positioning = SubpixelPositioning::horizontal;
    bool  gamma_correct_blending       = true;   // blend in linear, not sRGB
};

class Font {
public:
    [[nodiscard]] static core::Result<Font> with_descriptor(const FontDescriptor&);
    [[nodiscard]] static Font system_font_of_size(float points, FontWeight = FontWeight::regular);
    [[nodiscard]] static Font monospaced_system_font_of_size(float points);

    [[nodiscard]] FontMetrics metrics() const;      // ascent, descent, leading, x-height, cap-height
    [[nodiscard]] Font with_variation_axes(std::span<const VariationAxisValue>) const;
    [[nodiscard]] Font with_features(std::span<const FontFeature>) const;
    [[nodiscard]] bool has_glyph_for_codepoint(char32_t) const;
};

class ParagraphBuilder {
public:
    ParagraphBuilder& push_attributes(const TextAttributes&);
    ParagraphBuilder& pop_attributes();
    ParagraphBuilder& add_text(std::string_view utf8);
    ParagraphBuilder& add_inline_placeholder(const InlinePlaceholder&);
    [[nodiscard]] core::Result<Paragraph> build_with_constraints(
        float available_width, LineBreakStrategy = LineBreakStrategy::balanced);
};

class Paragraph {
public:
    [[nodiscard]] float measured_width() const;
    [[nodiscard]] float measured_height() const;
    [[nodiscard]] float first_baseline_offset() const;
    [[nodiscard]] size_t line_count() const;
    [[nodiscard]] std::span<const GlyphRun> glyph_runs_for_line(size_t) const;

    // Correct cursor behavior requires affinity and grapheme awareness.
    [[nodiscard]] TextPosition position_for_point(geometry::Point) const;
    [[nodiscard]] geometry::Rect caret_rect_for_position(TextPosition) const;
    [[nodiscard]] std::vector<geometry::Rect> selection_rects_for_range(TextRange) const;
    [[nodiscard]] TextRange word_range_containing(TextPosition) const;
    [[nodiscard]] TextPosition position_moved_by_graphemes(TextPosition, int delta) const;
};

} // namespace ca::text
```

---

## 4. Level 1 — GPU

The deepest public level. Portability guarantees narrow here, which is stated
plainly rather than hidden.

```cpp
namespace ca::gpu {

// Shaders are authored in Calcium's shader IR and cross-compiled by
// calcium-shaderc to MSL / HLSL / SPIR-V / GLSL at build time. This keeps
// Level 1 portable rather than forcing per-backend shader source.
class ShaderModule {
public:
    [[nodiscard]] static core::Result<ShaderModule> from_compiled_ir(
        std::span<const std::byte>);
    [[nodiscard]] std::span<const UniformDescriptor> uniform_descriptors() const;
};

// User-authored render pass. Composites into the layer tree as a first-class
// participant: it receives the real target, the real transform, the real clip.
// Not a "raw handle escape hatch" — a supported extension point (P7).
class CustomRenderPass {
public:
    virtual ~CustomRenderPass() = default;

    virtual void configure_pipeline(PipelineStateBuilder&) = 0;
    virtual void encode_commands(RenderCommandEncoder&, const PassContext&) = 0;

    // Declares what the compositor must know to schedule and cull this pass.
    [[nodiscard]] virtual geometry::Rect content_bounds() const = 0;
    [[nodiscard]] virtual bool is_opaque() const { return false; }
};

struct PassContext {
    geometry::Transform3D model_to_device_transform;  // presentation-resolved
    geometry::Rect        clip_bounds_in_device_space;
    core::Timestamp       presentation_timestamp;     // the *scanout* time
    float                 display_scale_factor;
    ColorSpace            target_color_space;
};

class RenderCommandEncoder {
public:
    void set_pipeline_state(const PipelineState&);
    void set_vertex_buffer(const Buffer&, size_t offset, uint32_t slot);
    void set_uniform_data(std::span<const std::byte>, uint32_t slot);
    void set_texture(const Texture&, uint32_t slot);
    void set_sampler(const Sampler&, uint32_t slot);
    void draw_indexed(uint32_t index_count, uint32_t instance_count = 1);
    void draw(uint32_t vertex_count, uint32_t instance_count = 1);
};

} // namespace ca::gpu
```

A custom pass reaches the tree through Level 3 without any special API:

```cpp
class WaveformRenderPass : public ca::gpu::CustomRenderPass { /* … */ };

class WaveformLayerDelegate : public ca::layer::LayerDelegate {
    void draw_layer_content(const layer::Layer& l, gfx::DrawingContext& ctx) override {
        ctx.fill_rect(l.bounds(), gfx::Paint::solid_color(background_));
        ctx.execute_custom_render_pass(waveform_pass_);   // Level 3 → 2 → 1
    }
};
```

---

## 5. Level 4 — widgets

```cpp
namespace ca::widget {

class Button : public view::View {
public:
    [[nodiscard]] static std::unique_ptr<Button> with_title(std::string_view);
    [[nodiscard]] static std::unique_ptr<Button> with_content(std::unique_ptr<View>);

    void set_action(std::function<void()>);
    void set_style(const ButtonStyle&);
    void set_enabled(bool);
    [[nodiscard]] ControlState control_state() const;

    // Physics-native by default: press scales down with a spring, release
    // springs back with preserved velocity. No configuration needed to feel right.
    void set_press_motion(const animation::Motion&);
    void set_press_scale(float);
};

class ScrollView : public view::View {
public:
    void set_content_view(std::unique_ptr<View>);
    void set_content_size(geometry::Size);

    // Twell decay + rubber banding, matching iOS characteristics.
    void set_deceleration_rate(double);              // 0.998 normal, 0.99 fast
    void set_rubber_band_tension(double);            // asymptotic overscroll
    void set_bounces(bool);
    void set_scroll_indicator_style(ScrollIndicatorStyle);
    void set_paging_behavior(PagingBehavior);
    void set_snap_positions(std::span<const float>);

    void scroll_to_offset(geometry::Point, const animation::Motion&);
    void scroll_rect_to_visible(geometry::Rect, const animation::Motion&);

    // Scroll offset is a real animatable property, so PropertyLink can drive
    // parallax, collapsing headers, and blur ramps with no per-frame code.
    [[nodiscard]] const animation::AnimatablePoint& content_offset() const;
};

class ListView : public view::View {
public:
    struct DataSource {
        std::function<size_t()> item_count;
        std::function<std::unique_ptr<View>(size_t)> view_for_item;
        std::function<float(size_t)> estimated_height_for_item;
        // P12: stable identity so reordering preserves animations and a11y focus.
        std::function<core::Identifier(size_t)> identifier_for_item;
    };
    void set_data_source(DataSource);
    void reload_data();

    // Diff-based updates: inserts/removes/moves animate correctly because
    // identity is stable across the change.
    void apply_changes(const CollectionChangeSet&, const animation::Motion&);
};

// P9: motion is part of the theme, alongside color and typography.
struct Theme {
    ColorScheme            colors;
    TypographyScale        typography;
    animation::MotionScheme motion;
    SpacingScale           spacing;
    geometry::CornerCurve  default_corner_curve = geometry::CornerCurve::continuous;

    [[nodiscard]] static Theme platform_default();     // adapts per OS
    [[nodiscard]] static Theme high_contrast_variant_of(const Theme&);
    [[nodiscard]] static Theme reduced_motion_variant_of(const Theme&);
};

} // namespace ca::widget
```

---

## 6. Level 5 — declarative

A reconciler over Level 3/4. Uses only public APIs (P4), so dropping a level is
always available.

```cpp
namespace ca::compose {

// Observable state. Mutation marks the owning subtree for rebuild.
template <typename ValueType>
class State {
public:
    explicit State(ValueType initial);
    [[nodiscard]] const ValueType& get() const;
    void set(ValueType);
    void update(std::function<void(ValueType&)>);
    [[nodiscard]] Binding<ValueType> binding();
};

class Element {
public:
    // Chainable modifiers, SwiftUI-shaped.
    Element& padding(layout::EdgeInsets);
    Element& frame(std::optional<float> width, std::optional<float> height);
    Element& background(graphics::Paint);
    Element& corner_radius(float, geometry::CornerCurve = geometry::CornerCurve::continuous);
    Element& opacity(float);
    Element& scale(float);
    Element& offset(geometry::Point);
    Element& shadow(const graphics::Shadow&);
    Element& clipped();

    // Motion. `animation()` makes every dependent property change spring.
    Element& animation(const animation::Motion&);
    Element& transition(const Transition&);
    Element& matched_geometry(core::Identifier);   // shared-element transitions

    // Input
    Element& on_tap(std::function<void()>);
    Element& on_drag(std::function<void(const DragValue&)>);
    Element& on_hover(std::function<void(bool)>);

    // Semantics
    Element& accessibility_label(std::string_view);
    Element& accessibility_role(accessibility::Role);

    // P12: explicit identity for dynamic collections.
    Element& identified_by(core::Identifier);

    // P7: descend to Level 3 without leaving the framework.
    Element& configure_backing_view(std::function<void(view::View&)>);
    Element& configure_backing_layer(std::function<void(layer::Layer&)>);
};

// Builders
[[nodiscard]] Element text(std::string_view);
[[nodiscard]] Element image(const graphics::Image&);
[[nodiscard]] Element spacer();
[[nodiscard]] Element vertical_stack(std::vector<Element>);
[[nodiscard]] Element horizontal_stack(std::vector<Element>);
[[nodiscard]] Element z_stack(std::vector<Element>);
[[nodiscard]] Element scroll_view(Element content);
[[nodiscard]] Element lazy_vertical_stack(size_t count,
                                          std::function<Element(size_t)> builder);
[[nodiscard]] Element canvas(std::function<void(graphics::DrawingContext&,
                                                geometry::Size)>);  // → Level 2

// A component: derive and implement body().
class Component {
public:
    virtual ~Component() = default;
    [[nodiscard]] virtual Element body(BuildContext&) = 0;
};

} // namespace ca::compose
```

Full declarative example:

```cpp
class CounterComponent : public ca::compose::Component {
public:
    Element body(BuildContext&) override {
        return vertical_stack({
            text(std::format("Count: {}", count_.get()))
                .font(text::Font::system_font_of_size(34.0f, FontWeight::bold))
                .animation(anim::Motion::snappy()),

            horizontal_stack({
                button("Decrement", [this] { count_.update([](int& c) { --c; }); }),
                button("Increment", [this] { count_.update([](int& c) { ++c; }); }),
            }).spacing(12.0f),
        })
        .padding(layout::EdgeInsets::all(24.0f))
        .animation(anim::Motion::standard());
    }
private:
    compose::State<int> count_{0};
};

int main() {
    auto app = ca::platform::Application::create({.name = "Counter"});
    auto window = app->create_window({.title = "Counter", .size = {480, 320}});
    window->set_root_component(std::make_unique<CounterComponent>());
    return app->run();
}
```

---

## 7. The C ABI (P13)

Hand-designed, opaque handles, generation-checked, versioned.

```c
#ifndef CALCIUM_H
#define CALCIUM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CA_VERSION_MAJOR 0
#define CA_VERSION_MINOR 1

/* Opaque, generation-checked handles. A stale handle is reported, not crashed. */
typedef struct { uint64_t bits; } ca_layer_t;
typedef struct { uint64_t bits; } ca_view_t;
typedef struct { uint64_t bits; } ca_property_t;
typedef struct { uint64_t bits; } ca_window_t;
typedef struct { uint64_t bits; } ca_application_t;
typedef struct { uint64_t bits; } ca_display_list_t;
typedef struct { uint64_t bits; } ca_font_t;
typedef struct { uint64_t bits; } ca_paragraph_t;

typedef enum {
    CA_SUCCESS = 0,
    CA_ERROR_INVALID_HANDLE,
    CA_ERROR_INVALID_ARGUMENT,
    CA_ERROR_OUT_OF_MEMORY,
    CA_ERROR_BACKEND_FAILURE,
    CA_ERROR_WRONG_THREAD,
    CA_ERROR_UNSUPPORTED
} ca_result_t;

/* Thread-local detail for the last failure. Never returns NULL. */
const char* ca_last_error_message(void);

typedef struct { float x, y; } ca_point_t;
typedef struct { float width, height; } ca_size_t;
typedef struct { ca_point_t origin; ca_size_t size; } ca_rect_t;
typedef struct { double mass, stiffness, damping, initial_velocity; } ca_spring_config_t;

/* --- Application --- */
typedef struct {
    const char* name;
    const char* bundle_identifier;
    uint32_t    max_animated_properties;   /* sizes the Twell arena */
    uint32_t    max_concurrent_gestures;
} ca_application_config_t;

ca_result_t ca_application_create(const ca_application_config_t*, ca_application_t* out);
ca_result_t ca_application_run(ca_application_t);
ca_result_t ca_application_destroy(ca_application_t);

/* --- Layers --- */
ca_result_t ca_layer_create(ca_layer_t* out);
ca_result_t ca_layer_add_sublayer(ca_layer_t parent, ca_layer_t child);
ca_result_t ca_layer_set_bounds(ca_layer_t, ca_rect_t);
ca_result_t ca_layer_set_background_color(ca_layer_t, float r, float g, float b, float a);

/* Animatable properties are handles, so bindings can wrap them naturally. */
ca_result_t ca_layer_get_position_property(ca_layer_t, ca_property_t* out);
ca_result_t ca_layer_get_opacity_property(ca_layer_t, ca_property_t* out);

/* --- Animation --- */
ca_result_t ca_property_get_model_value(ca_property_t, double* out);
ca_result_t ca_property_get_presentation_value(ca_property_t, double* out);
ca_result_t ca_property_animate_to(ca_property_t, double target, ca_spring_config_t);
ca_result_t ca_property_animate_to_2d(ca_property_t, double tx, double ty, ca_spring_config_t);
ca_result_t ca_property_set_immediate(ca_property_t, double value);

ca_result_t ca_transaction_begin(void);
ca_result_t ca_transaction_set_default_spring(ca_spring_config_t);
ca_result_t ca_transaction_commit(void);

/* --- Callbacks always carry user_data --- */
typedef void (*ca_action_callback_t)(void* user_data);
typedef void (*ca_draw_callback_t)(ca_display_list_t recorder, ca_rect_t bounds,
                                   void* user_data);

ca_result_t ca_layer_set_draw_callback(ca_layer_t, ca_draw_callback_t, void* user_data);

#endif /* CALCIUM_H */
```

### 7.1 Binding ergonomics

`calcium.h` is designed so generated bindings read naturally, because handles map
onto objects and properties map onto native property syntax:

```python
# Python — properties become descriptors
card = ca.Layer()
card.bounds = ca.Rect(0, 0, 200, 120)
with ca.animate(ca.Motion.standard()):
    card.position = (400, 200)
    card.opacity = 1.0
```

```rust
// Rust — RAII + Result
let card = ca::Layer::new()?;
card.set_bounds(ca::Rect::new(0.0, 0.0, 200.0, 120.0))?;
ca::animate(ca::Motion::standard(), || {
    card.position().set([400.0, 200.0]);
    card.opacity().set(1.0);
})?;
```

```csharp
// C# — IDisposable + properties
using var card = new Calcium.Layer();
card.Bounds = new Rect(0, 0, 200, 120);
using (Calcium.Animate(Motion.Standard)) {
    card.Position = new Point(400, 200);
    card.Opacity = 1.0f;
}
```
