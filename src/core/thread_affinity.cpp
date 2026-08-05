#include "calcium/core/thread_affinity.hpp"

#include <cstdio>
#include <cstdlib>

namespace ca::core {
namespace {

// Thread-local rather than a registry keyed by thread id: the assertion sits on
// every public entry point, so the lookup must be a single TLS read.
thread_local ThreadRole current_role = ThreadRole::unregistered;

} // namespace

const char* describe(ThreadRole role) noexcept {
    switch (role) {
    case ThreadRole::unregistered: return "unregistered";
    case ThreadRole::platform:     return "platform";
    case ThreadRole::ui:           return "ui";
    case ThreadRole::compositor:   return "compositor";
    case ThreadRole::worker:       return "worker";
    }
    return "unknown";
}

void register_current_thread_role(ThreadRole role) noexcept {
    current_role = role;
}

ThreadRole current_thread_role() noexcept {
    return current_role;
}

bool current_thread_has_role(ThreadRole role) noexcept {
    return current_role == role;
}

#if defined(CALCIUM_THREAD_ASSERTIONS_ENABLED)

void report_thread_affinity_violation(
    ThreadRole expected, ThreadRole actual,
    const std::source_location& location) noexcept {
    std::fprintf(
        stderr,
        "\n"
        "==== Calcium thread affinity violation ====\n"
        "  Expected thread : %s\n"
        "  Actual thread   : %s\n"
        "  Location        : %s:%u\n"
        "  Function        : %s\n"
        "\n"
        "Calcium's threading contract is load-bearing for animation smoothness:\n"
        "the compositor owns the Twell context and the UI thread never touches it.\n"
        "See docs/02-architecture.md section 2.3.\n"
        "===========================================\n\n",
        describe(expected), describe(actual),
        location.file_name(), location.line(), location.function_name());
    std::fflush(stderr);
    std::abort();
}

#endif // CALCIUM_THREAD_ASSERTIONS_ENABLED

} // namespace ca::core
