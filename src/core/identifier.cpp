#include "calcium/core/identifier.hpp"

#include <atomic>

namespace ca::core {

Identifier Identifier::generate() noexcept {
    // Starts at 1: 0 is reserved for the invalid identifier, and a
    // zero-initialised Identifier must never collide with a real one. Wrap
    // skips back to 1 rather than 0, matching HandlePool's generation rule.
    static std::atomic<std::uint64_t> next_value{1};
    const std::uint64_t value =
        next_value.fetch_add(1, std::memory_order_relaxed);
    return Identifier{value == 0 ? 1 : value};
}

} // namespace ca::core
