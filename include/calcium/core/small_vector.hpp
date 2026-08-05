#pragma once

// A vector with inline storage.
//
// The frame path must not allocate (P8), but most real collections are small:
// a layer's sublayer list, a gesture recognizer's tracked touches, a theme's
// font stack. SmallVector stores the first `InlineCapacity` elements inline
// and moves to the heap only when that is exceeded, so the common case costs
// zero allocations and zero indirection — `data()` is a pointer to the inline
// buffer and iterators are plain pointers.
//
// Semantics are a deliberate subset of std::vector: the operations the
// framework actually uses. Notably there is no `insert`/`erase` by position
// (tree edits go through the pool's handle machinery, not element shifts).

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace ca::core {

template <typename ValueType, std::size_t InlineCapacity>
class SmallVector {
public:
    using value_type = ValueType;
    using size_type = std::size_t;
    using iterator = ValueType*;
    using const_iterator = const ValueType*;

    static constexpr size_type inline_capacity = InlineCapacity;

    SmallVector() noexcept = default;

    SmallVector(std::initializer_list<ValueType> values) {
        reserve(values.size());
        for (const auto& value : values) {
            push_back(value);
        }
    }

    // Copy: elements are copied into inline storage when they fit, else a heap
    // buffer. `data_` must be re-pointed because it references this object's
    // own inline storage.
    SmallVector(const SmallVector& other) {
        reserve(other.size_);
        for (const auto& value : other) {
            push_back(value);
        }
    }

    SmallVector& operator=(const SmallVector& other) {
        if (this != &other) {
            SmallVector copy{other};
            swap(copy);
        }
        return *this;
    }

    // Move: a heap buffer is stolen; an inline buffer is element-moved and
    // `data_` re-pointed at the destination's inline storage. Declared
    // noexcept: the element moves below are noexcept in every use that matters
    // (POD and value-semantic types); a throwing move would terminate rather
    // than silently leave a half-moved container.
    SmallVector(SmallVector&& other) noexcept {
        if (other.on_heap_) {
            data_ = other.data_;
            capacity_ = other.capacity_;
            on_heap_ = true;
            size_ = other.size_;
            other.data_ = other.inline_data();
            other.capacity_ = inline_capacity;
            other.on_heap_ = false;
            other.size_ = 0;
            return;
        }
        for (auto& value : other) {
            push_back(std::move(value));
        }
        other.size_ = 0;
    }

    SmallVector& operator=(SmallVector&& other) noexcept {
        if (this != &other) {
            destroy_range(data_, size_);
            if (on_heap_) {
                ::operator delete(data_);
            }
            data_ = inline_data();
            capacity_ = inline_capacity;
            on_heap_ = false;
            size_ = 0;

            if (other.on_heap_) {
                data_ = other.data_;
                capacity_ = other.capacity_;
                on_heap_ = true;
                size_ = other.size_;
                other.data_ = other.inline_data();
                other.capacity_ = inline_capacity;
                other.on_heap_ = false;
                other.size_ = 0;
            } else {
                for (auto& value : other) {
                    push_back(std::move(value));
                }
                other.size_ = 0;
            }
        }
        return *this;
    }

    ~SmallVector() { clear(); }

    // --- Capacity -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    void reserve(size_type new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }
        ValueType* new_data = static_cast<ValueType*>(
            ::operator new(new_capacity * sizeof(ValueType)));
        size_type moved = 0;
        try {
            for (; moved < size_; ++moved) {
                ::new (static_cast<void*>(new_data + moved))
                    ValueType(std::move_if_noexcept(data_[moved]));
            }
        } catch (...) {
            destroy_range(new_data, moved);
            ::operator delete(new_data);
            throw;
        }
        destroy_range(data_, size_);
        if (on_heap_) {
            ::operator delete(data_);
        }
        data_ = new_data;
        capacity_ = new_capacity;
        on_heap_ = true;
    }

    /// Releases heap storage when the elements fit inline. A no-op for an
    /// already-inline vector, which is the optimal state.
    void shrink_to_fit() {
        if (!on_heap_) {
            return;
        }
        if (size_ == 0) {
            ::operator delete(data_);
            data_ = inline_data();
            capacity_ = inline_capacity;
            on_heap_ = false;
            return;
        }
        if (size_ <= inline_capacity) {
            ValueType* new_data = inline_data();
            size_type moved = 0;
            try {
                for (; moved < size_; ++moved) {
                    ::new (static_cast<void*>(new_data + moved))
                        ValueType(std::move_if_noexcept(data_[moved]));
                }
            } catch (...) {
                destroy_range(new_data, moved);
                throw;
            }
            destroy_range(data_, size_);
            ::operator delete(data_);
            data_ = new_data;
            capacity_ = inline_capacity;
            on_heap_ = false;
            return;
        }
        ValueType* new_data = static_cast<ValueType*>(
            ::operator new(size_ * sizeof(ValueType)));
        size_type moved = 0;
        try {
            for (; moved < size_; ++moved) {
                ::new (static_cast<void*>(new_data + moved))
                    ValueType(std::move_if_noexcept(data_[moved]));
            }
        } catch (...) {
            destroy_range(new_data, moved);
            ::operator delete(new_data);
            throw;
        }
        destroy_range(data_, size_);
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = size_;
    }

    // --- Element access -----------------------------------------------------

    [[nodiscard]] ValueType* data() noexcept { return data_; }
    [[nodiscard]] const ValueType* data() const noexcept { return data_; }

    [[nodiscard]] ValueType& operator[](size_type index) noexcept {
        return data_[index];
    }
    [[nodiscard]] const ValueType& operator[](size_type index) const noexcept {
        return data_[index];
    }
    [[nodiscard]] ValueType& front() noexcept { return data_[0]; }
    [[nodiscard]] const ValueType& front() const noexcept { return data_[0]; }
    [[nodiscard]] ValueType& back() noexcept { return data_[size_ - 1]; }
    [[nodiscard]] const ValueType& back() const noexcept { return data_[size_ - 1]; }

    // --- Mutation -----------------------------------------------------------

    template <typename... Args>
    ValueType& emplace_back(Args&&... args) {
        if (size_ == capacity_) {
            reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        }
        ::new (static_cast<void*>(data_ + size_)) ValueType(
            std::forward<Args>(args)...);
        ++size_;
        return back();
    }

    void push_back(const ValueType& value) { emplace_back(value); }
    void push_back(ValueType&& value) { emplace_back(std::move(value)); }

    void pop_back() noexcept { data_[--size_].~ValueType(); }

    void resize(size_type new_size) {
        if (new_size < size_) {
            destroy_range(data_ + new_size, size_ - new_size);
        } else if (new_size > size_) {
            reserve(new_size);
            for (size_type index = size_; index < new_size; ++index) {
                ::new (static_cast<void*>(data_ + index)) ValueType{};
            }
        }
        size_ = new_size;
    }

    void clear() noexcept {
        destroy_range(data_, size_);
        size_ = 0;
    }

    void swap(SmallVector& other) noexcept {
        SmallVector tmp{std::move(*this)};
        *this = std::move(other);
        other = std::move(tmp);
    }

    // --- Iteration ----------------------------------------------------------

    [[nodiscard]] iterator begin() noexcept { return data_; }
    [[nodiscard]] iterator end() noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_; }
    [[nodiscard]] const_iterator cend() const noexcept { return data_ + size_; }

    [[nodiscard]] bool operator==(const SmallVector& other) const {
        return std::equal(begin(), end(), other.begin(), other.end());
    }

private:
    static constexpr size_type inline_capacity_or_one =
        InlineCapacity > 0 ? InlineCapacity : 1;

    [[nodiscard]] ValueType* inline_data() noexcept {
        return reinterpret_cast<ValueType*>(inline_storage_);
    }

    static void destroy_range(ValueType* first, size_type count) noexcept {
        if constexpr (!std::is_trivially_destructible_v<ValueType>) {
            for (size_type index = 0; index < count; ++index) {
                first[index].~ValueType();
            }
        }
    }

    alignas(ValueType) std::byte inline_storage_[inline_capacity_or_one *
                                                 sizeof(ValueType)];
    ValueType* data_ = inline_data();
    size_type size_ = 0;
    size_type capacity_ = InlineCapacity;
    bool on_heap_ = false;
};

} // namespace ca::core
