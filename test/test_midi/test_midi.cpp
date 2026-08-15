// ============================================================================
//  MIDI: byte parser, router, note priority and pitch bend.
// ============================================================================
#include <unity.h>

#include "Mocks.h"
#include "midi/MidiParser.h"
#include "midi/MidiRouter.h"
#include "midi/NoteStack.h"

using namespace ot;
using namespace ot::mock;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
void test_parser_note_on(void) {
    MidiParser parser(MidiPort::DIN);
    MidiMessage msg;
    TEST_ASSERT_FALSE(parser.parse(0x92, msg));
    TEST_ASSERT_FALSE(parser.parse(60, msg));
    TEST_ASSERT_TRUE(parser.parse(100, msg));

    TEST_ASSERT_EQUAL(MidiType::NoteOn, msg.type);
    TEST_ASSERT_EQUAL_UINT8(3, msg.channel);
    TEST_ASSERT_EQUAL_UINT8(60, msg.data1);
    TEST_ASSERT_EQUAL_UINT8(100, msg.data2);
    TEST_ASSERT_EQUAL(MidiPort::DIN, msg.source);
    TEST_ASSERT_TRUE(msg.isNoteOn());
}

void test_parser_running_status(void) {
    MidiParser parser;
    MidiMessage msg;
    parser.parse(0x90, msg);
    parser.parse(60, msg);
    TEST_ASSERT_TRUE(parser.parse(100, msg));

    // No new status byte: the next pair reuses it.
    TEST_ASSERT_FALSE(parser.parse(64, msg));
    TEST_ASSERT_TRUE(parser.parse(90, msg));
    TEST_ASSERT_EQUAL(MidiType::NoteOn, msg.type);
    TEST_ASSERT_EQUAL_UINT8(64, msg.data1);
    TEST_ASSERT_EQUAL_UINT8(90, msg.data2);
}

void test_parser_note_on_with_zero_velocity_is_a_note_off(void) {
    MidiParser parser;
    MidiMessage msg;
    parser.parse(0x90, msg);
    parser.parse(60, msg);
    TEST_ASSERT_TRUE(parser.parse(0, msg));
    TEST_ASSERT_TRUE(msg.isNoteOff());
    TEST_ASSERT_FALSE(msg.isNoteOn());
}

void test_parser_realtime_inside_a_message(void) {
    MidiParser parser;
    MidiMessage msg;
    parser.parse(0x90, msg);
    parser.parse(60, msg);
    // A clock byte may arrive between the data bytes and must not corrupt it.
    TEST_ASSERT_TRUE(parser.parse(0xF8, msg));
    TEST_ASSERT_EQUAL(MidiType::Clock, msg.type);
    TEST_ASSERT_TRUE(parser.parse(100, msg));
    TEST_ASSERT_EQUAL(MidiType::NoteOn, msg.type);
    TEST_ASSERT_EQUAL_UINT8(60, msg.data1);
    TEST_ASSERT_EQUAL_UINT8(100, msg.data2);
}

void test_parser_two_byte_messages(void) {
    MidiParser parser;
    MidiMessage msg;
    parser.parse(0xC0, msg);
    TEST_ASSERT_TRUE(parser.parse(7, msg));
    TEST_ASSERT_EQUAL(MidiType::ProgramChange, msg.type);
    TEST_ASSERT_EQUAL_UINT8(7, msg.data1);

    parser.parse(0xD3, msg);
    TEST_ASSERT_TRUE(parser.parse(64, msg));
    TEST_ASSERT_EQUAL(MidiType::ChannelPressure, msg.type);
    TEST_ASSERT_EQUAL_UINT8(4, msg.channel);
}

void test_parser_sysex(void) {
    MidiParser parser;
    MidiMessage msg;
    const uint8_t body[] = {0x7D, 0x01, 0x02, 0x03};
    TEST_ASSERT_FALSE(parser.parse(0xF0, msg));
    for (uint8_t b : body) TEST_ASSERT_FALSE(parser.parse(b, msg));
    TEST_ASSERT_TRUE(parser.parse(0xF7, msg));

    TEST_ASSERT_EQUAL(MidiType::SystemExclusive, msg.type);
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), msg.sysexLength);
    TEST_ASSERT_EQUAL_UINT8(0x7D, msg.sysex[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, msg.sysex[3]);
}

