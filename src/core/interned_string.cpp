#include "calcium/core/interned_string.hpp"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ca::core {

namespace {

// The registry. `storage` owns the characters; `std::deque` guarantees that
// references into an existing element are never invalidated by growth, which
// is what makes `view()` stable for the process lifetime. `by_id` indexes the
// same views by id (slot 0 unused), so `view()` is a single vector read.
struct Registry {
    std::mutex mutex;
    std::deque<std::string> storage;
    std::vector<std::string_view> by_id{""};
    std::unordered_map<std::string_view, std::uint32_t> by_text;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

} // namespace

InternedString::InternedString(std::string_view text) noexcept {
    if (text.empty()) {
        return;  // id 0 is the empty string; nothing is stored for it
    }

    auto& r = registry();
    std::lock_guard lock{r.mutex};

    if (auto it = r.by_text.find(text); it != r.by_text.end()) {
        id_ = it->second;
        return;
    }

    // Storage is moved into the deque before the map entry is created, so the
    // string_view key outlives the insert and stays valid forever.
    r.storage.emplace_back(text);
    const auto& stable = r.storage.back();
    id_ = static_cast<std::uint32_t>(r.by_id.size());
    r.by_id.push_back(stable);
    r.by_text.emplace(stable, id_);
}

std::string_view InternedString::view() const noexcept {
    auto& r = registry();
    if (id_ == 0) {
        return {};
    }
    // A locked read: the registry only grows, so a concurrent insertion can
    // never invalidate an existing string or reallocate the vector; the mutex
    // is a memory fence that publishes the writes.
    std::lock_guard lock{r.mutex};
    return r.by_id[id_];
}

const char* InternedString::data() const noexcept {
    return view().data();
}

} // namespace ca::core
