#pragma once

// Symbol visibility.
//
// Calcium builds with hidden default visibility (`CMAKE_CXX_VISIBILITY_PRESET
// hidden`, and MSVC's implicit equivalent), so the exported surface is opt-in
// rather than opt-out. That is not just hygiene: P13 says the C API is the ABI,
// and an ABI you did not deliberately choose is one you cannot keep stable.
//
// Annotate anything crossing the shared-library boundary with `CALCIUM_API`.
// Header-only and static-module code does not need it.

#if defined(_WIN32)
#  if defined(CALCIUM_BUILDING_SHARED)
#    define CALCIUM_API __declspec(dllexport)
#  elif defined(CALCIUM_USING_SHARED)
#    define CALCIUM_API __declspec(dllimport)
#  else
#    define CALCIUM_API
#  endif
#else
#  if defined(CALCIUM_BUILDING_SHARED) || defined(CALCIUM_USING_SHARED)
#    define CALCIUM_API __attribute__((visibility("default")))
#  else
#    define CALCIUM_API
#  endif
#endif