void test_parser_ignores_orphan_data(void) {
    MidiParser parser;
    MidiMessage msg;
    // Data bytes before any status byte must not produce a message.
    TEST_ASSERT_FALSE(parser.parse(60, msg));
    TEST_ASSERT_FALSE(parser.parse(100, msg));
}

void test_message_serialisation_round_trip(void) {
    MidiParser parser;
    MidiMessage in = MidiMessage::noteOn(5, 72, 88);
    uint8_t bytes[3];
    TEST_ASSERT_EQUAL_UINT8(3, in.toBytes(bytes));

    MidiMessage out;
    bool complete = false;
    for (uint8_t i = 0; i < 3; ++i) complete = parser.parse(bytes[i], out);
    TEST_ASSERT_TRUE(complete);
    TEST_ASSERT_EQUAL_UINT8(in.channel, out.channel);
    TEST_ASSERT_EQUAL_UINT8(in.data1, out.data1);
    TEST_ASSERT_EQUAL_UINT8(in.data2, out.data2);
}

// ---------------------------------------------------------------------------
// Pitch bend
// ---------------------------------------------------------------------------
void test_pitch_bend_encoding(void) {
    MidiMessage centre = MidiMessage::pitchBend(1, 0);
    TEST_ASSERT_EQUAL_INT16(0, centre.pitchBendValue());
    TEST_ASSERT_EQUAL_UINT8(0x00, centre.data1);
    TEST_ASSERT_EQUAL_UINT8(0x40, centre.data2);

    TEST_ASSERT_EQUAL_INT16(8191, MidiMessage::pitchBend(1, 8191).pitchBendValue());
    TEST_ASSERT_EQUAL_INT16(-8192, MidiMessage::pitchBend(1, -8192).pitchBendValue());
    TEST_ASSERT_EQUAL_INT16(4096, MidiMessage::pitchBend(1, 4096).pitchBendValue());
    // Out of range values are clamped, never wrapped.
    TEST_ASSERT_EQUAL_INT16(8191, MidiMessage::pitchBend(1, 30000).pitchBendValue());
    TEST_ASSERT_EQUAL_INT16(-8192, MidiMessage::pitchBend(1, -30000).pitchBendValue());
}

// ---------------------------------------------------------------------------
// Note priority
// ---------------------------------------------------------------------------
void test_note_priority_last(void) {
    NoteStack stack;
    stack.setPriority(NotePriority::LAST);
    stack.noteOn(60, 100);
    stack.noteOn(64, 100);
    TEST_ASSERT_EQUAL_UINT8(64, stack.activeNote());
    stack.noteOn(55, 100);
    TEST_ASSERT_EQUAL_UINT8(55, stack.activeNote());
    // Releasing the newest falls back to the previous one.
    stack.noteOff(55);
    TEST_ASSERT_EQUAL_UINT8(64, stack.activeNote());
    stack.noteOff(64);
    TEST_ASSERT_EQUAL_UINT8(60, stack.activeNote());
    stack.noteOff(60);
    TEST_ASSERT_FALSE(stack.hasNote());
}

void test_note_priority_high_and_low(void) {
    NoteStack high;
    high.setPriority(NotePriority::HIGHEST);
    high.noteOn(60, 100);
    high.noteOn(72, 100);
    high.noteOn(55, 100);
    TEST_ASSERT_EQUAL_UINT8(72, high.activeNote());
    high.noteOff(72);
    TEST_ASSERT_EQUAL_UINT8(60, high.activeNote());

    NoteStack low;
    low.setPriority(NotePriority::LOWEST);
    low.noteOn(60, 100);
    low.noteOn(72, 100);
    low.noteOn(55, 100);
    TEST_ASSERT_EQUAL_UINT8(55, low.activeNote());
    low.noteOff(55);
    TEST_ASSERT_EQUAL_UINT8(60, low.activeNote());
}

void test_note_stack_overflow_drops_the_oldest(void) {
    NoteStack stack;
    for (uint8_t i = 0; i < kNoteStackDepth + 4; ++i) stack.noteOn(40 + i, 100);
    TEST_ASSERT_EQUAL_UINT8(kNoteStackDepth, stack.count());
    // The newest note always wins on a monophonic instrument.
    TEST_ASSERT_EQUAL_UINT8(40 + kNoteStackDepth + 3, stack.activeNote());
    TEST_ASSERT_FALSE(stack.contains(40));
}

