// ca::graphics::DisplayList / DisplayListRecorder tests.

#include "calcium/graphics/display_list.hpp"

#include <cstring>

#include "calcium_test.hpp"

using ca::graphics::Color;
using ca::graphics::Command;
using ca::graphics::DisplayList;
using ca::graphics::DisplayListRecorder;
using ca::graphics::Paint;
using ca::geometry::AffineTransform;
using ca::geometry::Point;
using ca::geometry::Rect;
using ca::geometry::RoundedRectangle;

CA_TEST(display_list_empty_seal) {
    DisplayListRecorder recorder;
    const DisplayList list = recorder.seal();
    CA_CHECK(list.is_empty());
    CA_CHECK(list.record_count() == 0);
    CA_CHECK(list.paint_count() == 0);
}

CA_TEST(display_list_records_fills_with_paints) {
    DisplayListRecorder recorder;
    const Rect rect{10.0f, 20.0f, 30.0f, 40.0f};
    recorder.fill_rect(rect, Paint::solid_color(Color::srgb(1.0f, 0.0f, 0.0f)));
    recorder.fill_rect(Rect{1.0f, 2.0f, 3.0f, 4.0f},
                       Paint::solid_color(Color::srgb(0.0f, 1.0f, 0.0f)));
    const DisplayList list = recorder.seal();

    CA_CHECK(list.record_count() == 2);
    CA_CHECK(list.paint_count() == 2);
    CA_CHECK(list.command_at(0) == Command::fill_rect);
    CA_CHECK(list.command_at(1) == Command::fill_rect);
    CA_CHECK(list.paint_index_at(0) == 0);
    CA_CHECK(list.paint_index_at(1) == 1);

    Rect read_back{};
    std::memcpy(&read_back, list.payload_at(0).data(), sizeof(read_back));
    CA_CHECK(read_back == rect);
}

CA_TEST(display_list_paints_are_interned) {
    DisplayListRecorder recorder;
    const Paint red = Paint::solid_color(Color::srgb(1.0f, 0.0f, 0.0f));
    recorder.fill_rect(Rect{0, 0, 1, 1}, red);
    recorder.fill_rect(Rect{0, 0, 2, 2}, red);
    recorder.fill_rect(Rect{0, 0, 3, 3},
                       Paint::solid_color(Color::srgb(0.0f, 0.0f, 1.0f)));
    const DisplayList list = recorder.seal();

    CA_CHECK(list.paint_count() == 2);
    CA_CHECK(list.paint_index_at(0) == 0);
    CA_CHECK(list.paint_index_at(1) == 0);
    CA_CHECK(list.paint_index_at(2) == 1);
    CA_CHECK(list.paint_at(0) == Color::srgb(1.0f, 0.0f, 0.0f));
    CA_CHECK(list.paint_at(1) == Color::srgb(0.0f, 0.0f, 1.0f));
}

CA_TEST(display_list_rounded_rectangle_payload_round_trips) {
    DisplayListRecorder recorder;
    const RoundedRectangle rounded =
        RoundedRectangle::uniform(Rect{0, 0, 100, 50}, 12.0f);
    recorder.fill_rounded_rectangle(rounded,
                                    Paint::solid_color(Color::srgb(0.5f, 0.5f, 0.5f)));
    const DisplayList list = recorder.seal();

    CA_CHECK(list.record_count() == 1);
    CA_CHECK(list.command_at(0) == Command::fill_rounded_rectangle);

    RoundedRectangle read_back{};
    std::memcpy(&read_back, list.payload_at(0).data(), sizeof(read_back));
    CA_CHECK(read_back == rounded);
}

