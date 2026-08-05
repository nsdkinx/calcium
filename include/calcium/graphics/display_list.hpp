#pragma once

// Display lists: the Level-2 contract the rasterizer consumes
// (docs/02-architecture.md §6.1).
//
// A DisplayList is an immutable, sealed buffer of tagged records plus an
// interned paint table. It is recorded on the UI thread (via
// DisplayListRecorder, which implements DrawingContext — the Level-3 doorway
// into Level 2), sealed once, and read on the compositor thread. Nothing
// about a sealed list ever changes (docs/02 §2.3); copying one is a
// shared-ownership no-op.
//
// M2 record set — the full set (clip paths, strokes, glyph runs, images,
// filters) lands with the display-list spec and its conformance suite:
//
//   save_state / restore_state     push/pop the CTM and clip state
//   concat_transform               compose the CTM
//   clip_rect                      intersect the clip (device-converted)
//   fill_rect                      solid fill of a rect
//   fill_rounded_rectangle         solid fill of a rounded rect
//
// Record encoding (the format IS the contract — backends parse it):
//   u8 command, u8 paint index (0xFF = none), u16 payload byte count,
//   payload. Payloads are byte-packed; readers copy fields with memcpy.
//   Payloads: AffineTransform (24), Rect (16), RoundedRectangle (36).
//
// `set_global_alpha` does not appear in the record set: the recorder folds
// it into subsequently recorded paints, so the rasterizer's state is exactly
// CTM + clip.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "calcium/geometry/affine_transform.hpp"
#include "calcium/geometry/rect.hpp"
#include "calcium/geometry/rounded_rectangle.hpp"
#include "calcium/graphics/color.hpp"
#include "calcium/graphics/paint.hpp"

namespace ca::graphics {

class DisplayListRecorder;

/// The record set (docs/02-architecture.md §6.1 — the M2 subset).
enum class Command : std::uint8_t {
    save_state,              ///< no payload
    restore_state,           ///< no payload
    concat_transform,        ///< payload: geometry::AffineTransform
    clip_rect,               ///< payload: geometry::Rect
    fill_rect,               ///< payload: geometry::Rect
    fill_rounded_rectangle,  ///< payload: geometry::RoundedRectangle
};

/// A sealed, immutable display list. Copyable; never mutable after `seal`.
class DisplayList {
public:
    DisplayList() = default;

    [[nodiscard]] bool is_empty() const noexcept { return record_count() == 0; }

    [[nodiscard]] std::size_t record_count() const noexcept;
    [[nodiscard]] std::size_t paint_count() const noexcept;

    /// The interned paint table (deduplicated at record time).
    [[nodiscard]] Color paint_at(std::size_t index) const noexcept;

    /// The i-th record (records are sequential; a record's paint index is
    /// `paint_index_at(i)` — 0xFF when it has none).
    [[nodiscard]] Command command_at(std::size_t index) const noexcept;
    [[nodiscard]] std::uint8_t paint_index_at(std::size_t index) const noexcept;
    /// The record's raw payload (byte-packed; see the file comment).
    [[nodiscard]] std::span<const std::byte> payload_at(
        std::size_t index) const noexcept;

    /// The raw record buffer. The rasterizer walks it sequentially (the
    /// indexed accessors above re-scan from the start — fine for tests and
    /// the conformance suite, wrong for the frame path).
    [[nodiscard]] std::span<const std::byte> raw_records() const noexcept;

    /// Deep equality (the underlying buffers compare equal, not just the
    /// shared handles).
    [[nodiscard]] bool operator==(const DisplayList& other) const noexcept;

private:
    friend class DisplayListRecorder;
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

/// The Level-3 → Level-2 drawing interface (docs/04-public-api.md §3, P7).
/// The M2 subset: the state stack, clips, and solid fills. Text, paths,
/// images, filters, and custom passes land with their milestones; the
/// signatures below are stable.
class DrawingContext {
public:
    virtual ~DrawingContext() = default;

    virtual void save_state() = 0;
    virtual void restore_state() = 0;
    virtual void concatenate_transform(const geometry::AffineTransform&) = 0;
    virtual void clip_to_rect(geometry::Rect) = 0;
    virtual void fill_rect(geometry::Rect, const Paint&) = 0;
    virtual void fill_rounded_rectangle(const geometry::RoundedRectangle&,
                                        const Paint&) = 0;
    /// Multiplies into the alpha of subsequently recorded paints.
    virtual void set_global_alpha(float) = 0;
};

/// Records drawing commands into a sealed DisplayList. UI-thread object:
/// recording allocates (the seal is a copy), so the compositor never touches
/// one (docs/02 §2.3).
class DisplayListRecorder final : public DrawingContext {
public:
    DisplayListRecorder() = default;
    ~DisplayListRecorder() override = default;

    // DrawingContext
    void save_state() override;
    void restore_state() override;
    void concatenate_transform(
        const geometry::AffineTransform& transform) override;
    void clip_to_rect(geometry::Rect rect) override;
    void fill_rect(geometry::Rect rect, const Paint& paint) override;
    void fill_rounded_rectangle(
        const geometry::RoundedRectangle& rounded_rectangle,
        const Paint& paint) override;
    void set_global_alpha(float alpha) override;

    /// Seals the recording: the returned list is immutable, and the recorder
    /// resets so it can record the next list.
    [[nodiscard]] DisplayList seal();

private:
    void append(Command command, std::uint8_t paint_index,
                std::span<const std::byte> payload);
    [[nodiscard]] std::uint8_t intern_paint(Color color);

    std::vector<std::byte> records_;
    std::vector<Color> paints_;
    float global_alpha_ = 1.0f;
    std::vector<float> alpha_stack_;  // save_state / restore_state
};

} // namespace ca::graphics
