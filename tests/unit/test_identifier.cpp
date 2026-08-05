// ca::core::Identifier tests: stable identity (P12).

#include "calcium/core/identifier.hpp"

#include <unordered_set>

#include "calcium_test.hpp"

using ca::core::Identifier;

CA_TEST(identifier_default_is_invalid) {
    const Identifier id;
    CA_CHECK(!id.is_valid());
    CA_CHECK(id.to_bits() == 0);
}

CA_TEST(identifier_generate_is_unique_and_valid) {
    std::unordered_set<uint64_t> seen;
    for (int i = 0; i < 10'000; ++i) {
        const Identifier id = Identifier::generate();
        CA_CHECK(id.is_valid());
        seen.insert(id.to_bits());
    }
    CA_CHECK(seen.size() == 10'000);
}

CA_TEST(identifier_never_returns_zero) {
    // 0 is reserved for the invalid identifier; generate() must skip it even
    // if the counter ever wraps (mirrors HandlePool's generation rule).
    for (int i = 0; i < 1'000; ++i) {
        CA_CHECK(Identifier::generate().to_bits() != 0);
    }
}

CA_TEST(identifier_bits_round_trip) {
    const Identifier id = Identifier::generate();
    CA_CHECK(Identifier::from_bits(id.to_bits()) == id);
}

CA_TEST(identifier_is_comparable_and_hashable) {
    const Identifier a = Identifier::generate();
    const Identifier b = Identifier::generate();
    CA_CHECK(a != b);
    CA_CHECK(a == Identifier::from_bits(a.to_bits()));

    // Usable as a map key (ListView recycling, matched-geometry transitions).
    std::unordered_map<Identifier, int> map;
    map[a] = 42;
    CA_CHECK(map.at(a) == 42);
    CA_CHECK(map.find(b) == map.end());
}

CA_TEST_MAIN()
