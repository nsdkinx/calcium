// ca::core tests: handles, arena, result, time.

#include "calcium/core/arena_allocator.hpp"
#include "calcium/core/handle.hpp"
#include "calcium/core/result.hpp"
#include "calcium/core/time.hpp"

#include <string>
#include <vector>

#include "calcium_test.hpp"

using ca::core::ArenaAllocator;
using ca::core::Duration;
using ca::core::Error;
using ca::core::ErrorCode;
using ca::core::Handle;
using ca::core::HandlePool;
using ca::core::Result;
using ca::core::ScopedArenaMark;
using ca::core::Timestamp;

namespace {
struct TestTag;
using TestHandle = Handle<TestTag>;
} // namespace

// ---------------------------------------------------------------------------
// Handles — stale-handle detection is the whole point (docs/02 section 4.1)
// ---------------------------------------------------------------------------

CA_TEST(default_handle_is_invalid) {
    const TestHandle handle;
    CA_CHECK(!handle.is_valid());
    CA_CHECK(!static_cast<bool>(handle));
}

CA_TEST(handle_packs_and_unpacks_through_bits) {
    const TestHandle original{42u, 7u};
    const TestHandle restored = TestHandle::from_bits(original.to_bits());
    CA_CHECK_EQUAL(restored.index(), 42u);
    CA_CHECK_EQUAL(restored.generation(), 7u);
    CA_CHECK(restored == original);
}

CA_TEST(pool_reuses_freed_slots) {
    HandlePool<TestTag> pool;
    bool is_new = false;

    const TestHandle first = pool.acquire(is_new);
    CA_CHECK(is_new);
    CA_CHECK_EQUAL(pool.live_count(), 1u);
    CA_CHECK(pool.is_valid(first));

    CA_CHECK(pool.release(first));
    CA_CHECK_EQUAL(pool.live_count(), 0u);

    const TestHandle second = pool.acquire(is_new);
    CA_CHECK(!is_new);  // slot recycled, so payload arrays need not grow
    CA_CHECK_EQUAL(second.index(), first.index());
    CA_CHECK_EQUAL(pool.slot_count(), 1u);
}

CA_TEST(released_handle_becomes_stale_and_is_detected) {
    HandlePool<TestTag> pool;
    bool is_new = false;

    const TestHandle original = pool.acquire(is_new);
    CA_CHECK(pool.release(original));

    // The stale handle must not validate, even though its slot is live again.
    const TestHandle recycled = pool.acquire(is_new);
    CA_CHECK(pool.is_valid(recycled));
    CA_CHECK(!pool.is_valid(original));
    CA_CHECK(original != recycled);
    CA_CHECK(recycled.generation() != original.generation());
}

CA_TEST(double_release_is_reported_not_silent) {
    HandlePool<TestTag> pool;
    bool is_new = false;
    const TestHandle handle = pool.acquire(is_new);

    CA_CHECK(pool.release(handle));
    CA_CHECK(!pool.release(handle));  // second release reports failure
}

CA_TEST(pool_free_list_is_last_in_first_out) {
    HandlePool<TestTag> pool;
    bool is_new = false;
    const TestHandle a = pool.acquire(is_new);
    const TestHandle b = pool.acquire(is_new);
    const TestHandle c = pool.acquire(is_new);
    CA_CHECK_EQUAL(pool.slot_count(), 3u);

    CA_CHECK(pool.release(a));
    CA_CHECK(pool.release(b));
    CA_CHECK(pool.release(c));

    // Most recently freed slot comes back first.
    CA_CHECK_EQUAL(pool.acquire(is_new).index(), c.index());
    CA_CHECK_EQUAL(pool.acquire(is_new).index(), b.index());
    CA_CHECK_EQUAL(pool.acquire(is_new).index(), a.index());
    CA_CHECK_EQUAL(pool.slot_count(), 3u);  // never grew
}

// ---------------------------------------------------------------------------
// Arena allocator — P8
// ---------------------------------------------------------------------------

