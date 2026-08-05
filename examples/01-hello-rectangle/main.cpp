// Hello Rectangle — M0 stage.
//
// At M2 this becomes the real thing: a window, a GPU-composited rounded rect,
// and a spring that survives interruption. Today the rendering stack does not
// exist yet, so this demonstrates the parts M0 does deliver — geometry, correct
// transform interpolation, and the frame-timing recorder — and prints the
// transform values a compositor would resolve.

#include <cstdio>

#include "calcium/calcium.hpp"

namespace geo = ca::geometry;

int main() {
    const auto version = ca::library_version();
    std::printf("Calcium %u.%u.%u\n\n", version.major, version.minor, version.patch);

    // A card, 200x120, centered in an 800x600 window.
    const geo::Rect window_bounds = geo::Rect::from_xywh(0.0f, 0.0f, 800.0f, 600.0f);
    const geo::Size card_size{200.0f, 120.0f};
    const geo::Rect card_bounds =
        geo::Rect::from_center_and_size(window_bounds.center(), card_size);

    std::printf("Window : %.0f x %.0f\n",
                window_bounds.width(), window_bounds.height());
    std::printf("Card   : origin (%.1f, %.1f), size %.0f x %.0f\n\n",
                card_bounds.origin.x, card_bounds.origin.y,
                card_bounds.width(), card_bounds.height());

    // The transform the card animates between: resting, and lifted with a
    // rotation plus perspective. At M2 a Twell spring drives t; here we sample
    // it directly to show the interpolation is correct at every step.
    const geo::Transform3D resting = geo::Transform3D::identity();

    geo::Transform3D lifted =
        geo::Transform3D::make_scale(1.08, 1.08, 1.0)
            .concatenating(geo::Transform3D::make_rotation_about_axis(
                geo::Vector3{0.0, 1.0, 0.0}, 0.35))
            .concatenating(geo::Transform3D::make_translation(0.0, -24.0, 0.0));
    lifted.m34 = -1.0 / 800.0;  // perspective, as CATransform3D would express it

    std::printf("Interpolating resting -> lifted (decompose/slerp/recompose):\n");
    std::printf("    t     translate.y    scale.x   det(M)\n");

    for (int step = 0; step <= 5; ++step) {
        const double t = static_cast<double>(step) / 5.0;
        const geo::Transform3D current =
            geo::Transform3D::interpolate(resting, lifted, t);

        const auto components = current.decompose();
        if (!components.has_value()) {
            std::printf("  %.2f    <singular>\n", t);
            continue;
        }

        // det(M) should equal scale.x * scale.y * scale.z at every step, rising
        // smoothly to 1.08^2 = 1.1664. The rotation contributes nothing to it,
        // which is the signal that it stays a real rotation throughout rather
        // than a collapsing matrix — the failure mode of lerping 4x4 elements.
        std::printf("  %.2f      %8.3f     %6.4f   %6.4f\n",
                    t, components->translation.y, components->scale.x,
                    current.determinant());
    }

    // Where the card's top-leading corner lands at full lift.
    const geo::Point projected = lifted.apply_to_point(card_bounds.origin);
    std::printf("\nCard origin projects to (%.2f, %.2f) at full lift.\n",
                projected.x, projected.y);

    // The frame budget this framework is built to hold.
    ca::core::FrameTimingRecorder recorder;
    recorder.set_frame_budget(ca::core::Duration::from_hertz(120.0));
    std::printf("Frame budget at 120 Hz: %.3f ms\n",
                recorder.frame_budget().milliseconds());

    return 0;
}
