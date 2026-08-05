// The single translation unit that defines Twell's implementation.
//
// Twell is a single-header library; exactly one TU per link unit must define
// TWELL_IMPL. This TU lives in the animation module, which is the only module
// that may reach Twell (P5); consumers link calcium_animation and get the
// kernel with it.

#define TWELL_IMPL
#include "twell.h"
