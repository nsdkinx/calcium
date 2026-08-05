// Umbrella translation unit for the shared library.
//
// Also the single home for TWELL_IMPL once ca::animation lands (M2): Twell is a
// single-header library and exactly one translation unit must define it.
//
// The C++ API surface is the static modules (tests, examples, and the
// compositor link them directly; `library_version` lives in
// src/core/version.cpp); the shared library exists for the C ABI
// (docs/04-public-api.md §7), which lands at M7. Backend registration lives in
// src/backends/backend_registration.cpp so it follows the static link, not
// this DLL.

#include "calcium/calcium.hpp"
