#include "calcium/animation/transaction.hpp"

#include <utility>
#include <vector>

#include "calcium/animation/animation_coordinator.hpp"
#include "calcium/core/thread_affinity.hpp"

namespace ca::animation {

namespace {

// The ambient transaction stack, thread-local: set_value inside an animate
// block reads the innermost transaction's motion.
struct AmbientStack {
    std::vector<Transaction*> transactions;
};

AmbientStack& ambient_stack() {
    static thread_local AmbientStack stack;
    return stack;
}

} // namespace

Transaction Transaction::begin() {
    return Transaction::begin_with_motion(Motion::standard());
}

Transaction Transaction::begin_with_motion(const Motion& motion) {
    // The coordinator for completion tracking is looked up lazily; a
    // transaction is valid even before a coordinator exists (it simply has
    // no completions to track).
    return Transaction{nullptr, motion};
}

Transaction& Transaction::current() {
    AmbientStack& stack = ambient_stack();
    if (!stack.transactions.empty()) {
        return *stack.transactions.back();
    }
    // A fresh ambient with the framework default; nobody owns it, so it is
    // returned by value into a thread-local static.
    static thread_local Transaction ambient{nullptr, Motion::standard()};
    return ambient;
}

Transaction::Transaction(AnimationCoordinator* coordinator, Motion default_motion)
    : coordinator_(coordinator), default_motion_(default_motion) {
    ambient_stack().transactions.push_back(this);
}

Transaction::Transaction(Transaction&& other) noexcept
    : coordinator_(other.coordinator_),
      default_motion_(other.default_motion_),
      disables_animation_(other.disables_animation_),
      committed_(other.committed_) {
    // The moved-from object leaves the stack; the new object takes its place
    // only if the source was the innermost (it always is — transactions are
    // stack-ordered by construction).
    other.coordinator_ = nullptr;
    other.committed_ = true;
    if (ambient_stack().transactions.back() == &other) {
        ambient_stack().transactions.back() = this;
    }
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        commit();
        coordinator_ = other.coordinator_;
        default_motion_ = other.default_motion_;
        disables_animation_ = other.disables_animation_;
        committed_ = other.committed_;
        other.coordinator_ = nullptr;
        other.committed_ = true;
        if (ambient_stack().transactions.back() == &other) {
            ambient_stack().transactions.back() = this;
        }
    }
    return *this;
}

void Transaction::add_completion_handler(std::function<void()> handler) {
    CA_ASSERT_UI_THREAD();
    completion_handler_ = std::move(handler);
}

void Transaction::note_touched(AnimationCoordinator& coordinator,
                               std::uint32_t index) {
    touched_.emplace_back(&coordinator, index);
}

void Transaction::commit() {
    if (committed_) {
        return;
    }
    committed_ = true;

    // Completion: the handler fires (on the UI thread, via
    // dispatch_rest_callbacks) once every touched property reaches rest.
    if (completion_handler_ && !touched_.empty()) {
        std::vector<std::uint32_t> indices;
        indices.reserve(touched_.size());
        AnimationCoordinator* coordinator = touched_.front().first;
        for (const auto& [owner, index] : touched_) {
            indices.push_back(index);
            coordinator = owner;  // all touches share one coordinator in practice
        }
        coordinator->register_completion(std::move(indices),
                                         std::move(completion_handler_));
    }
    completion_handler_ = nullptr;

    AmbientStack& stack = ambient_stack();
    if (!stack.transactions.empty() && stack.transactions.back() == this) {
        stack.transactions.pop_back();
    }
}

Transaction::~Transaction() {
    commit();
}

void animate(const Motion& motion, const std::function<void()>& mutations) {
    Transaction transaction = Transaction::begin_with_motion(motion);
    mutations();
    transaction.commit();
}

void animate_with_completion(const Motion& motion,
                             const std::function<void()>& mutations,
                             std::function<void()> completion) {
    Transaction transaction = Transaction::begin_with_motion(motion);
    transaction.add_completion_handler(std::move(completion));
    mutations();
    transaction.commit();
}

} // namespace ca::animation
