/* the headset's controllers as the game's controller (port/linux/src/vr_input.c): the buttons in the game and the
menus, their bindings, the menu button's short and long presses, the stick's turns, the gestures, the hands' swap
and the menus' pointer */
#include "harness.h"
#include <math.h>

#define VR_INPUT_HARNESS
#include "under_test.inc"

#define SECONDS (1.0f / 90.0f)

static int close_to(float a, float b, float tolerance) { return fabsf(a - b) <= tolerance; }

static struct vr_input_settings settings_of(int left_handed, float snap_angle, int gestures)
{
    struct vr_input_settings settings;

    settings.left_handed = left_handed;
    settings.snap_angle = snap_angle;
    settings.turn_rate = 2.0f;
    settings.gestures = gestures;
    settings.move_turn = 0;
    settings.seat_stick = 0;
    settings.room = 0;
    settings.room_offset[0] = settings.room_offset[1] = 0;
    vr_input_bindings_parse("", settings.bindings);
    return settings;
}

/* a frame with the head at the origin, looking ahead, and both hands tracked before it */
static void frame_of(struct vr_frame *frame)
{
    int hand;

    memset(frame, 0, sizeof(*frame));
    frame->stereo = frame->focused = frame->controllers = 1;
    frame->head.orientation[3] = 1;
    for (hand = 0; hand < VR_HAND_COUNT; hand++)
    {
        frame->hand[hand].tracked = 1;
        frame->hand[hand].grip.position[0] = hand == VR_HAND_LEFT ? -0.2f : 0.2f;
        frame->hand[hand].grip.position[1] = -0.35f;
        frame->hand[hand].grip.position[2] = -0.35f;
        frame->hand[hand].grip.orientation[3] = 1;
        frame->hand[hand].aim = frame->hand[hand].grip;
    }
}

/* a turn of `yaw` radians to the left */
static void yawed(float yaw, float out[4])
{
    out[0] = out[2] = 0;
    out[1] = sinf(yaw * 0.5f);
    out[3] = cosf(yaw * 0.5f);
}

static void gameplay_buttons(int left_handed)
{
    struct vr_input input;
    struct vr_input_settings settings = settings_of(left_handed, 0.5f, 0);
    struct vr_frame frame;
    struct vr_pad pad;
    int main_hand = left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT, off_hand = !main_hand;

    memset(&input, 0, sizeof(input));
    frame_of(&frame);
    frame.hand[main_hand].trigger = 1.0f;
    frame.hand[off_hand].trigger = 0.5f;
    frame.hand[main_hand].buttons = VR_BUTTON_LOWER | VR_BUTTON_UPPER | VR_BUTTON_GRIP | VR_BUTTON_STICK;
    frame.hand[off_hand].buttons = VR_BUTTON_LOWER | VR_BUTTON_UPPER | VR_BUTTON_GRIP | VR_BUTTON_STICK;
    frame.hand[off_hand].stick[0] = 0.5f;
    frame.hand[off_hand].stick[1] = -1.0f;
    vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
    CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255, "the main trigger fires (%d)", pad.analog[VR_PAD_RIGHT_TRIGGER]);
    CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 128, "the off trigger throws, as far as pulled (%d)",
        pad.analog[VR_PAD_LEFT_TRIGGER]);
    CHECK(pad.analog[VR_PAD_A] == 255 && pad.analog[VR_PAD_X] == 255 && pad.analog[VR_PAD_B] == 255,
        "the main hand's lower button jumps, its upper acts and reloads, its grip strikes");
    CHECK(pad.analog[VR_PAD_BLACK] == 255 && pad.analog[VR_PAD_Y] == 255,
        "the off hand's lower button switches grenades, its upper weapons");
    CHECK(pad.analog[VR_PAD_WHITE] == 255, "without gestures, the off grip is the flashlight");
    CHECK((pad.buttons & (VR_PAD_RIGHT_THUMB | VR_PAD_LEFT_THUMB)) == (VR_PAD_RIGHT_THUMB | VR_PAD_LEFT_THUMB),
        "the main stick's click zooms, the off stick's crouches");
    CHECK(!(pad.buttons & (VR_PAD_START | VR_PAD_BACK)), "no Start or Back");
    CHECK(pad.thumb[0] == 16383 && pad.thumb[1] == -32767, "the off stick moves (%d, %d)", pad.thumb[0], pad.thumb[1]);
    CHECK(pad.thumb[2] == 0 && pad.thumb[3] == 0, "the right stick stays still (the turn is the heading's)");

    /* the main hand alone presses nothing of the off hand's */
    frame_of(&frame);
    frame.hand[main_hand].trigger = 1.0f;
    frame.hand[main_hand].buttons = VR_BUTTON_LOWER;
    vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
    CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0 && pad.analog[VR_PAD_BLACK] == 0, "the main hand throws nothing");
    CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && pad.analog[VR_PAD_A] == 255, "but fires and jumps");
}