CA_TEST(display_list_state_commands_have_no_paint) {
    DisplayListRecorder recorder;
    recorder.save_state();
    recorder.concatenate_transform(AffineTransform::make_translation(5.0f, 6.0f));
    recorder.clip_to_rect(Rect{0, 0, 50, 50});
    recorder.restore_state();
    const DisplayList list = recorder.seal();

    CA_CHECK(list.record_count() == 4);
    CA_CHECK(list.command_at(0) == Command::save_state);
    CA_CHECK(list.command_at(1) == Command::concat_transform);
    CA_CHECK(list.command_at(2) == Command::clip_rect);
    CA_CHECK(list.command_at(3) == Command::restore_state);
    CA_CHECK(list.paint_index_at(0) == 0xFF);
    CA_CHECK(list.paint_index_at(1) == 0xFF);
    CA_CHECK(list.paint_index_at(2) == 0xFF);
    CA_CHECK(list.paint_count() == 0);

    AffineTransform transform{};
    std::memcpy(&transform, list.payload_at(1).data(), sizeof(transform));
    CA_CHECK(transform == AffineTransform::make_translation(5.0f, 6.0f));

    Rect clip{};
    std::memcpy(&clip, list.payload_at(2).data(), sizeof(clip));
    CA_CHECK(clip == Rect{0, 0, 50, 50});
}

CA_TEST(display_list_global_alpha_folds_into_paints) {
    DisplayListRecorder recorder;
    const Paint solid = Paint::solid_color(Color::srgb(1.0f, 0.0f, 0.0f, 1.0f));
    recorder.fill_rect(Rect{0, 0, 1, 1}, solid);
    recorder.set_global_alpha(0.5f);
    recorder.fill_rect(Rect{0, 0, 2, 2}, solid);
    recorder.save_state();
    recorder.set_global_alpha(0.25f);
    recorder.fill_rect(Rect{0, 0, 3, 3}, solid);
    recorder.restore_state();
    recorder.fill_rect(Rect{0, 0, 4, 4}, solid);
    const DisplayList list = recorder.seal();

    CA_CHECK(list.paint_count() == 3);
    CA_CHECK(list.paint_at(0) == Color::srgb(1.0f, 0.0f, 0.0f, 1.0f));
    CA_CHECK(list.paint_at(1) == Color::srgb(1.0f, 0.0f, 0.0f, 0.5f));
    CA_CHECK(list.paint_at(2) == Color::srgb(1.0f, 0.0f, 0.0f, 0.25f));
    // save/restore are records too: 0=fill, 1=fill, 2=save, 3=fill,
    // 4=restore, 5=fill — the last fill is back at the 0.5 paint.
    CA_CHECK(list.paint_index_at(5) == 1);
}

CA_TEST(display_list_is_immutable_and_compares_deeply) {
    DisplayListRecorder recorder;
    recorder.fill_rect(Rect{0, 0, 10, 10},
                       Paint::solid_color(Color::srgb(1.0f, 1.0f, 1.0f)));
    const DisplayList list = recorder.seal();

    // A second identical recording compares equal (deep), and the recorder
    // reset after seal — recording again starts from an empty list.
    DisplayListRecorder recorder2;
    recorder2.fill_rect(Rect{0, 0, 10, 10},
                        Paint::solid_color(Color::srgb(1.0f, 1.0f, 1.0f)));
    const DisplayList list2 = recorder2.seal();
    CA_CHECK(list == list2);

    DisplayListRecorder recorder3;
    recorder3.fill_rect(Rect{0, 0, 10, 10},
                        Paint::solid_color(Color::srgb(0.5f, 1.0f, 1.0f)));
    CA_CHECK(!(list == recorder3.seal()));

    const DisplayList copied = list;  // shared-ownership copy
    CA_CHECK(copied == list);
}

CA_TEST(display_list_paint_with_alpha_method) {
    const Paint solid = Paint::solid_color(Color::srgb(0.5f, 0.5f, 0.5f, 1.0f));
    Paint faded = solid;
    faded.with_alpha_multiplied_by(0.5f);
    CA_CHECK(faded.color() == Color::srgb(0.5f, 0.5f, 0.5f, 0.5f));
}

CA_TEST_MAIN()
