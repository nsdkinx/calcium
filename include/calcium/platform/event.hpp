#pragma once

// Platform input events.
//
// The platform thread normalizes OS events into these types and posts them to
// the UI thread's queue (docs/02-architecture.md §2). They are the *only* way
// input enters the framework, so they carry full device provenance (P14): a
// touch, a pen, and a mouse are different devices with different semantics,
// and a consumer that does not care can ignore the distinction cheaply.
//
// All event structs are fixed-size and trivially copyable so they travel
// through the lock-free event ring without allocation (P8). Timestamps are
// from the platform's monotonic clock — the same timeline Twell animates on.

#include <cstdint>
#include <variant>

#include "calcium/core/time.hpp"
#include "calcium/geometry/point.hpp"

namespace ca::platform {

// --- Pointers (mouse, touch, pen) -----------------------------------------

/// The device class that produced a pointer event. `eraser` is the pen's
/// eraser end; some devices (older Wacom) report it as a distinct pointer id.
enum class PointerType : std::uint8_t {
    mouse,
    touch,
    pen,
    eraser,
};

/// The lifecycle of a pointer on the surface.
enum class PointerEventKind : std::uint8_t {
    down,     ///< the pointer pressed or touched down
    move,     ///< the pointer moved while in contact (or hovering)
    up,       ///< the pointer released
    enter,    ///< the pointer entered the surface (hovering)
    leave,    ///< the pointer left the surface
    cancel,   ///< the platform cancelled the gesture (system gesture, scroll
              ///< capture): treat as up, then ignore the device until down
};

struct PointerEvent {
    PointerEventKind kind = PointerEventKind::move;
    PointerType pointer_type = PointerType::mouse;

    /// Stable per-device identity while the device is in contact. Multiple
    /// simultaneous touches have distinct ids; a pen and a finger never share
    /// one. The id is valid for the lifetime of a contact and may be reused
    /// by the platform after a full release.
    std::uint32_t pointer_id = 0;

    /// Position in points, in the window's content coordinate space.
    geometry::Point position;

    /// Bit 0 = primary (left / touch / pen tip), bit 1 = secondary (right),
    /// bit 2 = middle; bits 3+ are auxiliary buttons.
    std::uint32_t button_mask = 0;

    /// 0..1 pressure; 0 for devices without pressure (mouse).
    float pressure = 0.0f;

    /// Pen tilt in radians, (x, y); 0 for non-pen devices.
    geometry::Point tilt;

    /// Monotonic timestamp of the event at the platform layer.
    core::Timestamp timestamp;
};

// --- Keys ----------------------------------------------------------------

enum class KeyAction : std::uint8_t { down, up, repeat };

/// A key event. `keycode` is the logical key (layout-independent); `scancode`
/// is the physical key position — use one or the other, never both.
struct KeyEvent {
    KeyAction action = KeyAction::down;
    std::uint32_t keycode = 0;    ///< logical key (e.g. 'a', 0x1B for escape)
    std::uint32_t scancode = 0;   ///< physical key position
    bool is_auto_repeat = false;  ///< true for repeat events from key hold
    core::Timestamp timestamp;
};

// --- Scroll ---------------------------------------------------------------

/// `precise` is a trackpad or a mouse wheel in high-resolution mode: the delta
/// is in points and may be sub-pixel. `coarse` is a discrete wheel notch: the
/// delta is in notches and should be scaled by the consumer's tick size.
enum class ScrollKind : std::uint8_t { precise, coarse };

/// Trackpads report momentum phases; `none` for everything else.
enum class ScrollMomentumPhase : std::uint8_t {
    none,
    began,     ///< momentum started after finger lift
    changed,   ///< momentum continuing
    ended,     ///< momentum finished
    cancelled,
};

struct ScrollEvent {
    geometry::Point delta;    ///< points (precise) or notches (coarse)
    ScrollKind kind = ScrollKind::precise;
    ScrollMomentumPhase momentum_phase = ScrollMomentumPhase::none;
    /// True when the platform inverts the direction (natural scrolling) and
    /// the delta is reported in "content moves with fingers" terms.
    bool is_direction_inverted = false;
    core::Timestamp timestamp;
};

// --- System ---------------------------------------------------------------

enum class SystemEventKind : std::uint8_t {
    quit_requested,        ///< the user asked the application to quit
    window_close_requested,///< a window's close button was pressed
    window_gained_focus,
    window_lost_focus,
    window_resized,        ///< `size` is the new content size in points
    display_scale_changed,
};

struct SystemEvent {
    SystemEventKind kind = SystemEventKind::quit_requested;
    std::uint64_t window_id = 0;
    geometry::Point size;  ///< for window_resized: the new content size
    core::Timestamp timestamp;
};

// --- The union ------------------------------------------------------------

/// Every event the framework understands. Fixed-size and trivially copyable
/// (all alternatives are), so events travel through the lock-free ring.
struct Event {
    std::variant<std::monostate, PointerEvent, KeyEvent, ScrollEvent,
                 SystemEvent>
        payload;

    [[nodiscard]] bool is_pointer() const noexcept {
        return std::holds_alternative<PointerEvent>(payload);
    }
    [[nodiscard]] bool is_key() const noexcept {
        return std::holds_alternative<KeyEvent>(payload);
    }
    [[nodiscard]] bool is_scroll() const noexcept {
        return std::holds_alternative<ScrollEvent>(payload);
    }
    [[nodiscard]] bool is_system() const noexcept {
        return std::holds_alternative<SystemEvent>(payload);
    }

    [[nodiscard]] const PointerEvent& as_pointer() const noexcept {
        return std::get<PointerEvent>(payload);
    }
    [[nodiscard]] const KeyEvent& as_key() const noexcept {
        return std::get<KeyEvent>(payload);
    }
    [[nodiscard]] const ScrollEvent& as_scroll() const noexcept {
        return std::get<ScrollEvent>(payload);
    }
    [[nodiscard]] const SystemEvent& as_system() const noexcept {
        return std::get<SystemEvent>(payload);
    }
};

} // namespace ca::platform