int main(int argc, char **argv)
{
    char const *case_name = argc > 1 ? argv[1] : "";
    struct vr_input input;
    struct vr_input_settings settings;
    struct vr_frame frame;
    struct vr_pad pad;
    int index;

    CASE("gameplay") {
        gameplay_buttons(0);
        gameplay_buttons(1);
        return 0;
    }
    CASE("move") {
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        /* the hand aims a quarter turn right of the head: ahead of the head is the game's left */
        settings.move_turn = 1.5707963f;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].stick[1] = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[0] == -32767 && abs(pad.thumb[1]) <= 1, "forward is the game's left (%d, %d)", pad.thumb[0],
            pad.thumb[1]);
        frame.hand[VR_HAND_LEFT].stick[0] = 1.0f;
        frame.hand[VR_HAND_LEFT].stick[1] = 0.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(abs(pad.thumb[0]) <= 1 && pad.thumb[1] == 32767, "right of the head is the game's forward (%d, %d)",
            pad.thumb[0], pad.thumb[1]);
        return 0;
    }
    CASE("menus") {
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_LOWER;
        frame.hand[VR_HAND_RIGHT].stick[1] = 1.0f;
        frame.hand[VR_HAND_RIGHT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_TRIGGER | VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_A] == 255 && pad.analog[VR_PAD_B] == 0, "either hand's lower button is A");
        CHECK(pad.thumb[1] == 32767, "either stick moves about the menus");
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 0 && pad.analog[VR_PAD_X] == 0 && pad.analog[VR_PAD_B] == 0,
            "no trigger, grip or action in the menus");
        CHECK(input.pointer_hand == VR_HAND_RIGHT && input.clicks == 1, "the main hand points and its trigger clicks");
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(input.clicks == 1, "a trigger held clicks once");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_UPPER | VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_B] == 255 && pad.analog[VR_PAD_A] == 0, "the upper button is B");
        CHECK(input.pointer_hand == VR_HAND_LEFT && input.clicks == 1, "the other trigger takes the pointer, unclicked");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(input.clicks == 2, "then it clicks");
        /* (the game, then a menu again: the main hand points, no clicks kept) */
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(input.pointer_hand == VR_HAND_RIGHT && input.clicks == 0, "a menu opens with the main hand, no clicks");
        return 0;
    }
    CASE("menu-button") {
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_MENU;
        for (index = 0; index < 20; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(!(pad.buttons & (VR_PAD_START | VR_PAD_BACK)), "nothing while a press may yet be long");
        }
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.buttons & VR_PAD_START, "a short press is Start, as it is let go");
        for (index = 0; index < 30 && (pad.buttons & VR_PAD_START); index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(index >= 7 && index <= 10, "held a tenth of a second for a tick to see (%d frames)", index + 1);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_MENU;
        for (index = 0; index < 120; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            if (pad.buttons & VR_PAD_BACK)
                break;
        }
        CHECK(index >= 88 && index <= 91 && !(pad.buttons & VR_PAD_START), "held a second, Back (frame %d)", index + 1);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.buttons & VR_PAD_BACK, "and Back stays while held");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!(pad.buttons & (VR_PAD_START | VR_PAD_BACK)), "let go after a long press, neither");
        return 0;
    }
    CASE("room") {
        /* room scale: the body walks after the head, in the head's axes, with hysteresis */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.room = 1;
        frame_of(&frame);
        settings.room_offset[0] = 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.room_walking && pad.thumb[0] == 0 && pad.thumb[1] == 0, "a lean of 5 cm: still");
        settings.room_offset[0] = 0.3f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.room_walking && pad.thumb[0] > 15000 && abs(pad.thumb[1]) < 100,
            "a step of 30 cm to the right: walks right (%d %d)", pad.thumb[0], pad.thumb[1]);
        settings.room_offset[0] = 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.room_walking && pad.thumb[0] > 0 && pad.thumb[0] < 8000,
            "walking, 5 cm away: slower, not stopped (%d)", pad.thumb[0]);
        settings.room_offset[0] = 0.02f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.room_walking && pad.thumb[0] == 0, "2 cm away: stops");
        settings.room_offset[0] = 0;
        settings.room_offset[1] = -0.6f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[1] == 32767 && abs(pad.thumb[0]) < 100, "60 cm ahead: walks ahead, at full (%d %d)",
            pad.thumb[0], pad.thumb[1]);
        /* the head turned a quarter to the left: a step to the room's left is ahead of it */
        yawed(1.5707963f, frame.head.orientation);
        settings.room_offset[0] = -0.6f;
        settings.room_offset[1] = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[1] > 32000 && abs(pad.thumb[0]) < 200, "turned left, a step left is ahead (%d %d)",
            pad.thumb[0], pad.thumb[1]);
        /* the off stick pushed: it moves, the step waits */
        frame.hand[VR_HAND_LEFT].stick[0] = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.room_walking && pad.thumb[0] == 32767 && pad.thumb[1] == 0, "the stick's move (%d %d)",
            pad.thumb[0], pad.thumb[1]);
        /* without room scale, nothing */
        frame.hand[VR_HAND_LEFT].stick[0] = 0;
        settings.room = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.room_walking && pad.thumb[0] == 0 && pad.thumb[1] == 0, "off: still");
        /* in the menus, never */
        settings.room = 1;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(!input.room_walking, "not in the menus");
        return 0;
    }
    CASE("seat") {
        /* in a seat the main stick is the pad's right stick, and turns nothing */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.seat_stick = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.8f;
        frame.hand[VR_HAND_RIGHT].stick[1] = -1.0f;
        frame.hand[VR_HAND_LEFT].stick[1] = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[2] == 26213 && pad.thumb[3] == -32767, "the right stick as the main (%d %d)", pad.thumb[2],
            pad.thumb[3]);
        CHECK(input.turn == 0 && !input.snapped, "no snap");
        CHECK(pad.thumb[0] == 0 && pad.thumb[1] == 32767, "the off stick the left (%d %d)", pad.thumb[0], pad.thumb[1]);
        /* out of it, the main stick turns again, and the right stick is still */
        settings.seat_stick = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[2] == 0 && pad.thumb[3] == 0, "the right stick still (%d %d)", pad.thumb[2], pad.thumb[3]);
        CHECK(close_to(input.turn, -0.5f, 1e-6f), "a snap (%f)", (double)input.turn);
        return 0;
    }
    CASE("turn") {
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.9f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(close_to(input.turn, -0.5f, 1e-6f), "a push right snaps right (%f)", (double)input.turn);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.turn == 0, "held, once");
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.5f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.9f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.turn == 0, "not let back far enough, none");
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].stick[0] = -0.8f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(close_to(input.turn, 0.5f, 1e-6f), "let back, a push left snaps left (%f)", (double)input.turn);
        frame.hand[VR_HAND_LEFT].stick[0] = 1.0f;
        frame.hand[VR_HAND_RIGHT].stick[0] = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].stick[0] = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.turn == 0, "the off stick moves, it does not turn");
        settings = settings_of(0, 0, 1);
        frame.hand[VR_HAND_RIGHT].stick[0] = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(close_to(input.turn, -2.0f * SECONDS, 1e-6f), "smoothly at the rate (%f)", (double)input.turn);
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.turn == 0, "not within the dead zone");
        frame.hand[VR_HAND_RIGHT].stick[0] = 1.0f;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        CHECK(input.turn == 0, "nor in the menus");
        return 0;
    }
    CASE("gestures") {
        int strikes = 0;

        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        /* a punch: 4.5 m/s ahead for a few frames */
        for (index = 0; index < 40; index++)
        {
            if (index >= 2 && index < 6)
                frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            strikes += pad.analog[VR_PAD_B] == 255;
            if (index == 2)
                CHECK(pad.analog[VR_PAD_B] == 255, "a punch ahead strikes at once");
        }
        CHECK(strikes >= 8 && strikes <= 10, "one strike, held a tenth of a second (%d frames)", strikes);
        /* slow, or across, or the off hand: none */
        for (index = 0; index < 20; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.02f;
            frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_B] == 0, "1.8 m/s is no punch, nor the off hand's");
        }
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[0] += 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_B] == 0, "a fast swing across is no punch");
        }
        /* turned left, a punch to the left is ahead */
        yawed(1.5707963f, frame.head.orientation);
        for (index = 0; index < 3; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[0] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        }
        CHECK(pad.analog[VR_PAD_B] == 255, "ahead is where the head faces");

        /* the flashlight: the off grip at the head */
        memset(&input, 0, sizeof(input));
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 0, "the off grip at the body is no flashlight");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.1f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.05f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 255, "the off grip at the head is");
        for (index = 0; index < 30; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 0, "once, not as long as held (a toggle)");

        /* the crouch: the head lowered from standing */
        memset(&input, 0, sizeof(input));
        frame_of(&frame);
        frame.head.position[1] = 0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.head.position[1] = -0.15f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!(pad.buttons & VR_PAD_LEFT_THUMB), "a nod is no crouch");
        frame.head.position[1] = -0.25f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.buttons & VR_PAD_LEFT_THUMB, "35 cm down from the highest is");
        frame.head.position[1] = -0.17f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.buttons & VR_PAD_LEFT_THUMB, "and stays until the head rises past the hysteresis");
        frame.head.position[1] = -0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!(pad.buttons & VR_PAD_LEFT_THUMB), "risen, not");
        frame.head.position[1] = -0.25f;
        frame.recentres = 1;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!(pad.buttons & VR_PAD_LEFT_THUMB), "a recentre takes the standing height again");

        /* without gestures: none of them */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 0);
        frame_of(&frame);
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
            frame.head.position[1] -= 0.2f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_B] == 0 && !(pad.buttons & VR_PAD_LEFT_THUMB), "no gestures");
        }
        return 0;
    }
    CASE("hands") {
        float orientation[4], forward[3];
        static float const ahead[3] = {0, 0, -1};

        /* a holster: the main grip at the right shoulder switches weapons, not a melee */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.18f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = -0.25f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = 0.05f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_Y] == 255 && pad.analog[VR_PAD_B] == 0, "the grip at the shoulder draws a weapon");
        frame.hand[VR_HAND_RIGHT].grip.position[0] = -0.18f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_Y] == 255 && pad.analog[VR_PAD_B] == 0, "held on, it stays a draw");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_B] == 255 && pad.analog[VR_PAD_Y] == 0, "before the body the grip strikes");
        /* (turned left, the right shoulder is ahead of where it was) */
        yawed(1.5707963f, frame.head.orientation);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.05f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = -0.25f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = -0.18f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_Y] == 255, "the shoulders turn with the head");

        /* both hands on the weapon */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.2f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.65f;
        CHECK(!vr_two_handed(&frame, 0, orientation), "the off grip not held: one hand");
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        CHECK(vr_two_handed(&frame, 0, orientation), "held ahead of the main hand: both");
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], 0, 1e-5f) && close_to(forward[2], -1, 1e-5f), "pointing ahead (%f, %f, %f)",
            (double)forward[0], (double)forward[1], (double)forward[2]);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.65f;
        CHECK(vr_two_handed(&frame, 0, orientation), "held ahead and to the left");
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], -0.2f / 0.3605551f, 1e-4f) && close_to(forward[2], -0.3f / 0.3605551f, 1e-4f) &&
            close_to(forward[1], 0, 1e-4f), "from the main hand to the off (%f, %f, %f)", (double)forward[0],
            (double)forward[1], (double)forward[2]);
        CHECK(close_to(orientation[0] * orientation[0] + orientation[1] * orientation[1] + orientation[2] * orientation[2] +
            orientation[3] * orientation[3], 1, 1e-4f), "a unit quaternion");
        frame.hand[VR_HAND_LEFT].grip.position[2] = 0.0f;
        CHECK(!vr_two_handed(&frame, 0, orientation), "behind the main hand: one hand");
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.1f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.1f;
        CHECK(!vr_two_handed(&frame, 0, orientation), "at the head it is the flashlight");
        return 0;
    }
    CASE("ray") {
        struct vr_pose aim, panel;
        float u = -1, v = -1;

        memset(&aim, 0, sizeof(aim));
        memset(&panel, 0, sizeof(panel));
        aim.orientation[3] = panel.orientation[3] = 1;
        panel.position[2] = -2;
        CHECK(vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v) && close_to(u, 0.5f, 1e-5f) && close_to(v, 0.5f, 1e-5f),
            "straight ahead, the middle (%f, %f)", (double)u, (double)v);
        yawed(atanf(0.6f / 2), aim.orientation);
        CHECK(vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v) && close_to(u, 0.25f, 1e-5f) && close_to(v, 0.5f, 1e-5f),
            "turned left, left of the middle (%f)", (double)u);
        memset(aim.orientation, 0, sizeof(aim.orientation));
        aim.orientation[3] = 1;
        aim.position[1] = 0.45f;
        CHECK(vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v) && close_to(v, 0.25f, 1e-5f), "higher, nearer the top (%f)",
            (double)v);
        aim.position[1] = 1.0f;
        CHECK(!vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v), "above it, not on it");
        aim.position[1] = 0;
        yawed(3.14159265f, aim.orientation);
        CHECK(!vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v), "pointing away, not");
        memset(aim.orientation, 0, sizeof(aim.orientation));
        aim.orientation[3] = 1;
        aim.position[2] = -3;
        CHECK(!vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v), "behind it, not");
        /* a panel turned to face along +x, the hand left of it pointing right */
        yawed(-1.5707963f, panel.orientation);
        panel.position[0] = 2;
        panel.position[2] = 0;
        aim.position[2] = -0.3f;
        yawed(-1.5707963f, aim.orientation);
        CHECK(vr_ray_panel(&aim, &panel, 2.4f, 1.8f, &u, &v) && close_to(v, 0.5f, 1e-5f) && close_to(u, 0.375f, 1e-5f),
            "a turned panel (%f, %f)", (double)u, (double)v);
        return 0;
    }
    CASE("bindings") {
        unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT];

        CHECK(vr_input_bindings_parse("", bindings) == 0, "nothing to read");
        CHECK(bindings[VR_ROLE_MAIN][VR_CONTROL_TRIGGER] == VR_BIND_FIRE &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_GRIP] == VR_BIND_MELEE &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_LOWER] == VR_BIND_JUMP &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_UPPER] == VR_BIND_ACTION &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_CLICK] == VR_BIND_ZOOM, "the main hand's defaults");
        CHECK(bindings[VR_ROLE_OFF][VR_CONTROL_TRIGGER] == VR_BIND_THROW_GRENADE &&
            bindings[VR_ROLE_OFF][VR_CONTROL_GRIP] == VR_BIND_FLASHLIGHT &&
            bindings[VR_ROLE_OFF][VR_CONTROL_LOWER] == VR_BIND_SWITCH_GRENADE &&
            bindings[VR_ROLE_OFF][VR_CONTROL_UPPER] == VR_BIND_SWITCH_WEAPON &&
            bindings[VR_ROLE_OFF][VR_CONTROL_CLICK] == VR_BIND_CROUCH, "the off hand's");
        CHECK(vr_input_bindings_parse(" main.lower=action,off.click=none;main.upper=jump\t", bindings) == 0,
            "three items, all understood");
        CHECK(bindings[VR_ROLE_MAIN][VR_CONTROL_LOWER] == VR_BIND_ACTION &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_UPPER] == VR_BIND_JUMP &&
            bindings[VR_ROLE_OFF][VR_CONTROL_CLICK] == VR_BIND_NONE, "each in its place");
        CHECK(bindings[VR_ROLE_MAIN][VR_CONTROL_TRIGGER] == VR_BIND_FIRE &&
            bindings[VR_ROLE_OFF][VR_CONTROL_UPPER] == VR_BIND_SWITCH_WEAPON, "the others the defaults");
        CHECK(vr_input_bindings_parse("main.lower=fly side.grip=melee main.wing=jump junk main.lower main.click=zoomed",
            bindings) == 6, "six not understood (%d)", vr_input_bindings_parse("main.lower=fly side.grip=melee "
            "main.wing=jump junk main.lower main.click=zoomed", bindings));
        CHECK(bindings[VR_ROLE_MAIN][VR_CONTROL_LOWER] == VR_BIND_JUMP &&
            bindings[VR_ROLE_MAIN][VR_CONTROL_CLICK] == VR_BIND_ZOOM, "and left out");

        /* in the game: the buttons as bound */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 0);
        vr_input_bindings_parse("main.lower=action main.upper=jump main.trigger=jump off.trigger=fire "
            "main.click=scoreboard off.grip=zoom", settings.bindings);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_LOWER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 255 && pad.analog[VR_PAD_A] == 0, "the lower button acts");
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_UPPER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_A] == 255 && pad.analog[VR_PAD_X] == 0, "the upper jumps");
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_TRIGGER;
        frame.hand[VR_HAND_RIGHT].trigger = 0.9f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_A] == 255 && pad.analog[VR_PAD_RIGHT_TRIGGER] == 0, "the main trigger jumps, firing not");
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 0.5f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_STICK;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 128 && pad.analog[VR_PAD_LEFT_TRIGGER] == 0,
            "the off trigger fires, as far as pulled (%d)", pad.analog[VR_PAD_RIGHT_TRIGGER]);
        CHECK((pad.buttons & VR_PAD_BACK) && !(pad.buttons & VR_PAD_RIGHT_THUMB), "the main click shows the scores");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK((pad.buttons & VR_PAD_RIGHT_THUMB) && pad.analog[VR_PAD_WHITE] == 0, "the off grip zooms");

        /* with the gestures, the off grip's action is the tap at the head's */
        memset(&input, 0, sizeof(input));
        settings.gestures = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!(pad.buttons & VR_PAD_RIGHT_THUMB), "the off grip away from the head does nothing");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.1f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.05f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK((pad.buttons & VR_PAD_RIGHT_THUMB) && pad.analog[VR_PAD_WHITE] == 0, "at the head it zooms");
        return 0;
    }
    CASE("swap") {
        /* the palms together, the off grip pressed: the left hand takes the weapon */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.05f;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 0, "the right hand holds it");
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 1, "the left hand takes it");
        for (index = 0; index < 10; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 1, "once a press, not while held");
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].trigger = 0.5f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && pad.analog[VR_PAD_LEFT_TRIGGER] == 128,
            "the left trigger fires now, the right throws");
        /* and back: the right hand's grip (now the off hand's) with the palms together */
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.05f;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.05f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 2, "the right hand takes it back");
        /* not with the main grip held, the hands apart, at the head, or without the gestures */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "the weapon's hand gripping keeps it");
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "the hands apart keep it");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.04f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.1f;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.04f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = -0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2 && pad.analog[VR_PAD_WHITE] == 255, "together at the head is the flashlight");
        settings.gestures = 0;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.05f;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "without the gestures, no swap");
        /* the setting changed: its hand again */
        settings.gestures = 1;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 3, "swapped again");
        settings.left_handed = 1;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed, "left-handed set: the left");
        settings.left_handed = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed, "right-handed set: the right, the swap forgotten");
        return 0;
    }
    fprintf(stderr, "no case %s\n", case_name);
    return 2;
}
