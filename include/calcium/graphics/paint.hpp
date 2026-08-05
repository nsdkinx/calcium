#pragma once

// Paint.
//
// M2 carries the minimal surface the display-list milestone needs: solid
// colors. The full paint — linear/radial/angular gradients, image patterns,
// custom shaders, with explicit interpolation color space — lands with the
// color pipeline (docs/04-public-api.md §3, docs/spec/color-pipeline.md).
// The API below is a subset of that design, not a placeholder: `solid_color`
// and `with_alpha_multiplied_by` keep their signatures when it lands.

#include "calcium/graphics/color.hpp"

namespace ca::graphics {

class Paint {
public:
    [[nodiscard]] static constexpr Paint solid_color(Color color) noexcept {
        return Paint{color};
    }

    [[nodiscard]] constexpr Color color() const noexcept { return color_; }

    /// Multiplies the paint's alpha; the result clamps to [0, 1].
    constexpr Paint& with_alpha_multiplied_by(float factor) noexcept {
        color_ = color_.with_alpha_multiplied_by(factor);
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Paint&) const noexcept =
        default;

private:
    constexpr explicit Paint(Color color) noexcept : color_(color) {}

    Color color_ = Color::clear();
};

} // namespace ca::graphics
