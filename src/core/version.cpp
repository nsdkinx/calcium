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
