#pragma once

// A bounded, lock-free, single-producer/single-consumer ring.
//
// This is the wire between the platform thread and the UI thread, and between
// the UI thread and the compositor (docs/02-architecture.md §2): events flow
// one way, intents flow the other, and neither side may ever block the other
// or allocate. The ring is wait-free for the common path — push and pop are a
// couple of atomic loads and a memcpy — and `pop_or_wait` parks the consumer
// on a condition variable that the producer signals.
//
// Invariants:
//   * exactly one producer thread, exactly one consumer thread;
//   * `ItemType` is trivially copyable and fixed-size (checked at compile
//     time), so items are copied by bytes and the ring never runs their
//     constructors or destructors;
//   * capacity is a power of two (checked at compile time).
//
// The queue never blocks the producer: `try_push` fails instead of waiting,
// so a slow consumer degrades by dropping the newest event — never by stalling
// the platform thread (which must never block on the UI thread).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <type_traits>

namespace ca::core {

template <typename ItemType, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "SpscRing capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<ItemType>,
                  "SpscRing items must be trivially copyable");

public:
    static constexpr std::size_t capacity = Capacity;

    SpscRing() = default;

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    /// Appends an item without blocking. Returns false when the ring is full;
    /// the producer then decides whether to drop or retry.
    bool try_push(const ItemType& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head >= Capacity) {
            return false;  // full
        }
        std::memcpy(&storage_[tail & (Capacity - 1)], &item, sizeof(ItemType));
        tail_.store(tail + 1, std::memory_order_release);
        wake_consumer();
        return true;
    }

    /// Removes the oldest item. Returns false when empty.
    bool try_pop(ItemType& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;  // empty
        }
        std::memcpy(&out, &storage_[head & (Capacity - 1)], sizeof(ItemType));
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Blocks until an item is available. Returns false only when woken
    /// without an item by `notify_wake` (the platform's way of saying "quit"
    /// without pushing an item, which keeps quit out of the data path).
    bool pop_or_wait(ItemType& out) {
        if (try_pop(out)) {
            return true;
        }
        std::unique_lock lock{wake_mutex_};
        while (!wake_flag_) {
            wake_cv_.wait(lock);
        }
        wake_flag_ = false;
        return try_pop(out);
    }

    /// Unblocks a pending `pop_or_wait` with no item delivered. Thread-safe.
    void notify_wake() noexcept {
        {
            std::lock_guard lock{wake_mutex_};
            wake_flag_ = true;
        }
        wake_cv_.notify_one();
    }

    [[nodiscard]] std::size_t count() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail - head;
    }

private:
    void wake_consumer() noexcept {
        // Only signal when a consumer might actually be parked; the flag
        // check is racy by design (a lost wake is harmless — the producer
        // re-checks on its next push), so this stays on the wait-free path.
        if (wake_flag_) {
            return;
        }
        std::lock_guard lock{wake_mutex_};
        wake_flag_ = true;
        wake_cv_.notify_one();
    }

    // Producer and consumer indices never share a cache line.
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};

    alignas(64) std::array<std::byte, sizeof(ItemType)> storage_[Capacity]{};

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    bool wake_flag_ = false;
};

} // namespace ca::core
