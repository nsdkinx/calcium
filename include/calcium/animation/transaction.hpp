#pragma once

// Transactions.
//
// P3: writes to animatable properties are batched. A transaction establishes
// the ambient motion (the default for `set_value` calls inside it) and the
// compositor applies the transaction's intents contiguously, in commit order,
// at the next presentation timestamp — so an animate block reads as one
// atomic change of state, and the springs inside it all start from the same
// velocity field.
//
// The ambient transaction is thread-local; application code never passes it
// around.

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "calcium/animation/motion.hpp"
#include "calcium/core/result.hpp"

namespace ca::animation {

class AnimationCoordinator;

class Transaction {
public:
    /// Begins a new ambient transaction, or returns the current one if a
    /// transaction is already open (nested transactions share the ambient).
    [[nodiscard]] static Transaction begin();
    [[nodiscard]] static Transaction begin_with_motion(const Motion& motion);

    /// The innermost open transaction; a fresh one with the default motion
    /// when none is open (so `set_value` always has an ambient).
    [[nodiscard]] static Transaction& current();

    /// The ambient motion: what `AnimatableProperty::set_value(value)` uses
    /// when no explicit motion is given.
    [[nodiscard]] const Motion& default_motion() const noexcept {
        return default_motion_;
    }
    void set_default_motion(const Motion& motion) { default_motion_ = motion; }

    /// `true`: set_value inside this transaction snaps instead of animating.
    void set_disables_animation(bool disables) { disables_animation_ = disables; }
    [[nodiscard]] bool disables_animation() const noexcept {
        return disables_animation_;
    }

    /// Registers a completion handler: fires once, on the UI thread, when
    /// every property touched inside this transaction reaches rest.
    void add_completion_handler(std::function<void()> handler);

    /// Called by AnimatableProperty::set_value: records the touched property
    /// so the completion waiter can be registered at commit.
    void note_touched(AnimationCoordinator& coordinator, std::uint32_t index);

    /// Applies the transaction: completion tracking takes effect. A no-op for
    /// the ambient transaction when nested; the outermost commit ends it.
    void commit();

    ~Transaction();

    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

private:
    explicit Transaction(AnimationCoordinator* coordinator, Motion default_motion);
    AnimationCoordinator* coordinator_ = nullptr;
    Motion default_motion_;
    bool disables_animation_ = false;
    bool committed_ = false;
    std::vector<std::pair<AnimationCoordinator*, std::uint32_t>> touched_;
    std::function<void()> completion_handler_;
};

/// The form nearly all application code uses:
/// `animate(Motion::standard(), [&] { card.position().set_value({400, 200}); });`
void animate(const Motion& motion, const std::function<void()>& mutations);

/// With a completion handler (fires on the UI thread when the transaction's
/// properties reach rest).
void animate_with_completion(const Motion& motion,
                             const std::function<void()>& mutations,
                             std::function<void()> completion);

} // namespace ca::animation
