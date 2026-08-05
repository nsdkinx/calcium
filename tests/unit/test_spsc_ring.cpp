// ca::core::SpscRing tests.

#include "calcium/core/spsc_ring.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include "calcium_test.hpp"

using ca::core::SpscRing;

struct Pod {
    std::uint64_t sequence = 0;
    float payload = 0.0f;
};
static_assert(std::is_trivially_copyable_v<Pod>);

CA_TEST(spsc_ring_single_thread_round_trip) {
    SpscRing<Pod, 4> ring;
    CA_CHECK(ring.count() == 0);

    CA_CHECK(ring.try_push({1, 1.0f}));
    CA_CHECK(ring.try_push({2, 2.0f}));
    CA_CHECK(ring.try_push({3, 3.0f}));
    CA_CHECK(ring.try_push({4, 4.0f}));
    CA_CHECK(!ring.try_push({5, 5.0f}));  // full

    Pod out{};
    CA_CHECK(ring.try_pop(out));
    CA_CHECK(out.sequence == 1);
    CA_CHECK(ring.try_push({5, 5.0f}));  // room again, FIFO preserved

    CA_CHECK(ring.try_pop(out));
    CA_CHECK(out.sequence == 2);
    CA_CHECK(ring.try_pop(out));
    CA_CHECK(out.sequence == 3);
    CA_CHECK(ring.try_pop(out));
    CA_CHECK(out.sequence == 4);
    CA_CHECK(ring.try_pop(out));
    CA_CHECK(out.sequence == 5);
    CA_CHECK(!ring.try_pop(out));  // empty
    CA_CHECK(ring.count() == 0);
}

CA_TEST(spsc_ring_wraps_around) {
    SpscRing<Pod, 4> ring;
    for (std::uint64_t i = 0; i < 100; ++i) {
        CA_CHECK(ring.try_push({i, 0.0f}));
        Pod out{};
        CA_CHECK(ring.try_pop(out));
        CA_CHECK(out.sequence == i);
    }
    CA_CHECK(ring.count() == 0);
}

CA_TEST(spsc_ring_concurrent_transfer_is_lossless) {
    constexpr int total = 100'000;
    SpscRing<Pod, 1024> ring;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (int i = 0; i < total; ++i) {
            while (!ring.try_push({static_cast<std::uint64_t>(i), 0.0f})) {
                // spin: the ring must not drop items
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        Pod out{};
        while (true) {
            if (ring.try_pop(out)) {
                CA_CHECK(out.sequence == expected);
                ++expected;
                continue;
            }
            if (producer_done.load(std::memory_order_acquire) &&
                ring.count() == 0) {
                break;
            }
        }
        CA_CHECK(expected == total);
    });

    producer.join();
    consumer.join();
}

CA_TEST(spsc_ring_pop_or_wait_blocks_and_wakes) {
    SpscRing<Pod, 4> ring;
    std::atomic<bool> received{false};
    std::uint64_t got = 0;

    std::thread consumer([&] {
        Pod out{};
        CA_CHECK(ring.pop_or_wait(out));  // blocks until the producer pushes
        got = out.sequence;
        received.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CA_CHECK(!received.load(std::memory_order_acquire));  // still blocked
    ring.try_push({42, 0.0f});
    consumer.join();
    CA_CHECK(got == 42);
}

CA_TEST(spsc_ring_notify_wake_unblocks_without_item) {
    SpscRing<Pod, 4> ring;
    bool woken = false;

    std::thread consumer([&] {
        Pod out{};
        if (!ring.pop_or_wait(out)) {
            woken = true;  // woken without an item
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ring.notify_wake();
    consumer.join();
    CA_CHECK(woken);
}

CA_TEST_MAIN()
