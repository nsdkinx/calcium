#pragma once

// Interned strings.
//
// String comparison is one of the quiet taxes of a UI framework: theme names,
// accessibility labels, font families and bundle identifiers are compared on
// hot paths, and strcmp against variable-length storage adds cache misses at
// the worst possible moments. InternedString replaces the comparison with a
// single `uint32_t` equality — the interner guarantees one canonical entry per
// unique text, so equal ids mean equal text and vice versa.
//
// The registry is internally synchronized and safe from any thread: FontManager
// interning from a worker while the UI thread interns a theme name is normal
// usage (docs/02-architecture.md §2.3).
//
// Storage is owned by the registry and is stable forever — `view()` remains
// valid for the lifetime of the process. The registry only grows; a UI
// framework's identifier set is small and bounded in practice, and recycling
// strings adds synchronization that the steady-state frame path must not pay.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ca::core {

/// A canonicalized string: equal ids imply equal text.
class InternedString {
public:
    /// The empty string. Interning an empty string also yields id 0.
    constexpr InternedString() noexcept = default;

    /// Interns `text`. A given text always maps to the same id.
    /// The empty string is not stored; it maps to id 0.
    explicit InternedString(std::string_view text) noexcept;

    [[nodiscard]] constexpr bool is_empty() const noexcept { return id_ == 0; }

    /// The registry id; 0 for the empty string. Not meaningful across processes.
    [[nodiscard]] constexpr std::uint32_t id() const noexcept { return id_; }

    /// The interned text. Stable for the lifetime of the process.
    [[nodiscard]] std::string_view view() const noexcept;
    [[nodiscard]] const char* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return view().size(); }

    /// Id comparison: two InternedStrings are equal iff their text is equal.
    [[nodiscard]] constexpr bool operator==(const InternedString& other) const noexcept {
        return id_ == other.id_;
    }
    [[nodiscard]] constexpr bool operator!=(const InternedString& other) const noexcept {
        return id_ != other.id_;
    }

    [[nodiscard]] constexpr auto operator<=>(const InternedString& other) const noexcept {
        return id_ <=> other.id_;
    }

private:
    std::uint32_t id_ = 0;
};

} // namespace ca::core

namespace std {
template <>
struct hash<ca::core::InternedString> {
    [[nodiscard]] size_t operator()(const ca::core::InternedString& s) const noexcept {
        return hash<uint32_t>{}(s.id());
    }
};
} // namespace std
