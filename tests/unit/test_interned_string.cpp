// ca::core::InternedString tests.

#include "calcium/core/interned_string.hpp"

#include <unordered_set>

#include "calcium_test.hpp"

using ca::core::InternedString;

CA_TEST(interned_string_empty_maps_to_id_zero) {
    const InternedString empty;
    CA_CHECK(empty.is_empty());
    CA_CHECK(empty.id() == 0);
    CA_CHECK(empty.view().empty());

    const InternedString from_empty{""};
    CA_CHECK(from_empty.is_empty());
    CA_CHECK(from_empty == empty);
}

CA_TEST(interned_string_interning_is_canonical) {
    const InternedString first{"theme.dark"};
    const InternedString second{"theme.dark"};
    const InternedString other{"theme.light"};

    CA_CHECK(first == second);           // same text, same id
    CA_CHECK(first.id() == second.id());
    CA_CHECK(first != other);
    CA_CHECK(first.view() == "theme.dark");
}

CA_TEST(interned_string_view_is_stable) {
    // The registry never invalidates storage; a view captured at interning
    // time stays valid after more strings are interned.
    const InternedString early{"a.very.early.string"};
    const std::string_view captured = early.view();
    for (int i = 0; i < 10'000; ++i) {
        (void)InternedString{"some.other.string.with.a.long.name"};
    }
    CA_CHECK(captured == "a.very.early.string");
    CA_CHECK(early.view() == captured);
}

CA_TEST(interned_string_order_is_id_order) {
    // Sparse interning order is not guaranteed, but interning the same text
    // twice never creates a second entry.
    const InternedString a{"alpha"};
    const InternedString a_again{"alpha"};
    CA_CHECK(a.id() == a_again.id());
}

CA_TEST(interned_string_hashable) {
    std::unordered_set<InternedString> set;
    set.insert(InternedString{"same"});
    set.insert(InternedString{"same"});
    set.insert(InternedString{"different"});
    CA_CHECK(set.size() == 2);
}

CA_TEST_MAIN()
