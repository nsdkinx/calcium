#include "calcium/core/arena_allocator.hpp"

namespace ca::core {

Result<ArenaAllocator> ArenaAllocator::with_capacity(std::size_t size_bytes) {
    if (size_bytes == 0) {
        return Error{ErrorCode::invalid_argument, "arena capacity must be non-zero"};
    }

    auto memory = std::make_unique<std::byte[]>(size_bytes);
    if (!memory) {
        return Error{ErrorCode::out_of_memory, "failed to allocate arena block"};
    }

    ArenaAllocator arena;
    arena.begin_ = memory.get();
    arena.capacity_ = size_bytes;
    arena.owned_ = std::move(memory);
    return arena;
}

void* ArenaAllocator::allocate_bytes(std::size_t size_bytes,
                                     std::size_t alignment) noexcept {
    if (begin_ == nullptr || size_bytes == 0) {
        return nullptr;
    }

    // Alignment must be a non-zero power of two.
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }

    const std::size_t current = reinterpret_cast<std::uintptr_t>(begin_ + offset_);
    const std::size_t aligned = (current + alignment - 1) & ~(alignment - 1);
    const std::size_t padding = aligned - current;

    // Overflow-safe capacity check: compare against remaining space rather than
    // computing offset_ + padding + size_bytes, which could wrap.
    if (padding > capacity_ - offset_) {
        return nullptr;
    }
    const std::size_t remaining_after_padding = capacity_ - offset_ - padding;
    if (size_bytes > remaining_after_padding) {
        return nullptr;
    }

    offset_ += padding;
    void* result = begin_ + offset_;
    offset_ += size_bytes;

    if (offset_ > high_water_mark_) {
        high_water_mark_ = offset_;
    }
    return result;
}

} // namespace ca::core