CA_TEST(arena_allocates_and_tracks_usage) {
    auto arena_result = ArenaAllocator::with_capacity(1024);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    CA_CHECK_EQUAL(arena.capacity_bytes(), std::size_t{1024});
    CA_CHECK_EQUAL(arena.used_bytes(), std::size_t{0});

    void* block = arena.allocate_bytes(64, 16);
    CA_CHECK(block != nullptr);
    CA_CHECK(arena.used_bytes() >= 64);
    CA_CHECK(arena.owns_pointer(block));
}

CA_TEST(arena_honors_alignment) {
    auto arena_result = ArenaAllocator::with_capacity(1024);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    // Force a misaligned offset, then confirm the next request realigns.
    CA_CHECK(arena.allocate_bytes(1, 1) != nullptr);
    for (const std::size_t alignment : {std::size_t{4}, std::size_t{8},
                                        std::size_t{16}, std::size_t{32}}) {
        void* block = arena.allocate_bytes(8, alignment);
        CA_CHECK(block != nullptr);
        const auto address = reinterpret_cast<std::uintptr_t>(block);
        CA_CHECK_EQUAL(address % alignment, std::uintptr_t{0});
    }
}

CA_TEST(arena_returns_null_when_exhausted_rather_than_throwing) {
    auto arena_result = ArenaAllocator::with_capacity(128);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    CA_CHECK(arena.allocate_bytes(128, 1) != nullptr);
    CA_CHECK(arena.allocate_bytes(1, 1) == nullptr);   // exhausted
    CA_CHECK(arena.allocate_bytes(1'000'000, 1) == nullptr);
}

CA_TEST(arena_rejects_non_power_of_two_alignment) {
    auto arena_result = ArenaAllocator::with_capacity(256);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    CA_CHECK(arena.allocate_bytes(8, 3) == nullptr);
    CA_CHECK(arena.allocate_bytes(8, 0) == nullptr);
}

CA_TEST(arena_reset_is_o1_and_reclaims_everything) {
    auto arena_result = ArenaAllocator::with_capacity(512);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    CA_CHECK(arena.allocate_bytes(256, 16) != nullptr);
    const std::size_t high_water = arena.high_water_mark_bytes();
    CA_CHECK(high_water >= 256);

    arena.reset();
    CA_CHECK_EQUAL(arena.used_bytes(), std::size_t{0});
    // The high-water mark survives reset: it is how arenas get sized.
    CA_CHECK_EQUAL(arena.high_water_mark_bytes(), high_water);
}

CA_TEST(scoped_mark_rewinds_on_scope_exit) {
    auto arena_result = ArenaAllocator::with_capacity(1024);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    CA_CHECK(arena.allocate_bytes(64, 16) != nullptr);
    const std::size_t outer_usage = arena.used_bytes();

    {
        const ScopedArenaMark mark{arena};
        CA_CHECK(arena.allocate_bytes(256, 16) != nullptr);
        CA_CHECK(arena.used_bytes() > outer_usage);
    }

    CA_CHECK_EQUAL(arena.used_bytes(), outer_usage);
}

CA_TEST(arena_emplace_constructs_in_place) {
    auto arena_result = ArenaAllocator::with_capacity(1024);
    CA_CHECK(arena_result.has_value());
    ArenaAllocator arena = std::move(arena_result).take_value();

    struct Sample { int a; double b; };
    Sample* sample = arena.emplace<Sample>(7, 2.5);
    CA_CHECK(sample != nullptr);
    if (sample != nullptr) {
        CA_CHECK_EQUAL(sample->a, 7);
        CA_CHECK_NEAR(sample->b, 2.5, 0.0);
    }

    auto span = arena.allocate_uninitialized<int>(16);
    CA_CHECK_EQUAL(span.size(), std::size_t{16});
}

