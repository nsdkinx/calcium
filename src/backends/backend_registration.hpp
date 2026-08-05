// Backend registration (internal).
//
// Static libraries only pull in objects that resolve an undefined symbol, so a
// self-registering backend object would silently never be linked. The
// compositor — the framework component that actually consumes both backends —
// calls `calcium_register_backends()` at startup, which pulls the registration
// object (and through it the backends) into the application's link.
//
// The C-ABI bootstrap (M7) will own this call; until then the compositor is
// its home.

#pragma once

namespace ca {

/// Registers the compiled-in platform and GPU backends. Safe to call more than
/// once. Called by the compositor at startup.
void calcium_register_backends();

} // namespace ca
