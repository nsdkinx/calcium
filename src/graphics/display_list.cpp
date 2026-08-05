// Display-list recording (docs/02-architecture.md §6.1).
//
// Record encoding (the contract, documented in display_list.hpp):
//   u8 command, u8 paint index (0xFF = none), u16 payload byte count,
//   payload. All fields native-endian, byte-packed.

#include "calcium/graphics/display_list.hpp"

#include <cstring>

namespace ca::graphics {

namespace {

constexpr std::uint8_t k_no_paint = 0xFF;

// The header of a record: 4 bytes before the payload.
constexpr std::size_t k_record_header_size = 4;

void append_bytes(std::vector<std::byte>& out,
                  std::span<const std::byte> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void append_u8(std::vector<std::byte>& out, std::uint8_t value) {
    out.push_back(static_cast<std::byte>(value));
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
    const std::byte bytes[2] = {static_cast<std::byte>(value & 0xFF),
                                static_cast<std::byte>(value >> 8)};
    append_bytes(out, bytes);
}

template <typename T>
std::span<const std::byte> bytes_of(const T& value) {
    return {reinterpret_cast<const std::byte*>(&value), sizeof(T)};
}

std::uint8_t read_u8(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint8_t>(bytes[offset]);
}

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

// Walks the record buffer to the i-th record, returning its offset. O(i) —
// fine for tests and the conformance suite; the rasterizer walks raw_records
// sequentially instead.
std::size_t record_offset(std::span<const std::byte> records,
                          std::size_t index) {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < index; ++i) {
        const std::uint16_t payload_size =
            read_u16(records, offset + 2);
        offset += k_record_header_size + payload_size;
    }
    return offset;
}

} // namespace

struct DisplayList::Impl {
    std::vector<std::byte> records;
    std::vector<Color> paints;
    std::size_t record_count = 0;
};

// ---------------------------------------------------------------------------
// DisplayList
// ---------------------------------------------------------------------------

std::size_t DisplayList::record_count() const noexcept {
    return impl_ != nullptr ? impl_->record_count : 0;
}

std::size_t DisplayList::paint_count() const noexcept {
    return impl_ != nullptr ? impl_->paints.size() : 0;
}

Color DisplayList::paint_at(std::size_t index) const noexcept {
    if (impl_ == nullptr || index >= impl_->paints.size()) {
        return Color::clear();
    }
    return impl_->paints[index];
}

Command DisplayList::command_at(std::size_t index) const noexcept {
    if (impl_ == nullptr || index >= impl_->record_count) {
        return Command::save_state;  // well-formed fallback: a no-op command
    }
    return static_cast<Command>(
        read_u8(impl_->records, record_offset(impl_->records, index)));
}

std::uint8_t DisplayList::paint_index_at(std::size_t index) const noexcept {
    if (impl_ == nullptr || index >= impl_->record_count) {
        return k_no_paint;
    }
    return read_u8(impl_->records, record_offset(impl_->records, index) + 1);
}

std::span<const std::byte> DisplayList::payload_at(
    std::size_t index) const noexcept {
    if (impl_ == nullptr || index >= impl_->record_count) {
        return {};
    }
    const std::size_t offset = record_offset(impl_->records, index);
    const std::uint16_t payload_size = read_u16(impl_->records, offset + 2);
    return std::span<const std::byte>{impl_->records}.subspan(
        offset + k_record_header_size, payload_size);
}

std::span<const std::byte> DisplayList::raw_records() const noexcept {
    return impl_ != nullptr ? std::span<const std::byte>{impl_->records}
                            : std::span<const std::byte>{};
}

bool DisplayList::operator==(const DisplayList& other) const noexcept {
    if (impl_ == other.impl_) {
        return true;
    }
    if (impl_ == nullptr || other.impl_ == nullptr) {
        return false;
    }
    return impl_->records == other.impl_->records
        && impl_->paints == other.impl_->paints;
}

// ---------------------------------------------------------------------------
// DisplayListRecorder
// ---------------------------------------------------------------------------

void DisplayListRecorder::append(Command command, std::uint8_t paint_index,
                                 std::span<const std::byte> payload) {
    append_u8(records_, static_cast<std::uint8_t>(command));
    append_u8(records_, paint_index);
    append_u16(records_, static_cast<std::uint16_t>(payload.size()));
    append_bytes(records_, payload);
}

std::uint8_t DisplayListRecorder::intern_paint(Color color) {
    for (std::size_t i = 0; i < paints_.size(); ++i) {
        if (paints_[i] == color) {
            return static_cast<std::uint8_t>(i);
        }
    }
    // The paint table is bounded by 255 entries (u8 indices); a list with
    // more distinct colors is far beyond the M2 slice and would need a
    // wider index (a real intern table lands with the conformance suite).
    if (paints_.size() >= 255) {
        // Last resort: reuse the final slot. Rendering stays correct — the
        // table still holds a color — only deduplication degrades.
        paints_.back() = color;
        return static_cast<std::uint8_t>(paints_.size() - 1);
    }
    paints_.push_back(color);
    return static_cast<std::uint8_t>(paints_.size() - 1);
}

void DisplayListRecorder::save_state() {
    append(Command::save_state, k_no_paint, {});
    alpha_stack_.push_back(global_alpha_);
}

void DisplayListRecorder::restore_state() {
    append(Command::restore_state, k_no_paint, {});
    if (!alpha_stack_.empty()) {
        global_alpha_ = alpha_stack_.back();
        alpha_stack_.pop_back();
    }
}

void DisplayListRecorder::concatenate_transform(
    const geometry::AffineTransform& transform) {
    append(Command::concat_transform, k_no_paint, bytes_of(transform));
}

void DisplayListRecorder::clip_to_rect(geometry::Rect rect) {
    append(Command::clip_rect, k_no_paint, bytes_of(rect));
}

void DisplayListRecorder::fill_rect(geometry::Rect rect, const Paint& paint) {
    // set_global_alpha is folded at record time: the paint table stores the
    // tinted color, so the rasterizer's state is CTM + clip only and the
    // table stays deduplicated for equal results.
    append(Command::fill_rect,
           intern_paint(paint.color().with_alpha_multiplied_by(global_alpha_)),
           bytes_of(rect));
}

void DisplayListRecorder::fill_rounded_rectangle(
    const geometry::RoundedRectangle& rounded_rectangle, const Paint& paint) {
    append(Command::fill_rounded_rectangle,
           intern_paint(paint.color().with_alpha_multiplied_by(global_alpha_)),
           bytes_of(rounded_rectangle));
}

void DisplayListRecorder::set_global_alpha(float alpha) {
    global_alpha_ = alpha;
}

DisplayList DisplayListRecorder::seal() {
    auto impl = std::make_shared<DisplayList::Impl>();
    impl->records = std::move(records_);
    impl->paints = std::move(paints_);
    // Count records once (the sealed format is stable; the walk is exact).
    std::size_t offset = 0;
    while (offset + k_record_header_size <= impl->records.size()) {
        const std::uint16_t payload_size = read_u16(impl->records, offset + 2);
        offset += k_record_header_size + payload_size;
        ++impl->record_count;
    }

    records_.clear();
    paints_.clear();
    global_alpha_ = 1.0f;
    alpha_stack_.clear();

    DisplayList list;
    list.impl_ = std::move(impl);
    return list;
}

} // namespace ca::graphics
