#include "calcium/platform/display.hpp"

#include "display_timing.hpp"

namespace ca::platform {

core::Timestamp Display::predicted_presentation_time() const noexcept {
    return timing_ != nullptr ? timing_->predicted_presentation_time()
                              : core::Timestamp::now();
}

} // namespace ca::platform