CA_TEST(arena_with_zero_capacity_is_an_error) {
    auto arena_result = ArenaAllocator::with_capacity(0);
    CA_CHECK(!arena_result.has_value());
    CA_CHECK(arena_result.error_code() == ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

CA_TEST(result_carries_value) {
    const Result<int> result{42};
    CA_CHECK(result.has_value());
    CA_CHECK(static_cast<bool>(result));
    CA_CHECK_EQUAL(result.value(), 42);
    CA_CHECK(result.error_code() == ErrorCode::none);
}

CA_TEST(result_carries_error) {
    const Result<int> result{ErrorCode::not_found, "no such layer"};
    CA_CHECK(!result.has_value());
    CA_CHECK(result.error_code() == ErrorCode::not_found);
    CA_CHECK(result.error().context() == "no such layer");
    CA_CHECK_EQUAL(result.value_or(-1), -1);
}

CA_TEST(result_moves_non_trivial_values) {
    Result<std::string> result{std::string("calcium")};
    CA_CHECK(result.has_value());
    const std::string taken = std::move(result).take_value();
    CA_CHECK(taken == "calcium");
}

CA_TEST(result_of_void_reports_success_and_failure) {
    const auto success = ca::core::VoidResult::success();
    CA_CHECK(success.has_value());

    const ca::core::VoidResult failure{ErrorCode::wrong_thread};
    CA_CHECK(!failure.has_value());
    CA_CHECK(failure.error_code() == ErrorCode::wrong_thread);
}

CA_TEST(error_description_falls_back_to_code_text) {
    const Error without_context{ErrorCode::out_of_memory};
    CA_CHECK(without_context.description() == "out of memory");

    const Error with_context{ErrorCode::out_of_memory, "glyph atlas full"};
    CA_CHECK(with_context.description() == "glyph atlas full");
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

CA_TEST(duration_converts_between_units) {
    const Duration half_second = Duration::from_milliseconds(500.0);
    CA_CHECK_NEAR(half_second.seconds(), 0.5, 1e-12);
    CA_CHECK_NEAR(half_second.milliseconds(), 500.0, 1e-9);
    CA_CHECK_NEAR(half_second.microseconds(), 500'000.0, 1e-6);
}

CA_TEST(duration_from_hertz_gives_frame_interval) {
    CA_CHECK_NEAR(Duration::from_hertz(120.0).milliseconds(), 8.3333333, 1e-6);
    CA_CHECK_NEAR(Duration::from_hertz(60.0).milliseconds(), 16.6666667, 1e-6);
    CA_CHECK_NEAR(Duration::from_hertz(0.0).seconds(), 0.0, 0.0);
}

CA_TEST(timestamp_difference_yields_duration) {
    const Timestamp start = Timestamp::from_seconds(10.0);
    const Timestamp end = Timestamp::from_seconds(10.25);
    CA_CHECK_NEAR((end - start).seconds(), 0.25, 1e-12);
    CA_CHECK((start + Duration::from_seconds(0.25)) == end);
}

CA_TEST(monotonic_clock_does_not_move_backwards) {
    const Timestamp first = Timestamp::now();
    const Timestamp second = Timestamp::now();
    CA_CHECK(second.seconds() >= first.seconds());
}

CA_TEST(frame_recorder_counts_budget_overruns_on_slowest_stage) {
    ca::core::FrameTimingRecorder recorder;
    recorder.set_frame_budget(Duration::from_hertz(120.0));  // 8.33 ms

    // Within budget: stages are pipelined, so the sum exceeding the budget is
    // fine as long as no single stage does.
    recorder.record_frame(Duration::from_milliseconds(4.0),
                          Duration::from_milliseconds(2.0),
                          Duration::from_milliseconds(6.0));
    CA_CHECK_EQUAL(recorder.budget_overrun_count(), 0u);

    // A single stage over budget is a late frame.
    recorder.record_frame(Duration::from_milliseconds(20.0),
                          Duration::from_milliseconds(2.0),
                          Duration::from_milliseconds(6.0));
    CA_CHECK_EQUAL(recorder.budget_overrun_count(), 1u);
    CA_CHECK_EQUAL(recorder.recorded_frame_count(), 2u);
}

CA_TEST_MAIN()