void test_note_off_of_an_unheld_note_is_harmless(void) {
    NoteStack stack;
    stack.noteOn(60, 100);
    TEST_ASSERT_FALSE(stack.noteOff(70));
    TEST_ASSERT_TRUE(stack.hasNote());
    TEST_ASSERT_EQUAL_UINT8(60, stack.activeNote());
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------
namespace {

struct Fixture {
    MidiConfig cfg;
    MidiRouter router;
    RecordingSink sound;
    RecordingSink valves;
    RecordingSink monitor;
    MockMidiTransport din{MidiPort::DIN};
    MockMidiTransport usb{MidiPort::USB};

    Fixture() {
        MidiRouter::makeDefaultRoutes(cfg);
        router.configure(cfg);
        router.setSink(MidiPort::SOUND_ENGINE, &sound);
        router.setSink(MidiPort::VALVE_ENGINE, &valves);
        router.setSink(MidiPort::MONITOR, &monitor);
        router.setTransport(MidiPort::DIN, &din);
        router.setTransport(MidiPort::USB, &usb);
    }

    void reconfigure() {
        router.configure(cfg);
        router.setSink(MidiPort::SOUND_ENGINE, &sound);
        router.setSink(MidiPort::VALVE_ENGINE, &valves);
        router.setSink(MidiPort::MONITOR, &monitor);
        router.setTransport(MidiPort::DIN, &din);
        router.setTransport(MidiPort::USB, &usb);
    }

    MidiRoute* route(MidiPort source, MidiPort destination) {
        for (uint8_t i = 0; i < cfg.routeCount; ++i) {
            if (cfg.routes[i].source == source && cfg.routes[i].destination == destination) {
                return &cfg.routes[i];
            }
        }
        return nullptr;
    }
};

}  // namespace

void test_router_default_routes_reach_both_engines(void) {
    Fixture f;
    f.din.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_TRUE(f.sound.sawNoteOn(60));
    TEST_ASSERT_TRUE(f.valves.sawNoteOn(60));
    TEST_ASSERT_EQUAL_UINT16(1, f.monitor.count);
}

void test_router_channel_filter(void) {
    Fixture f;
    f.cfg.globalChannelMask = 0x0001;   // channel 1 only
    f.reconfigure();

    f.din.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_TRUE(f.sound.sawNoteOn(60));

    f.sound.clear();
    f.din.receive(MidiMessage::noteOn(5, 62, 100));
    TEST_ASSERT_FALSE(f.sound.sawNoteOn(62));
}

void test_router_transpose_and_note_range(void) {
    Fixture f;
    MidiRoute* r = f.route(MidiPort::DIN, MidiPort::SOUND_ENGINE);
    TEST_ASSERT_NOT_NULL(r);
    r->transpose = 12;
    r->noteMin = 48;
    r->noteMax = 72;
    f.reconfigure();

    f.din.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_TRUE(f.sound.sawNoteOn(72));

    // Outside the route's range: dropped, and never transposed into range.
    f.sound.clear();
    f.din.receive(MidiMessage::noteOn(1, 80, 100));
    TEST_ASSERT_EQUAL_UINT16(0, f.sound.count);
}

void test_router_velocity_curves(void) {
    TEST_ASSERT_EQUAL_UINT8(64, MidiRouter::applyVelocityCurve(VelocityCurve::LINEAR, 64, 100));
    TEST_ASSERT_EQUAL_UINT8(100, MidiRouter::applyVelocityCurve(VelocityCurve::FIXED, 20, 100));
    // A note off must stay a note off whatever the curve.
    TEST_ASSERT_EQUAL_UINT8(0, MidiRouter::applyVelocityCurve(VelocityCurve::FIXED, 0, 100));

    const uint8_t soft = MidiRouter::applyVelocityCurve(VelocityCurve::SOFT, 64, 100);
    const uint8_t hard = MidiRouter::applyVelocityCurve(VelocityCurve::HARD, 64, 100);
    TEST_ASSERT_GREATER_THAN_UINT8(64, soft);
    TEST_ASSERT_LESS_THAN_UINT8(64, hard);
    // Both curves keep the extremes.
    TEST_ASSERT_EQUAL_UINT8(127, MidiRouter::applyVelocityCurve(VelocityCurve::SOFT, 127, 100));
    TEST_ASSERT_EQUAL_UINT8(127, MidiRouter::applyVelocityCurve(VelocityCurve::HARD, 127, 100));
}

void test_router_forwards_to_an_output_transport(void) {
    Fixture f;
    if (f.cfg.routeCount < kMaxRoutes) {
        MidiRoute& r = f.cfg.routes[f.cfg.routeCount++];
        r = MidiRoute();
        r.source = MidiPort::USB;
        r.destination = MidiPort::DIN;
        r.enabled = true;
    }
    f.reconfigure();

    f.usb.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_EQUAL_UINT16(1, f.din.sentCount);
    TEST_ASSERT_EQUAL_UINT8(60, f.din.sent[0].data1);
}

void test_router_respects_a_disabled_output(void) {
    Fixture f;
    if (f.cfg.routeCount < kMaxRoutes) {
        MidiRoute& r = f.cfg.routes[f.cfg.routeCount++];
        r = MidiRoute();
        r.source = MidiPort::USB;
        r.destination = MidiPort::DIN;
        r.enabled = true;
    }
    f.reconfigure();
    f.din.outEnabled = false;

    f.usb.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_EQUAL_UINT16(0, f.din.sentCount);
}

void test_router_suppresses_loops(void) {
    Fixture f;
    if (f.cfg.routeCount < kMaxRoutes) {
        MidiRoute& r = f.cfg.routes[f.cfg.routeCount++];
        r = MidiRoute();
        r.source = MidiPort::DIN;
        r.destination = MidiPort::DIN;   // straight back onto itself
        r.enabled = true;
    }
    f.cfg.suppressLoops = true;
    f.reconfigure();

    f.din.receive(MidiMessage::noteOn(1, 60, 100));
    TEST_ASSERT_EQUAL_UINT16(0, f.din.sentCount);
    TEST_ASSERT_GREATER_THAN_UINT32(0, f.router.stats().loopsSuppressed);
}

void test_router_panic_reaches_every_destination(void) {
    Fixture f;
    f.router.broadcastPanic();

    TEST_ASSERT_TRUE(f.sound.sawController(cc::AllSoundOff));
    TEST_ASSERT_TRUE(f.sound.sawController(cc::AllNotesOff));
    TEST_ASSERT_TRUE(f.sound.sawController(cc::ResetControllers));
    TEST_ASSERT_TRUE(f.valves.sawController(cc::AllNotesOff));
    // Hardware outputs receive it too, so a chained synthesiser also stops.
    TEST_ASSERT_GREATER_THAN_UINT16(0, f.din.sentCount);
}

void test_router_counts_traffic(void) {
    Fixture f;
    for (uint8_t i = 0; i < 5; ++i) f.din.receive(MidiMessage::noteOn(1, 60 + i, 100));
    TEST_ASSERT_EQUAL_UINT32(5, f.router.stats().rxTotal);
    TEST_ASSERT_EQUAL_UINT32(5, f.router.stats().rxPerPort[static_cast<uint8_t>(MidiPort::DIN)]);
    TEST_ASSERT_EQUAL_UINT32(10, f.router.stats().txTotal);   // two engines per note
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parser_note_on);
    RUN_TEST(test_parser_running_status);
    RUN_TEST(test_parser_note_on_with_zero_velocity_is_a_note_off);
    RUN_TEST(test_parser_realtime_inside_a_message);
    RUN_TEST(test_parser_two_byte_messages);
    RUN_TEST(test_parser_sysex);
    RUN_TEST(test_parser_ignores_orphan_data);
    RUN_TEST(test_message_serialisation_round_trip);
    RUN_TEST(test_pitch_bend_encoding);
    RUN_TEST(test_note_priority_last);
    RUN_TEST(test_note_priority_high_and_low);
    RUN_TEST(test_note_stack_overflow_drops_the_oldest);
    RUN_TEST(test_note_off_of_an_unheld_note_is_harmless);
    RUN_TEST(test_router_default_routes_reach_both_engines);
    RUN_TEST(test_router_channel_filter);
    RUN_TEST(test_router_transpose_and_note_range);
    RUN_TEST(test_router_velocity_curves);
    RUN_TEST(test_router_forwards_to_an_output_transport);
    RUN_TEST(test_router_respects_a_disabled_output);
    RUN_TEST(test_router_suppresses_loops);
    RUN_TEST(test_router_panic_reaches_every_destination);
    RUN_TEST(test_router_counts_traffic);
    return UNITY_END();
}
