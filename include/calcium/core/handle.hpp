#pragma once

// Typed, generation-checked handles.
//
// Calcium's tree nodes live in pooled structure-of-arrays storage, not as
// individually heap-allocated objects (docs/02-architecture.md §4.1). Handles
// are how the public API refers to them.
//
// The generation counter is the important part: when a slot is freed its
// generation increments, so a handle held past its object's lifetime compares
// unequal to the live slot. A stale handle is therefore *detected*, which turns
// a use-after-free into a diagnosable error instead of memory corruption. This
// check is retained in release builds; it costs one comparison.

#include <cstdint>
#include <functional>
#include <vector>

namespace ca::core {

/// A generation-checked reference to a pooled object.
///
/// `Tag` is an incomplete tag type that makes handles to different pools
/// distinct at compile time, so a `Handle<LayerTag>` cannot be passed where a
/// `Handle<ViewTag>` is expected.
template <typename Tag>
class Handle {
public:
    using IndexType = std::uint32_t;
    using GenerationType = std::uint32_t;

    static constexpr IndexType invalid_index = static_cast<IndexType>(-1);

    constexpr Handle() noexcept = default;

    constexpr Handle(IndexType index, GenerationType generation) noexcept
        : index_(index), generation_(generation) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index_ != invalid_index;
    }

    [[nodiscard]] constexpr IndexType index() const noexcept { return index_; }

    [[nodiscard]] constexpr GenerationType generation() const noexcept {
        return generation_;
    }

    /// Packs into a single 64-bit value for the C ABI (docs/04-public-api.md §7).
    [[nodiscard]] constexpr std::uint64_t to_bits() const noexcept {
        return (static_cast<std::uint64_t>(generation_) << 32)
             | static_cast<std::uint64_t>(index_);
    }

    [[nodiscard]] static constexpr Handle from_bits(std::uint64_t bits) noexcept {
        return Handle{static_cast<IndexType>(bits & 0xFFFF'FFFFu),
                      static_cast<GenerationType>(bits >> 32)};
    }

    [[nodiscard]] constexpr bool operator==(const Handle&) const noexcept = default;
    [[nodiscard]] constexpr bool operator!=(const Handle&) const noexcept = default;

    explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    IndexType index_ = invalid_index;
    GenerationType generation_ = 0;
};

/// Slot bookkeeping shared by every pool: a free list threaded through the
/// vacant slots plus a per-slot generation counter.
///
/// Kept separate from the payload arrays so pools stay structure-of-arrays: the
/// compositor's transform-resolution loop touches only the arrays it needs.
template <typename Tag>
class HandlePool {
public:
    using HandleType = Handle<Tag>;
    using IndexType = typename HandleType::IndexType;
    using GenerationType = typename HandleType::GenerationType;

    /// Number of slots ever allocated, live or free. Pool payload arrays are
    /// sized to this.
    [[nodiscard]] std::uint32_t slot_count() const noexcept {
        return static_cast<std::uint32_t>(generations_.size());
    }

    [[nodiscard]] std::uint32_t live_count() const noexcept { return live_count_; }

    [[nodiscard]] bool is_valid(HandleType handle) const noexcept {
        if (!handle.is_valid() || handle.index() >= slot_count()) {
            return false;
        }
        return occupied_[handle.index()]
            && generations_[handle.index()] == handle.generation();
    }

    /// Reserves a slot. `out_is_new_slot` reports whether payload arrays must
    /// grow, so callers can push_back exactly once.
    [[nodiscard]] HandleType acquire(bool& out_is_new_slot) {
        ++live_count_;
        if (free_list_head_ != HandleType::invalid_index) {
            const IndexType index = free_list_head_;
            free_list_head_ = next_free_[index];
            occupied_[index] = true;
            out_is_new_slot = false;
            return HandleType{index, generations_[index]};
        }
        const auto index = static_cast<IndexType>(generations_.size());
        generations_.push_back(1);
        occupied_.push_back(true);
        next_free_.push_back(HandleType::invalid_index);
        out_is_new_slot = true;
        return HandleType{index, 1};
    }

    /// Releases a slot and bumps its generation so existing handles go stale.
    /// Returns false if the handle was already invalid (double free).
    [[nodiscard]] bool release(HandleType handle) noexcept {
        if (!is_valid(handle)) {
            return false;
        }
        const IndexType index = handle.index();
        occupied_[index] = false;
        // Skip generation 0 on wrap so a zero-initialised handle never matches.
        generations_[index] = generations_[index] == 0xFFFF'FFFFu
                                  ? 1u
                                  : generations_[index] + 1u;
        next_free_[index] = free_list_head_;
        free_list_head_ = index;
        --live_count_;
        return true;
    }

    void clear() noexcept {
        generations_.clear();
        occupied_.clear();
        next_free_.clear();
        free_list_head_ = HandleType::invalid_index;
        live_count_ = 0;
    }

private:
    std::vector<GenerationType> generations_;
    std::vector<bool> occupied_;
    std::vector<IndexType> next_free_;
    IndexType free_list_head_ = HandleType::invalid_index;
    std::uint32_t live_count_ = 0;
};

} // namespace ca::core

namespace std {
template <typename Tag>
struct hash<ca::core::Handle<Tag>> {
    [[nodiscard]] size_t operator()(
        const ca::core::Handle<Tag>& handle) const noexcept {
        return hash<uint64_t>{}(handle.to_bits());
    }
};
} // namespace std
