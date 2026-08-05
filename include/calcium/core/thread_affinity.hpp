#pragma once

// Thread affinity enforcement.
//
// Calcium's threading contract (docs/02-architecture.md §2.3) is the load-bearing
// element of the smoothness guarantee: the compositor owns the Twell context and
// the UI thread never touches it. Violations of that contract produce tearing and
// data races that reproduce only under load, on someone else's machine.
//
// So every public entry point asserts its expected thread in debug builds. This
// converts an entire class of Heisenbugs into a hard failure at the moment of the
// mistake. The assertions compile to nothing in release.

#include <cstdint>

#include "calcium/core/export.hpp"

#if defined(CALCIUM_THREAD_ASSERTIONS_ENABLED)
#  include <source_location>
#endif

namespace ca::core {

enum class ThreadRole : std::uint8_t {
    unregistered = 0,
    platform,    ///< OS main thread. Event pump, window lifecycle, IME, a11y.
    ui,          ///< Owns the model tree. All application code runs here.
    compositor,  ///< Owns the Twell context. Ticks animation, submits GPU work.
    worker,      ///< Pool: glyph raster, image decode, tessellation, measurement.
};

const char* describe(ThreadRole role) noexcept;

/// Registers the calling thread's role. Called once during startup per thread.
void register_current_thread_role(ThreadRole role) noexcept;

ThreadRole current_thread_role() noexcept;

bool current_thread_has_role(ThreadRole role) noexcept;

#if defined(CALCIUM_THREAD_ASSERTIONS_ENABLED)

/// Reports a violation and aborts. Never returns.
void report_thread_affinity_violation(
    ThreadRole expected, ThreadRole actual,
    const std::source_location& location) noexcept;

inline void assert_thread_role(
    ThreadRole expected,
    const std::source_location& location = std::source_location::current()) noexcept {
    const ThreadRole actual = current_thread_role();
    if (actual != expected) {
        report_thread_affinity_violation(expected, actual, location);
    }
}

#define CA_ASSERT_UI_THREAD()                                                  \
    ::ca::core::assert_thread_role(::ca::core::ThreadRole::ui)
#define CA_ASSERT_COMPOSITOR_THREAD()                                          \
    ::ca::core::assert_thread_role(::ca::core::ThreadRole::compositor)
#define CA_ASSERT_PLATFORM_THREAD()                                            \
    ::ca::core::assert_thread_role(::ca::core::ThreadRole::platform)

#else

#define CA_ASSERT_UI_THREAD()         ((void)0)
#define CA_ASSERT_COMPOSITOR_THREAD() ((void)0)
#define CA_ASSERT_PLATFORM_THREAD()   ((void)0)

#endif // CALCIUM_THREAD_ASSERTIONS_ENABLED

} // namespace ca::core
