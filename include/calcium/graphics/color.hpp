#pragma once

// Color.
//
// M1 carries the minimal surface the frame pipeline needs: sRGB floats and a
// premultiplied-alpha conversion for the GPU. The full color pipeline — color
// spaces, Oklab interpolation, gamma-correct blending — lands with the
// display-list milestone (docs/03-project-structure.md §2, docs/spec/color-
// pipeline.md). The API below is a subset of that design, not a placeholder:
// `srgb`, `from_hex`, and `with_alpha_multiplied_by` keep their signatures
// when the pipeline lands.
//
// Components are non-premultiplied sRGB, each in [0, 1].

#include <cstdint>

namespace ca::graphics {

/// sRGB color, non-premultiplied, each channel in [0, 1].
struct Color {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;

    [[nodiscard]] static constexpr Color srgb(float r, float g, float b,
                                              float a = 1.0f) noexcept {
        return {r, g, b, a};
    }

    /// 0xAARRGGBB, e.g. `from_hex(0xFF3366CC)`.
    [[nodiscard]] static constexpr Color from_hex(std::uint32_t argb) noexcept {
        return {((argb >> 16) & 0xFF) / 255.0f,
                ((argb >> 8) & 0xFF) / 255.0f,
                (argb & 0xFF) / 255.0f,
                (argb >> 24) / 255.0f};
    }

    [[nodiscard]] static constexpr Color black() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color white() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color clear() noexcept { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    /// The color with alpha multiplied by `factor`; the result clamps to [0, 1].
    [[nodiscard]] constexpr Color with_alpha_multiplied_by(float factor) const noexcept {
        const float multiplied = alpha * factor;
        return {red, green, blue,
                multiplied < 0.0f ? 0.0f : multiplied > 1.0f ? 1.0f : multiplied};
    }

    /// Straight-alpha → premultiplied. The GPU blends in premultiplied space
    /// (docs/00-overview.md §4.4); doing it here once beats per-pixel work.
    [[nodiscard]] constexpr Color premultiplied() const noexcept {
        return {red * alpha, green * alpha, blue * alpha, alpha};
    }

    [[nodiscard]] constexpr bool operator==(const Color&) const noexcept = default;
};

} // namespace ca::graphics
