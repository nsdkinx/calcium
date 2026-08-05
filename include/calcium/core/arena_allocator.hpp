#pragma once

// Arena (bump) allocation.
//
// P8 requires zero heap allocation in the steady-state frame path. Allocator
// behaviour is the most common source of frame-time outliers and the least
// consistent thing across five platforms.
//
// Two arena flavours:
//   * `ArenaAllocator`  — owns a fixed block; reset is O(1).
//   * `ScopedArenaMark` — RAII sub-scope, so transient allocations inside a
//                         frame stage unwind without a full reset.
//
// The house style follows Twell, which takes its arena from the caller and never
// calls malloc. Calcium does the same for per-frame scratch.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "calcium/core/result.hpp"

namespace ca::core {

/// Default alignment: 16 bytes, so SIMD types land aligned without asking.
inline constexpr std::size_t default_arena_alignment = 16;

/// A fixed-capacity bump allocator.
///
/// Individual deallocation is not supported by design — that is the point. The
/// whole arena is reclaimed at a frame or stage boundary.
class ArenaAllocator {
public:
    ArenaAllocator() noexcept = default;

    /// Adopts caller-provided memory. The arena does not take ownership; the
    /// buffer must outlive it. Mirrors `twell_context_create`.
    ArenaAllocator(std::byte* memory, std::size_t size_bytes) noexcept
        : begin_(memory), capacity_(size_bytes) {}

    /// Allocates and owns a block of `size_bytes`.
    [[nodiscard]] static Result<ArenaAllocator> with_capacity(std::size_t size_bytes);

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& other) noexcept
        : owned_(std::move(other.owned_)),
          begin_(other.begin_),
          capacity_(other.capacity_),
          offset_(other.offset_),
          high_water_mark_(other.high_water_mark_) {
        other.begin_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
        other.high_water_mark_ = 0;
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
        if (this != &other) {
            owned_ = std::move(other.owned_);
            begin_ = other.begin_;
            capacity_ = other.capacity_;
            offset_ = other.offset_;
            high_water_mark_ = other.high_water_mark_;
            other.begin_ = nullptr;
            other.capacity_ = 0;
            other.offset_ = 0;
            other.high_water_mark_ = 0;
        }
        return *this;
    }

    ~ArenaAllocator() = default;

    /// Returns nullptr when exhausted rather than throwing: the frame path must
    /// degrade predictably, and callers can fall back to a spill arena.
    [[nodiscard]] void* allocate_bytes(
        std::size_t size_bytes,
        std::size_t alignment = default_arena_alignment) noexcept;

    /// Allocates storage for `count` objects without constructing them.
    template <typename ObjectType>
    [[nodiscard]] std::span<ObjectType> allocate_uninitialized(
        std::size_t count) noexcept {
        static_assert(std::is_trivially_destructible_v<ObjectType>,
                      "Arena memory is reclaimed wholesale; stored types must be "
                      "trivially destructible.");
        void* memory = allocate_bytes(sizeof(ObjectType) * count,
                                      alignof(ObjectType));
        if (memory == nullptr) {
            return {};
        }
        return std::span<ObjectType>(static_cast<ObjectType*>(memory), count);
    }

    /// Constructs a trivially destructible object in the arena.
    template <typename ObjectType, typename... Arguments>
    [[nodiscard]] ObjectType* emplace(Arguments&&... arguments) noexcept {
        static_assert(std::is_trivially_destructible_v<ObjectType>,
                      "Arena memory is reclaimed wholesale; stored types must be "
                      "trivially destructible.");
        void* memory = allocate_bytes(sizeof(ObjectType), alignof(ObjectType));
        if (memory == nullptr) {
            return nullptr;
        }
        return ::new (memory) ObjectType(std::forward<Arguments>(arguments)...);
    }

    /// Reclaims everything in O(1). Does not run destructors.
    void reset() noexcept { offset_ = 0; }

    [[nodiscard]] std::size_t capacity_bytes() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used_bytes() const noexcept { return offset_; }
    [[nodiscard]] std::size_t available_bytes() const noexcept {
        return capacity_ - offset_;
    }

    /// Largest `used_bytes()` seen since construction. This is how arenas get
    /// sized correctly: run the app, read the mark, size to it plus headroom.
    [[nodiscard]] std::size_t high_water_mark_bytes() const noexcept {
        return high_water_mark_;
    }

    [[nodiscard]] bool owns_pointer(const void* pointer) const noexcept {
        const auto* address = static_cast<const std::byte*>(pointer);
        return address >= begin_ && address < begin_ + capacity_;
    }

    /// Opaque position for scoped unwinding.
    using Marker = std::size_t;
    [[nodiscard]] Marker current_marker() const noexcept { return offset_; }
    void rewind_to_marker(Marker marker) noexcept {
        if (marker <= offset_) {
            offset_ = marker;
        }
    }

private:
    std::unique_ptr<std::byte[]> owned_;
    std::byte* begin_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
    std::size_t high_water_mark_ = 0;
};

/// RAII sub-scope within an arena. Rewinds on destruction.
///
/// Lets a frame stage allocate transient scratch without a full arena reset,
/// which matters when an outer scope's allocations must survive the stage.
class ScopedArenaMark {
public:
    explicit ScopedArenaMark(ArenaAllocator& arena) noexcept
        : arena_(&arena), marker_(arena.current_marker()) {}

    ScopedArenaMark(const ScopedArenaMark&) = delete;
    ScopedArenaMark& operator=(const ScopedArenaMark&) = delete;
    ScopedArenaMark(ScopedArenaMark&&) = delete;
    ScopedArenaMark& operator=(ScopedArenaMark&&) = delete;

    ~ScopedArenaMark() { arena_->rewind_to_marker(marker_); }

private:
    ArenaAllocator* arena_;
    ArenaAllocator::Marker marker_;
};

} // namespace ca::core
