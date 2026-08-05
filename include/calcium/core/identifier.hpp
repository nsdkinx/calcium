#pragma once

// Stable identity (P12).
//
// ListView recycling, matched-geometry transitions, and accessibility focus
// all require an identity that survives structural churn: an item that moves
// from row 5 to row 2 must be the *same node* before and after, so its
// animation state and accessibility focus follow it. A re-generated identity
// on every rebuild silently breaks that contract in ways that are hard to
// reproduce.
//
// Identifier is a dense, process-unique 64-bit value, cheap to compare and
// hash, and trivially portable to the C ABI as an integer. Values are
// allocated from a monotonic counter, so two identifiers from the same process
// are never equal even if a caller forgets to copy the original.

#include <cstdint>
#include <functional>

namespace ca::core {

/// A process-unique stable identity.
class Identifier {
public:
    constexpr Identifier() noexcept = default;

    /// Allocates the next unused identifier. Thread-safe; never returns 0
    /// (which is reserved for the invalid identifier).
    [[nodiscard]] static Identifier generate() noexcept;

    /// Round-trips through the C ABI (`uint64_t`), like `Handle::to_bits`.
    [[nodiscard]] constexpr std::uint64_t to_bits() const noexcept {
        return value_;
    }
    [[nodiscard]] static constexpr Identifier from_bits(std::uint64_t bits) noexcept {
        return Identifier{bits};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }

    [[nodiscard]] constexpr auto operator<=>(const Identifier&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Identifier&) const noexcept = default;

private:
    constexpr explicit Identifier(std::uint64_t value) noexcept : value_(value) {}
    std::uint64_t value_ = 0;
};

} // namespace ca::core

namespace std {
template <>
struct hash<ca::core::Identifier> {
    [[nodiscard]] size_t operator()(const ca::core::Identifier& id) const noexcept {
        return hash<uint64_t>{}(id.to_bits());
    }
};
} // namespace std
