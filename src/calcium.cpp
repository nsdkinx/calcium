// Umbrella translation unit for the shared library.
//
// Also the single home for TWELL_IMPL once ca::animation lands (M2): Twell is a
// single-header library and exactly one translation unit must define it.

#include "calcium/calcium.hpp"

namespace ca {

VersionInfo library_version() noexcept {
    return VersionInfo{
        .major = 0,
        .minor = 1,
        .patch = 0,
        .description = "Calcium 0.1.0",
    };
}

} // namespace ca
