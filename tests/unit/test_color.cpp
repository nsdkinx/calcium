// ca::graphics::Color tests.

#include "calcium/graphics/color.hpp"

#include "calcium_test.hpp"

using ca::graphics::Color;

// Compile-time construction (docs/03 section 5: constexpr all geometry/colors).
static_assert(Color::srgb(1.0f, 0.0f, 0.0f) == Color{1.0f, 0.0f, 0.0f, 1.0f});
static_assert(Color::from_hex(0xFFFFFFFF) == Color::white());
static_assert(Color::from_hex(0x00000000) == Color::clear());

CA_TEST(color_from_hex) {
    CA_CHECK(Color::from_hex(0xFF3366CC) == Color::srgb(0x33 / 255.0f,
                                                        0x66 / 255.0f,
                                                        0xCC / 255.0f));
    CA_CHECK(Color::from_hex(0x80FF0000).alpha == 0x80 / 255.0f);
}

CA_TEST(color_alpha_multiply_clamps) {
    const Color color = Color::srgb(0.5f, 0.25f, 0.125f, 0.5f);
    CA_CHECK(color.with_alpha_multiplied_by(0.5f) ==
             Color::srgb(0.5f, 0.25f, 0.125f, 0.25f));
    CA_CHECK(color.with_alpha_multiplied_by(3.0f) ==
             Color::srgb(0.5f, 0.25f, 0.125f, 1.0f));
    CA_CHECK(color.with_alpha_multiplied_by(-1.0f) ==
             Color::srgb(0.5f, 0.25f, 0.125f, 0.0f));
}

CA_TEST(color_premultiplied) {
    const Color color = Color::srgb(0.8f, 0.6f, 0.4f, 0.5f);
    const Color premultiplied = color.premultiplied();
    CA_CHECK_NEAR(premultiplied.red, 0.4, 1e-6);
    CA_CHECK_NEAR(premultiplied.green, 0.3, 1e-6);
    CA_CHECK_NEAR(premultiplied.blue, 0.2, 1e-6);
    CA_CHECK_NEAR(premultiplied.alpha, 0.5, 1e-6);
}

CA_TEST_MAIN()
