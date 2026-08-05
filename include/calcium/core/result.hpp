#pragma once

// The error channel for Calcium's public API.
//
// No exceptions cross the API boundary (P13): the C ABI cannot carry them, and
// every language binding would have to translate them. Internally, non-frame-path
// code may throw freely.
//
// `Result<T>` is deliberately not `std::expected` — Calcium targets toolchains
// where <expected> is not universally available, and the error type is fixed.

#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ca::core {

/// Error codes. Values are part of the C ABI and must never be renumbered;
/// append only. Mirrors `ca_result_t` in calcium.h.
enum class ErrorCode : std::int32_t {
    none = 0,
    invalid_handle,
    invalid_argument,
    out_of_memory,
    out_of_range,
    backend_failure,
    wrong_thread,
    unsupported,
    not_found,
    already_exists,
    precondition_failed,
    arena_exhausted,
};

[[nodiscard]] constexpr std::string_view describe(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::none:                return "no error";
    case ErrorCode::invalid_handle:      return "invalid or stale handle";
    case ErrorCode::invalid_argument:    return "invalid argument";
    case ErrorCode::out_of_memory:       return "out of memory";
    case ErrorCode::out_of_range:        return "index or value out of range";
    case ErrorCode::backend_failure:     return "backend reported a failure";
    case ErrorCode::wrong_thread:        return "called from the wrong thread";
    case ErrorCode::unsupported:         return "operation not supported";
    case ErrorCode::not_found:           return "not found";
    case ErrorCode::already_exists:      return "already exists";
    case ErrorCode::precondition_failed: return "precondition failed";
    case ErrorCode::arena_exhausted:     return "arena exhausted";
    }
    return "unrecognized error";
}

/// An error with optional static context.
///
/// `context` must point at a string literal or otherwise static storage: Error
/// is copied through the frame path and must not allocate.
class Error {
public:
    constexpr Error() noexcept = default;

    constexpr explicit Error(ErrorCode code,
                             std::string_view context = {}) noexcept
        : code_(code), context_(context) {}

    [[nodiscard]] constexpr ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] constexpr std::string_view context() const noexcept {
        return context_;
    }
    [[nodiscard]] constexpr std::string_view description() const noexcept {
        return context_.empty() ? describe(code_) : context_;
    }
    [[nodiscard]] constexpr bool operator==(const Error&) const noexcept = default;

private:
    ErrorCode code_ = ErrorCode::none;
    std::string_view context_{};
};

/// Either a value or an Error. Always check before reading.
template <typename ValueType>
class [[nodiscard]] Result {
public:
    using value_type = ValueType;

    constexpr Result(ValueType value) noexcept(
        std::is_nothrow_move_constructible_v<ValueType>)
        : has_value_(true) {
        ::new (static_cast<void*>(storage_)) ValueType(std::move(value));
    }

    constexpr Result(Error error) noexcept : error_(error), has_value_(false) {}

    constexpr Result(ErrorCode code, std::string_view context = {}) noexcept
        : error_(code, context), has_value_(false) {}

    Result(const Result& other) : error_(other.error_), has_value_(other.has_value_) {
        if (has_value_) {
            ::new (static_cast<void*>(storage_)) ValueType(*other.value_pointer());
        }
    }

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<ValueType>)
        : error_(other.error_), has_value_(other.has_value_) {
        if (has_value_) {
            ::new (static_cast<void*>(storage_))
                ValueType(std::move(*other.value_pointer()));
        }
    }

    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy();
            error_ = other.error_;
            has_value_ = other.has_value_;
            if (has_value_) {
                ::new (static_cast<void*>(storage_)) ValueType(*other.value_pointer());
            }
        }
        return *this;
    }

    Result& operator=(Result&& other)
        noexcept(std::is_nothrow_move_constructible_v<ValueType>) {
        if (this != &other) {
            destroy();
            error_ = other.error_;
            has_value_ = other.has_value_;
            if (has_value_) {
                ::new (static_cast<void*>(storage_))
                    ValueType(std::move(*other.value_pointer()));
            }
        }
        return *this;
    }

    ~Result() { destroy(); }

    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
    explicit constexpr operator bool() const noexcept { return has_value_; }

    [[nodiscard]] const ValueType& value() const& noexcept {
        return *value_pointer();
    }
    [[nodiscard]] ValueType& value() & noexcept { return *value_pointer(); }
    [[nodiscard]] ValueType&& take_value() && noexcept {
        return std::move(*value_pointer());
    }

    [[nodiscard]] const ValueType* operator->() const noexcept {
        return value_pointer();
    }
    [[nodiscard]] ValueType* operator->() noexcept { return value_pointer(); }
    [[nodiscard]] const ValueType& operator*() const& noexcept {
        return *value_pointer();
    }

    [[nodiscard]] ValueType value_or(ValueType fallback) const {
        return has_value_ ? *value_pointer() : std::move(fallback);
    }

    [[nodiscard]] constexpr Error error() const noexcept { return error_; }
    [[nodiscard]] constexpr ErrorCode error_code() const noexcept {
        return error_.code();
    }

private:
    [[nodiscard]] ValueType* value_pointer() noexcept {
        return reinterpret_cast<ValueType*>(storage_);
    }
    [[nodiscard]] const ValueType* value_pointer() const noexcept {
        return reinterpret_cast<const ValueType*>(storage_);
    }

    void destroy() noexcept {
        if (has_value_) {
            value_pointer()->~ValueType();
            has_value_ = false;
        }
    }

    alignas(ValueType) unsigned char storage_[sizeof(ValueType)]{};
    Error error_{};
    bool has_value_ = false;
};

/// Void specialization: success or failure with no payload.
template <>
class [[nodiscard]] Result<void> {
public:
    constexpr Result() noexcept = default;
    constexpr Result(Error error) noexcept : error_(error) {}
    constexpr Result(ErrorCode code, std::string_view context = {}) noexcept
        : error_(code, context) {}

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return error_.code() == ErrorCode::none;
    }
    explicit constexpr operator bool() const noexcept { return has_value(); }
    [[nodiscard]] constexpr Error error() const noexcept { return error_; }
    [[nodiscard]] constexpr ErrorCode error_code() const noexcept {
        return error_.code();
    }

    [[nodiscard]] static constexpr Result success() noexcept { return Result{}; }

private:
    Error error_{};
};

using VoidResult = Result<void>;

/// Propagates an error out of the current function, mirroring Rust's `?`.
/// Verbose by design: a macro that hides control flow should look like one.
#define CA_TRY(expression)                                                     \
    do {                                                                       \
        auto&& ca_try_result_ = (expression);                                   \
        if (!ca_try_result_.has_value()) {                                      \
            return ::ca::core::Error(ca_try_result_.error());                   \
        }                                                                      \
    } while (false)

} // namespace ca::core
