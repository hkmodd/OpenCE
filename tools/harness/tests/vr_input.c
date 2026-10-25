/* the headset's controllers as the game's controller (port/linux/src/vr_input.c): the buttons in the game and the
menus, their bindings, the menu button's short and long presses, the stick's turns, the gestures, the hand-over of
the weapon, both hands on it (a pistol's support, a rifle's foregrip), where a hand points, the rumble in the hands
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
    settings.drive = 0;
    settings.room = 0;
    settings.room_offset[0] = settings.room_offset[1] = 0;
    settings.aim_pitch = 0;
    settings.pistol_grip = 0;
    settings.throw_gesture = 0;
    settings.bare_hands = 0;
    settings.grab_reach[VR_HAND_LEFT] = settings.grab_reach[VR_HAND_RIGHT] = 0;
    settings.physical = 0;
    settings.grip_toggle = 0;
    settings.on_foot = 1;
    settings.shoulder_full[VR_HAND_LEFT] = settings.shoulder_full[VR_HAND_RIGHT] = 0;
    settings.drop = 0;
    settings.dual = 0;
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

/* the hand at a shoulder (VR_HAND_LEFT, VR_HAND_RIGHT) of a head at the origin, looking ahead */
static void at_shoulder(struct vr_frame *frame, int hand, int side)
{
    frame->hand[hand].grip.position[0] = side == VR_HAND_LEFT ? -0.18f : 0.18f;
    frame->hand[hand].grip.position[1] = -0.25f;
    frame->hand[hand].grip.position[2] = 0.05f;
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
    CASE("drive") {
        /* driving, the off stick's sideways steers (vr_view.c): the pad's left stick has the throttle alone, and the
        main stick still turns */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.drive = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].stick[0] = 1.0f;
        frame.hand[VR_HAND_LEFT].stick[1] = 0.5f;
        frame.hand[VR_HAND_RIGHT].stick[0] = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[0] == 0 && pad.thumb[1] == 16383, "the throttle alone (%d %d)", pad.thumb[0], pad.thumb[1]);
        CHECK(close_to(input.turn, -0.5f, 1e-6f), "a snap (%f)", (double)input.turn);
        /* on foot again, the off stick strafes */
        settings.drive = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.thumb[0] == 32767, "strafing (%d)", pad.thumb[0]);
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
        /* (by the ear is a shoulder, not the flashlight) */
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.15f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = 0.05f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 0, "the off grip by the ear is no flashlight");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.02f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = -0.02f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.15f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 255, "the off grip at the visor is");
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

        /* a holster: the main grip pressed at the right shoulder is a press there (vr_view.c puts the weapon away
        or draws one), once, not a melee nor the game's switch */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.18f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = -0.25f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = 0.05f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.shoulder_grabs == 1 && input.shoulder_side == VR_HAND_RIGHT, "a press at the right shoulder (%lu, %d)",
            input.shoulder_grabs, input.shoulder_side);
        CHECK(pad.analog[VR_PAD_Y] == 0 && pad.analog[VR_PAD_B] == 0, "neither a switch nor a strike");
        frame.hand[VR_HAND_RIGHT].grip.position[0] = -0.18f;
        for (index = 0; index < 5; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.shoulder_grabs == 1 && pad.analog[VR_PAD_B] == 0, "held on, still one press, no strike (%lu)",
            input.shoulder_grabs);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.shoulder_grabs == 2 && input.shoulder_side == VR_HAND_LEFT, "a press at the left shoulder (%lu, %d)",
            input.shoulder_grabs, input.shoulder_side);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_B] == 255 && input.shoulder_grabs == 2, "before the body the grip strikes");
        /* (turned left, the right shoulder is ahead of where it was) */
        yawed(1.5707963f, frame.head.orientation);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.05f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = -0.25f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = -0.18f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.shoulder_grabs == 3 && input.shoulder_side == VR_HAND_RIGHT, "the shoulders turn with the head");
        /* where a player's hand let go over the shoulder, in the headset: by the ear, from before it to behind
        it, a little above and below the eyes; not before the chest, where the weapon is held */
        {
            static float const ears[][3] = {{0.15f, 0.0f, 0.0f}, {0.21f, 0.06f, 0.13f}, {0.12f, -0.05f, -0.09f},
                {-0.18f, 0.02f, 0.05f}};
            static float const chest[][3] = {{0.14f, -0.32f, -0.21f}, {0.14f, -0.2f, -0.25f}};
            unsigned long grabs;

            frame.head.orientation[1] = 0;
            frame.head.orientation[3] = 1;
            for (index = 0; index < 4; index++)
            {
                frame.hand[VR_HAND_RIGHT].buttons = 0;
                vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
                memcpy(frame.hand[VR_HAND_RIGHT].grip.position, ears[index], sizeof(ears[index]));
                frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
                grabs = input.shoulder_grabs;
                vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
                CHECK(input.shoulder_grabs == grabs + 1 && input.shoulder_side == (ears[index][0] > 0 ? VR_HAND_RIGHT :
                    VR_HAND_LEFT), "by the ear is a shoulder (%d: %lu, %d)", index, input.shoulder_grabs,
                    input.shoulder_side);
            }
            for (index = 0; index < 2; index++)
            {
                frame.hand[VR_HAND_RIGHT].buttons = 0;
                vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
                memcpy(frame.hand[VR_HAND_RIGHT].grip.position, chest[index], sizeof(chest[index]));
                frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
                grabs = input.shoulder_grabs;
                vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
                CHECK(input.shoulder_grabs == grabs, "before the chest is none (%d: %lu)", index, input.shoulder_grabs);
            }
            frame.hand[VR_HAND_RIGHT].buttons = 0;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        }

        /* both hands on the weapon */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.2f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.65f;
        CHECK(!vr_two_handed(&frame, 0, 0, 0.0f, orientation), "the off grip not held: one hand");
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        CHECK(vr_two_handed(&frame, 0, 0, 0.0f, orientation) == VR_HELD_BOTH, "held ahead of the main hand: both");
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], 0, 1e-5f) && close_to(forward[2], -1, 1e-5f), "pointing ahead (%f, %f, %f)",
            (double)forward[0], (double)forward[1], (double)forward[2]);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.65f;
        CHECK(vr_two_handed(&frame, 0, 0, 0.0f, orientation), "held ahead and to the left");
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], -0.2f / 0.3605551f, 1e-4f) && close_to(forward[2], -0.3f / 0.3605551f, 1e-4f) &&
            close_to(forward[1], 0, 1e-4f), "from the main hand to the off (%f, %f, %f)", (double)forward[0],
            (double)forward[1], (double)forward[2]);
        CHECK(close_to(orientation[0] * orientation[0] + orientation[1] * orientation[1] + orientation[2] * orientation[2] +
            orientation[3] * orientation[3], 1, 1e-4f), "a unit quaternion");
        frame.hand[VR_HAND_LEFT].grip.position[2] = 0.0f;
        CHECK(!vr_two_handed(&frame, 0, 0, 0.0f, orientation), "behind the main hand: one hand");
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.1f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.1f;
        CHECK(!vr_two_handed(&frame, 0, 0, 0.0f, orientation), "at the head it is the flashlight");

        /* at the main hand the off supports it: the weapon points as the main hand does */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.15f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = -0.40f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        yawed(0.3f, frame.hand[VR_HAND_RIGHT].aim.orientation);
        CHECK(vr_two_handed(&frame, 0, 0, 0.0f, orientation) == VR_HELD_SUPPORT, "at the main hand it supports it");
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], -sinf(0.3f), 1e-4f) && close_to(forward[2], -cosf(0.3f), 1e-4f),
            "pointing as the main hand (%f, %f, %f)", (double)forward[0], (double)forward[1], (double)forward[2]);
        CHECK(vr_two_handed(&frame, 0, 1, 0.0f, orientation) == VR_HELD_SUPPORT, "a pistol's too");
        /* a pistol is not held from ahead of the main hand, a rifle is */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.2f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.65f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        CHECK(vr_two_handed(&frame, 0, 1, 0.0f, orientation) == VR_HELD_ONE, "a pistol: the off hand ahead holds nothing");
        CHECK(vr_two_handed(&frame, 0, 0, 0.0f, orientation) == VR_HELD_BOTH, "a rifle: it holds it");

        /* the hand points along its aim pose (not its grip pose, along the handle), its barrel turned up by
           the pitch */
        frame_of(&frame);
        yawed(1.0f, frame.hand[VR_HAND_RIGHT].grip.orientation);
        vr_hand_pointing(&frame.hand[VR_HAND_RIGHT], 0.5f, orientation);
        vr_rotate(orientation, ahead, forward);
        CHECK(close_to(forward[0], 0, 1e-4f) && close_to(forward[1], sinf(0.5f), 1e-4f) &&
            close_to(forward[2], -cosf(0.5f), 1e-4f), "the aim's -z turned 0.5 up (%f, %f, %f)", (double)forward[0],
            (double)forward[1], (double)forward[2]);

        /* the rumble: the weapon's hand feels both motors, the other the left, and the right while it holds it too */
        {
            float amplitude[VR_HAND_COUNT];

            vr_rumble_hands(0.2f, 0.9f, 0, 0, amplitude);
            CHECK(close_to(amplitude[VR_HAND_RIGHT], 0.9f, 1e-6f) && close_to(amplitude[VR_HAND_LEFT], 0.2f, 1e-6f),
                "one hand: the weapon's feels the shots, the other the hits (%f, %f)",
                (double)amplitude[VR_HAND_LEFT], (double)amplitude[VR_HAND_RIGHT]);
            vr_rumble_hands(0.2f, 0.9f, 0, 1, amplitude);
            CHECK(close_to(amplitude[VR_HAND_LEFT], 0.9f, 1e-6f) && close_to(amplitude[VR_HAND_RIGHT], 0.9f, 1e-6f),
                "both hands on it: both feel the shots");
            vr_rumble_hands(0.2f, 0.9f, 1, 0, amplitude);
            CHECK(close_to(amplitude[VR_HAND_LEFT], 0.9f, 1e-6f) && close_to(amplitude[VR_HAND_RIGHT], 0.2f, 1e-6f),
                "left-handed: mirrored");
        }
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
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.02f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = -0.02f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.15f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK((pad.buttons & VR_PAD_RIGHT_THUMB) && pad.analog[VR_PAD_WHITE] == 0, "at the visor it zooms");
        return 0;
    }
    CASE("throw") {
        int thrown = 0;

        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.throw_gesture = 1;
        frame_of(&frame);
        /* the off trigger held takes a grenade, and throws nothing */
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        for (index = 0; index < 10; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0, "a grenade held is not thrown (%d)",
                pad.analog[VR_PAD_LEFT_TRIGGER]);
        }
        /* let go still, it is kept */
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        for (index = 0; index < 20; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0, "let go still, it is kept");
        }
        /* a swing ahead and up at 3.2 m/s, the hand still for two frames, then let go: thrown, as the hand slows
        to let go */
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        for (index = 0; index < 7; index++)
        {
            if (index < 5)
            {
                frame.hand[VR_HAND_LEFT].grip.position[1] += 0.02f;
                frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.03f;
            }
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0, "not thrown while held");
        }
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        for (index = 0; index < 30; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            thrown += pad.analog[VR_PAD_LEFT_TRIGGER] == 255;
            if (index == 0)
                CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 255, "let go after a swing, thrown at once");
        }
        CHECK(thrown >= 8 && thrown <= 10, "one throw, held a tenth of a second (%d frames)", thrown);
        CHECK(input.throws == 1 && input.throw_hand == VR_HAND_LEFT, "one throw, the left hand's (%lu, %d)",
            input.throws, input.throw_hand);
        /* the throw's velocity the swing's at its fastest (smoothed), not the slowing hand's as it let go; its
        place the hand's as it let go */
        {
            float speed = sqrtf(input.throw_velocity[0] * input.throw_velocity[0] +
                input.throw_velocity[1] * input.throw_velocity[1] + input.throw_velocity[2] * input.throw_velocity[2]);

            CHECK(speed > 2.0f && speed < 3.3f && input.throw_velocity[1] > 0.0f && input.throw_velocity[2] < 0.0f &&
                close_to(input.throw_velocity[1] / input.throw_velocity[2], -2.0f / 3.0f, 0.01f),
                "the swing's way at its fastest (%f: %f %f %f)", (double)speed, (double)input.throw_velocity[0],
                (double)input.throw_velocity[1], (double)input.throw_velocity[2]);
            CHECK(close_to(input.throw_place[1], frame.hand[VR_HAND_LEFT].grip.position[1], 1e-6f) &&
                close_to(input.throw_place[2], frame.hand[VR_HAND_LEFT].grip.position[2], 1e-6f),
                "let go where the hand was");
        }
        /* a slow swing, 0.9 m/s: kept */
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        for (index = 0; index < 10; index++)
        {
            frame.hand[VR_HAND_LEFT].grip.position[2] += 0.01f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        }
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0, "let go after a slow swing, it is kept");
        /* lost by the tracking in the swing, as it is let go: thrown */
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.04f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        }
        frame.hand[VR_HAND_LEFT].tracked = 0;
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 255, "let go untracked after a swing, thrown");
        for (index = 0; index < 20; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].tracked = 1;

        /* the menus: the trigger held through them, both hands moved there, then let go in the game: neither a
        throw nor a punch (the hands' speeds start again) */
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.5f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.5f;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_B] == 0, "no punch from where the hand was before the menus");
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 0 && pad.analog[VR_PAD_B] == 0,
            "nor a throw (%d, %d)", pad.analog[VR_PAD_LEFT_TRIGGER], pad.analog[VR_PAD_B]);
        /* (no grenade held: the main hand moved in the menus is no punch back in the game) */
        for (index = 0; index < 60; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.5f;
        vr_input_update(&input, &frame, &settings, 1, SECONDS, &pad);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_B] == 0, "no punch from where the main hand was before the menus");

        /* the main hand's punch while a grenade is held, or just let go, is none (a throw's swing is no melee); a
        punch once VR_GRENADE_CALM has passed is */
        for (index = 0; index < 90; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        for (index = 0; index < 3; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_B] == 0, "no punch while a grenade is held");
        }
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        frame_of(&frame);
        for (index = 0; index < 9; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            CHECK(pad.analog[VR_PAD_B] == 0, "nor just after it is let go");
        }
        frame_of(&frame);
        for (index = 0; index < 45; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        }
        CHECK(pad.analog[VR_PAD_B] == 255, "a punch once the throw has passed");

        /* without the setting, the trigger throws as a pad's, as far as pulled */
        settings.throw_gesture = 0;
        frame.hand[VR_HAND_LEFT].trigger = 0.5f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 128, "without it the trigger throws (%d)",
            pad.analog[VR_PAD_LEFT_TRIGGER]);
        return 0;
    }
    CASE("grab") {
        int held = 0;

        /* the off grip with a weapon in its reach holds the action, there, VR_GRAB_SECONDS at most */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[1] = -0.9f;
        settings.grab_reach[VR_HAND_LEFT] = 1;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        for (index = 0; index < 90; index++)
        {
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            held += pad.analog[VR_PAD_X] == 255;
            if (index == 0)
                CHECK(pad.analog[VR_PAD_X] == 255 && pad.analog[VR_PAD_WHITE] == 0, "the grip takes it at once");
        }
        CHECK(held >= 53 && held <= 55, "the action held 0.6 s (%d frames)", held);
        /* the weapon comes: to the hand that took it, which now aims */
        vr_input_weapon_taken(&input);
        CHECK(input.left_handed == 1 && input.swaps == 1, "the weapon goes to the left hand (%d, %lu)", input.left_handed,
            input.swaps);
        /* out of its reach, the action is let go */
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 255, "the left (now the main) grip takes one too, without a strike");
        CHECK(pad.analog[VR_PAD_B] == 0, "no strike while it takes (%d)", pad.analog[VR_PAD_B]);
        settings.grab_reach[VR_HAND_LEFT] = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 0 && pad.analog[VR_PAD_B] == 0, "taken (out of reach): let go, still no strike");
        /* a weapon that comes long after a grab (a switch, a new life) stays in the aiming hand */
        frame.hand[VR_HAND_LEFT].buttons = 0;
        for (index = 0; index < 120; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        input.grab_hand = VR_HAND_RIGHT;
        vr_input_weapon_taken(&input);
        CHECK(input.left_handed == 1 && input.swaps == 1, "no grab of late: the hands stay");
        /* the grip with nothing in reach: the main's strikes, the off's does nothing */
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 0, "out of reach the right grip takes nothing (%d)", pad.analog[VR_PAD_X]);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 0 && pad.analog[VR_PAD_B] == 255, "the main grip strikes out of reach");
        return 0;
    }
    CASE("layout") {
        /* the weapon in the left hand (handed over or taken), the right-handed's layout: the triggers and the grips
        go with the weapon, the sticks and the face buttons stay on their controllers */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 0);
        input.left_handed = input.left_handed_valid = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].trigger = 0.5f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_LOWER | VR_BUTTON_GRIP;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_UPPER;
        frame.hand[VR_HAND_LEFT].stick[1] = -1.0f;
        frame.hand[VR_HAND_RIGHT].stick[0] = 0.9f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && pad.analog[VR_PAD_LEFT_TRIGGER] == 128,
            "the left trigger fires, the right throws (%d, %d)", pad.analog[VR_PAD_RIGHT_TRIGGER],
            pad.analog[VR_PAD_LEFT_TRIGGER]);
        CHECK(pad.analog[VR_PAD_B] == 255 && pad.analog[VR_PAD_WHITE] == 0, "the left grip strikes");
        CHECK(pad.analog[VR_PAD_BLACK] == 255 && pad.analog[VR_PAD_A] == 0,
            "the left lower button still switches grenades, it does not jump");
        CHECK(pad.analog[VR_PAD_X] == 255 && pad.analog[VR_PAD_Y] == 0, "the right upper button still acts");
        CHECK(pad.thumb[1] == -32767, "the left stick still moves (%d)", pad.thumb[1]);
        CHECK(close_to(input.turn, -0.5f, 1e-6f), "the right stick still turns (%f)", (double)input.turn);
        frame.hand[VR_HAND_LEFT].buttons = 0;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_WHITE] == 255 && pad.analog[VR_PAD_B] == 0, "the right grip is the flashlight");
        return 0;
    }
    CASE("bare") {
        /* the hands bare: the trigger fires nothing, the stick's click zooms nothing, the grip still strikes */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.bare_hands = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_TRIGGER | VR_BUTTON_STICK | VR_BUTTON_GRIP;
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 0 && !(pad.buttons & VR_PAD_RIGHT_THUMB),
            "bare hands neither fire nor zoom (%d)", pad.analog[VR_PAD_RIGHT_TRIGGER]);
        CHECK(pad.analog[VR_PAD_B] == 255 && pad.analog[VR_PAD_LEFT_TRIGGER] == 255, "but strike and throw");
        settings.bare_hands = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && (pad.buttons & VR_PAD_RIGHT_THUMB), "a weapon in hand fires");
        return 0;
    }
    CASE("physical") {
        unsigned long grabs;
        int hits = 0, held_hand, still;

        /* the hands as they hold: a weapon that came at a new life is held, though its grip is not */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.physical = 1;
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.physical && !input.holding, "no weapon yet, the hands empty");
        vr_input_weapon_taken(&input);
        CHECK(input.holding && !input.gripped, "a new life's weapon: held, not by its grip (%d, %d)", input.holding,
            input.gripped);
        /* the hand that holds it fires; the empty hand's trigger is the grenade's */
        frame.hand[VR_HAND_RIGHT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && pad.analog[VR_PAD_LEFT_TRIGGER] == 0, "the weapon's hand fires");
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_LEFT_TRIGGER] == 255 && pad.analog[VR_PAD_RIGHT_TRIGGER] == 0,
            "the empty hand's trigger, a grenade");
        /* its grip pressed takes hold (no strike); let go away from the shoulders, the weapon goes back to its own */
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding && input.gripped && pad.analog[VR_PAD_B] == 0, "gripped, no strike (%d)", pad.analog[VR_PAD_B]);
        grabs = input.shoulder_grabs;
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_grabs == grabs + 1 && input.shoulder_kind == VR_SHOULDER_PUT &&
            input.shoulder_side == -1, "let go: put back at its own shoulder (%d, %lu, %d, %d)", input.holding,
            input.shoulder_grabs, input.shoulder_kind, input.shoulder_side);
        /* empty: the main trigger is a grenade, the stick's click zooms nothing */
        frame.hand[VR_HAND_RIGHT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_TRIGGER | VR_BUTTON_STICK;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 0 && pad.analog[VR_PAD_LEFT_TRIGGER] == 255 &&
            !(pad.buttons & VR_PAD_RIGHT_THUMB), "empty hands: a grenade, no shot, no zoom (%d, %d)",
            pad.analog[VR_PAD_RIGHT_TRIGGER], pad.analog[VR_PAD_LEFT_TRIGGER]);
        /* a grip at a shoulder that keeps nothing draws nothing */
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        at_shoulder(&frame, VR_HAND_RIGHT, VR_HAND_RIGHT);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_grabs == grabs, "an empty shoulder: nothing drawn");
        /* at a shoulder that keeps a weapon, drawn into the hand, held by its grip */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        settings.shoulder_full[VR_HAND_RIGHT] = 1;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding && input.gripped && input.shoulder_grabs == grabs + 1 &&
            input.shoulder_kind == VR_SHOULDER_DRAW && input.shoulder_side == VR_HAND_RIGHT && !input.left_handed,
            "drawn from the right shoulder (%d, %lu, %d, %d)", input.holding, input.shoulder_grabs, input.shoulder_kind,
            input.shoulder_side);
        /* carried to the left shoulder and let go there: kept there */
        at_shoulder(&frame, VR_HAND_RIGHT, VR_HAND_LEFT);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding, "carried while the grip is held");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_kind == VR_SHOULDER_PUT && input.shoulder_side == VR_HAND_LEFT,
            "let go at the left shoulder (%d, %d, %d)", input.holding, input.shoulder_kind, input.shoulder_side);
        /* the left hand draws it there: the left now aims */
        settings.shoulder_full[VR_HAND_RIGHT] = 0;
        settings.shoulder_full[VR_HAND_LEFT] = 1;
        frame_of(&frame);
        at_shoulder(&frame, VR_HAND_LEFT, VR_HAND_LEFT);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding && input.left_handed && input.swaps == 1 && input.shoulder_kind == VR_SHOULDER_DRAW &&
            input.shoulder_side == VR_HAND_LEFT, "the left hand drew it (%d, %d, %lu)", input.holding,
            input.left_handed, input.swaps);
        settings.shoulder_full[VR_HAND_LEFT] = 0;
        /* the right hand holds it too, at the left: its trigger presses nothing */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = -0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP | VR_BUTTON_TRIGGER;
        frame.hand[VR_HAND_RIGHT].trigger = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.supporting && pad.analog[VR_PAD_LEFT_TRIGGER] == 0 && pad.analog[VR_PAD_RIGHT_TRIGGER] == 0,
            "held with both, the other hand's trigger is nothing (%d, %d)", input.supporting,
            pad.analog[VR_PAD_LEFT_TRIGGER]);
        /* nor its punch */
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        frame.hand[VR_HAND_RIGHT].trigger = 0.0f;
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.04f;
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.04f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            hits += pad.analog[VR_PAD_B] == 255;
        }
        CHECK(hits > 0, "both hands thrust together: the weapon's hand strikes");
        for (index = 0; index < 60; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        hits = 0;
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.04f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            hits += pad.analog[VR_PAD_B] == 255;
        }
        frame.hand[VR_HAND_RIGHT].grip.position[2] = frame.hand[VR_HAND_LEFT].grip.position[2];
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(hits == 0, "the hand that holds it too (a foregrip) strikes nothing alone (%d)", hits);
        /* letting go of it, still, the other hand strikes nothing from where it was before it held it */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.supporting && pad.analog[VR_PAD_B] == 0, "let go of it, no strike (%d)", pad.analog[VR_PAD_B]);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        /* the left lets go: handed over to the right, held still */
        grabs = input.shoulder_grabs;
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 2 && input.holding && input.gripped && input.shoulder_grabs == grabs,
            "handed over to the right (%d, %lu, %d)", input.left_handed, input.swaps, input.holding);
        /* the empty left hand punches */
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        for (index = 0; index < 60; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        hits = 0;
        for (index = 0; index < 4; index++)
        {
            frame.hand[VR_HAND_LEFT].grip.position[2] -= 0.04f;
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
            hits += pad.analog[VR_PAD_B] == 255;
        }
        CHECK(hits > 0 && input.holding, "the empty hand's punch strikes (%d)", hits);
        /* the empty left hand takes a weapon from the ground: to that hand, by its grip */
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        for (index = 0; index < 60; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[1] = -0.9f;
        settings.grab_reach[VR_HAND_LEFT] = 1;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_X] == 255, "the left grip takes it (%d)", pad.analog[VR_PAD_X]);
        vr_input_weapon_taken(&input);
        settings.grab_reach[VR_HAND_LEFT] = 0;
        CHECK(input.left_handed && input.holding && input.gripped, "taken into the left hand, by its grip");
        /* with the grip as a toggle: let go, still held; a press lets go */
        settings.grip_toggle = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding, "toggled: held with the grip let go");
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_kind == VR_SHOULDER_PUT, "toggled: a press lets go");
        frame.hand[VR_HAND_LEFT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        /* in a seat the hands hold as the game's: the trigger fires (the seat's gun); back on foot, held */
        settings.grip_toggle = 0;
        settings.on_foot = 0;
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_TRIGGER;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.physical && pad.analog[VR_PAD_RIGHT_TRIGGER] == 255, "in a seat the trigger fires (%d)",
            pad.analog[VR_PAD_RIGHT_TRIGGER]);
        settings.on_foot = 1;
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.physical && input.holding && !input.gripped, "on foot again: held, not by its grip");
        /* by a VR server's rules, let go away from the shoulders as it swings ahead: it falls, thrown so */
        frame_of(&frame);
        for (still = 0; still < 30; still++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        held_hand = input.left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;
        settings.drop = 1;
        vr_input_weapon_taken(&input);
        frame.hand[held_hand].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[held_hand].grip.position[2] -= 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[held_hand].grip.position[2] -= 0.05f;
        frame.hand[held_hand].buttons = 0;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_grabs == grabs + 1 && input.shoulder_kind == VR_SHOULDER_DROP &&
            input.shoulder_side == -1, "a VR server's rules: let go, it falls (%d, %lu, %d, %d)", input.holding,
            input.shoulder_grabs, input.shoulder_kind, input.shoulder_side);
        {
            float velocity[3];

            memcpy(velocity, input.drop_velocity, sizeof(velocity));
            CHECK(velocity[2] < -0.5f && fabsf(velocity[0]) < 0.01f && fabsf(velocity[1]) < 0.01f,
                "thrown ahead as the hand swung (%f, %f, %f)", velocity[0], velocity[1], velocity[2]);
        }
        settings.drop = 0;
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        /* the game's hands: always held, the grip strikes */
        settings.physical = 0;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.physical && !input.holding && pad.analog[VR_PAD_B] == 255, "the game's hands: the grip strikes");
        return 0;
    }
    CASE("dual") {
        unsigned long grabs;
        int still;

        /* in a game with no network, by a VR server's rules: the right hand holds its weapon */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        settings.physical = 1;
        settings.drop = settings.dual = 1;
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        vr_input_weapon_taken(&input);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.holding && input.gripped && !input.off_holding, "the right hand holds its weapon");
        /* the left hand draws from the left shoulder: it holds that weapon itself, the right keeps its own */
        settings.shoulder_full[VR_HAND_LEFT] = 1;
        at_shoulder(&frame, VR_HAND_LEFT, VR_HAND_LEFT);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding && input.off_side == VR_HAND_LEFT && input.shoulder_grabs == grabs &&
            input.holding && !input.left_handed && input.swaps == 0, "the left hand holds the shoulder's weapon (%d, %d, %lu, %d)",
            input.off_holding, input.off_side, input.shoulder_grabs, input.left_handed);
        settings.shoulder_full[VR_HAND_LEFT] = 0;
        /* the left hand at the right: not a support; the left trigger is that weapon's, no grenade */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.12f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP | VR_BUTTON_TRIGGER;
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding && !input.supporting && pad.analog[VR_PAD_LEFT_TRIGGER] == 0,
            "the left trigger: no grenade (%d, %d)", input.supporting, pad.analog[VR_PAD_LEFT_TRIGGER]);
        CHECK(input.off_side == VR_HAND_LEFT && input.off_trigger == 1.0f && input.off_transfers == 0,
            "told: the left shoulder's, its trigger pulled (%d, %f, %lu)", input.off_side, input.off_trigger,
            input.off_transfers);
        /* the right hand lets go: the left's weapon is the one in hand, the left aims */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.off_transfers == 1 && input.holding && input.gripped && input.left_handed &&
            input.shoulder_grabs == grabs + 1, "the right let go: the left's weapon in hand (%d, %lu, %d, %d)",
            input.off_holding, input.off_transfers, input.holding, input.left_handed);
        /* the right hand draws from the right shoulder, then lets go: back to that shoulder, nothing else */
        settings.shoulder_full[VR_HAND_RIGHT] = 1;
        frame.hand[VR_HAND_LEFT].trigger = 0.0f;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        at_shoulder(&frame, VR_HAND_RIGHT, VR_HAND_RIGHT);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding && input.off_side == VR_HAND_RIGHT, "the right hand holds the right shoulder's weapon");
        /* (drawn, that shoulder keeps nothing now) */
        settings.shoulder_full[VR_HAND_RIGHT] = 0;
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.holding && input.left_handed && input.off_transfers == 1 &&
            input.shoulder_grabs == grabs, "the right let go: its weapon back, the left's kept (%d, %d, %d, %lu)",
            input.off_holding, input.holding, input.left_handed, input.off_transfers);
        CHECK(input.off_releases == 1 && input.off_release_side == VR_HAND_RIGHT,
            "let go at the free right shoulder: kept there (%lu, %d)", input.off_releases, input.off_release_side);
        /* drawn again, and let go there once another weapon is kept there: kept there (the other one moving
           to the free shoulder), and with both shoulders keeping one, it falls */
        settings.shoulder_full[VR_HAND_RIGHT] = 1;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding, "the right hand holds the right shoulder's weapon again");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.off_releases == 2 && input.off_release_side == VR_HAND_RIGHT,
            "let go at a shoulder that keeps another, the other free: kept there (%lu, %d)", input.off_releases,
            input.off_release_side);
        settings.shoulder_full[VR_HAND_LEFT] = 1;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding, "the right hand holds the right shoulder's weapon once more");
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        settings.shoulder_full[VR_HAND_LEFT] = 0;
        CHECK(!input.off_holding && input.off_releases == 3 && input.off_release_side == -1,
            "let go at a shoulder with both keeping one: it falls (%lu, %d)", input.off_releases,
            input.off_release_side);
        /* drawn again and let go before the head as it swings ahead: it falls, thrown so */
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        settings.shoulder_full[VR_HAND_RIGHT] = 0;
        CHECK(input.off_holding, "the right hand holds the right shoulder's weapon a third time");
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.2f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = -0.35f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = -0.35f;
        for (still = 0; still < 30; still++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[2] -= 0.05f;
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.off_releases == 4 && input.off_release_side == -1 &&
            input.off_drop_velocity[2] < -0.5f, "let go at no shoulder: it falls, thrown ahead (%lu, %d, %f)",
            input.off_releases, input.off_release_side, input.off_drop_velocity[2]);
        /* the main hand lets go at a shoulder that keeps another weapon: kept there, the other free; it falls
           with both keeping one */
        settings.shoulder_full[VR_HAND_LEFT] = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        at_shoulder(&frame, VR_HAND_LEFT, VR_HAND_LEFT);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = 0;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_grabs == grabs + 1 && input.shoulder_kind == VR_SHOULDER_PUT &&
            input.shoulder_side == VR_HAND_LEFT, "let go at a full shoulder, the other free: put there (%d, %d)",
            input.holding, input.shoulder_kind);
        vr_input_weapon_taken(&input);
        settings.shoulder_full[VR_HAND_RIGHT] = 1;
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = 0;
        grabs = input.shoulder_grabs;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.holding && input.shoulder_grabs == grabs + 1 && input.shoulder_kind == VR_SHOULDER_DROP,
            "let go at a shoulder with both keeping one: it falls (%d, %d)", input.holding, input.shoulder_kind);
        settings.shoulder_full[VR_HAND_LEFT] = settings.shoulder_full[VR_HAND_RIGHT] = 0;
        vr_input_weapon_taken(&input);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        settings.shoulder_full[VR_HAND_RIGHT] = 1;
        at_shoulder(&frame, VR_HAND_RIGHT, VR_HAND_RIGHT);
        /* held again as those rules end (a network game begins): let go of */
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.off_holding, "the right hand holds it again");
        settings.dual = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.holding && input.left_handed, "no dual wield: let go of (%d)", input.off_holding);
        /* not by those rules: the shoulder's weapon goes to the drawing hand, as always */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.off_holding && input.shoulder_kind == VR_SHOULDER_DRAW && !input.left_handed,
            "no dual wield: drawn into the hand (%d, %d, %d)", input.off_holding, input.shoulder_kind, input.left_handed);
        return 0;
    }
    CASE("swap") {
        /* the off grip takes the weapon at the main hand; the main hand pulls away: handed over */
        memset(&input, 0, sizeof(input));
        settings = settings_of(0, 0.5f, 1);
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 0, "the right hand holds it");
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 0, "taken at the main hand: not yet handed over");
        /* held together past the hand-over's time: a pistol in both hands, kept by the right */
        for (index = 0; index < 200; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.6f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 0, "held together a while, the right keeps it (%lu)", input.swaps);
        /* again, the main hand pulling away at once */
        frame.hand[VR_HAND_LEFT].buttons = 0;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.2f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.32f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 0, "not yet apart");
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.45f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 1, "the right hand pulled away: the left holds it");
        for (index = 0; index < 10; index++)
            vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 1, "once a hand-over, not while held");
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].trigger = 1.0f;
        frame.hand[VR_HAND_RIGHT].trigger = 0.5f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(pad.analog[VR_PAD_RIGHT_TRIGGER] == 255 && pad.analog[VR_PAD_LEFT_TRIGGER] == 128,
            "the left trigger fires now, the right throws");
        /* the off hand (now the right) taking it and pulling away itself lets go: no hand-over */
        frame_of(&frame);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = -0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.3f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.left_handed && input.swaps == 1, "the off hand pulling away lets go");
        /* and back to the right: taken at the left, the left pulling away */
        frame.hand[VR_HAND_RIGHT].buttons = 0;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = -0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.5f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(!input.left_handed && input.swaps == 2, "the right hand takes it back");
        /* not with the main grip held, the hands apart, at the head, or without the gestures */
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.12f;
        frame.hand[VR_HAND_RIGHT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.6f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "the weapon's hand gripping keeps it");
        frame_of(&frame);
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.7f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "the hands apart keep it");
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = -0.04f;
        frame.hand[VR_HAND_LEFT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_LEFT].grip.position[2] = -0.1f;
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.04f;
        frame.hand[VR_HAND_RIGHT].grip.position[1] = 0.0f;
        frame.hand[VR_HAND_RIGHT].grip.position[2] = -0.1f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.5f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2 && pad.analog[VR_PAD_WHITE] == 255, "together at the head is the flashlight");
        settings.gestures = 0;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.6f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        CHECK(input.swaps == 2, "without the gestures, no swap");
        /* the setting changed: its hand again */
        settings.gestures = 1;
        frame_of(&frame);
        frame.hand[VR_HAND_LEFT].grip.position[0] = 0.12f;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_LEFT].buttons = VR_BUTTON_GRIP;
        vr_input_update(&input, &frame, &settings, 0, SECONDS, &pad);
        frame.hand[VR_HAND_RIGHT].grip.position[0] = 0.6f;
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
