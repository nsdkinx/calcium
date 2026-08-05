// ca::core::SmallVector tests.

#include "calcium/core/small_vector.hpp"

#include <string>
#include <utility>

#include "calcium_test.hpp"

using ca::core::SmallVector;

// True when the vector currently stores its elements inline (P8: the common
// case must not allocate — a pointer inside the object proves it).
template <typename Vector>
bool is_inline(const Vector& v) {
    const char* data = reinterpret_cast<const char*>(v.data());
    const char* object = reinterpret_cast<const char*>(&v);
    return data >= object && data < object + sizeof(Vector);
}

CA_TEST(small_vector_stays_inline_within_capacity) {
    SmallVector<int, 4> v;
    CA_CHECK(v.empty());
    CA_CHECK(is_inline(v));

    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    CA_CHECK(v.size() == 3);
    CA_CHECK(is_inline(v));
    CA_CHECK(v[0] == 1 && v[2] == 3);
    CA_CHECK(v.front() == 1 && v.back() == 3);
}

CA_TEST(small_vector_moves_to_heap_on_growth) {
    SmallVector<int, 4> v;
    for (int i = 0; i < 4; ++i) {
        v.push_back(i);
    }
    CA_CHECK(is_inline(v));

    v.push_back(4);  // exceeds inline capacity
    CA_CHECK(!is_inline(v));
    for (int i = 0; i < 5; ++i) {
        CA_CHECK(v[i] == i);
    }
    CA_CHECK(v.capacity() >= 5);
}

CA_TEST(small_vector_zero_inline_capacity_is_valid) {
    SmallVector<int, 0> v;
    CA_CHECK(v.empty());
    v.push_back(7);
    CA_CHECK(!is_inline(v));  // zero inline capacity means the first push grows
    CA_CHECK(v.back() == 7);
}

CA_TEST(small_vector_initializer_list) {
    const SmallVector<int, 3> v{1, 2, 3};
    CA_CHECK(v.size() == 3);
    CA_CHECK(v[1] == 2);
    CA_CHECK(is_inline(v));
}

CA_TEST(small_vector_iteration) {
    SmallVector<int, 2> v{10, 20};
    v.push_back(30);

    int sum = 0;
    for (const int value : v) {
        sum += value;
    }
    CA_CHECK(sum == 60);

    // Iterators are plain pointers.
    static_assert(std::is_same_v<decltype(v.begin()), int*>);
    CA_CHECK(v.end() - v.begin() == 3);
}

CA_TEST(small_vector_copy) {
    SmallVector<int, 2> v{1, 2, 3, 4, 5};  // on the heap
    const SmallVector<int, 2> copy = v;
    CA_CHECK(copy == v);
    CA_CHECK(!is_inline(copy));

    // Copying into a small source stays inline.
    const SmallVector<int, 2> small{9};
    const SmallVector<int, 2> small_copy = small;
    CA_CHECK(small_copy == small);
    CA_CHECK(is_inline(small_copy));

    // Assignment replaces contents.
    SmallVector<int, 2> target{1};
    target = v;
    CA_CHECK(target == v);
}

CA_TEST(small_vector_move) {
    SmallVector<int, 2> v{1, 2, 3};  // on the heap
    SmallVector<int, 2> moved = std::move(v);
    CA_CHECK(moved.size() == 3);
    CA_CHECK(moved[2] == 3);
    CA_CHECK(v.empty());  // moved-from is empty, not dangling

    SmallVector<int, 2> inline_v{7, 8};
    SmallVector<int, 2> inline_moved = std::move(inline_v);
    CA_CHECK(inline_moved[0] == 7 && inline_moved[1] == 8);
    CA_CHECK(inline_v.empty());
    CA_CHECK(is_inline(inline_moved));

    // Move assignment steals the heap buffer.
    SmallVector<int, 2> target{0};
    target = std::move(moved);
    CA_CHECK(target.size() == 3);
    CA_CHECK(!is_inline(target));
}

CA_TEST(small_vector_resize_and_clear) {
    SmallVector<int, 4> v;
    v.resize(3);
    CA_CHECK(v.size() == 3);
    CA_CHECK(v[0] == 0 && v[2] == 0);  // value-initialized

    v.resize(5);
    CA_CHECK(v.size() == 5);

    v.resize(2);
    CA_CHECK(v.size() == 2);
    CA_CHECK(v[1] == 0);

    v.clear();
    CA_CHECK(v.empty());
    v.push_back(42);
    CA_CHECK(v.back() == 42);
}

CA_TEST(small_vector_handles_non_trivial_types) {
    SmallVector<std::string, 2> v;
    v.emplace_back("hello");
    v.push_back(std::string{"world"});
    v.emplace_back("third");  // grows off the inline buffer

    CA_CHECK(v[0] == "hello");
    CA_CHECK(v[1] == "world");
    CA_CHECK(v[2] == "third");

    SmallVector<std::string, 2> copy = v;
    CA_CHECK(copy == v);

    SmallVector<std::string, 2> moved = std::move(v);
    CA_CHECK(moved[2] == "third");
    CA_CHECK(v.empty());  // moving out of v left it empty

    moved.pop_back();
    CA_CHECK(moved.size() == 2);
    CA_CHECK(moved[1] == "world");
}

CA_TEST(small_vector_shrink_to_fit_returns_to_inline) {
    SmallVector<int, 4> v;
    for (int i = 0; i < 20; ++i) {
        v.push_back(i);
    }
    CA_CHECK(!is_inline(v));

    v.resize(2);
    v.shrink_to_fit();
    CA_CHECK(is_inline(v));
    CA_CHECK(v[0] == 0 && v[1] == 1);
}

CA_TEST(small_vector_swap) {
    SmallVector<int, 2> a{1, 2};
    SmallVector<int, 2> b{3, 4, 5};
    a.swap(b);
    CA_CHECK(a.size() == 3 && a[2] == 5);
    CA_CHECK(b.size() == 2 && b[0] == 1);
}

CA_TEST_MAIN()
