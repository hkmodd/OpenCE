#include "harness.h"
/* A hand on a vehicle's control in a headset's seat (port/linux/game/vr_view.c):
it takes hold near, keeps hold a little further, lets go beyond, always eased
from 0 to 1. */
#include "under_test.inc"

#define FRAME (1.0f / 90.0f)

int main(int argc, char **argv)
{
	boolean held = FALSE;
	real hold = 0.0f;
	short index;

	if (argc > 1 && !strcmp(argv[1], "hold"))
	{
		/* away, nothing */
		hold = vr_seat_hold_eased(hold, &held, 0.30f, FRAME);
		CHECK(!held && hold == 0.0f, "30 cm away has not taken hold (%f)", (double)hold);
		/* near: taken at once, eased over its time, not in a frame */
		hold = vr_seat_hold_eased(hold, &held, 0.20f, FRAME);
		CHECK(held && hold > 0.0f && hold < 0.2f, "taking hold (%f)", (double)hold);
		for (index = 0; index < 30; index++)
			hold = vr_seat_hold_eased(hold, &held, 0.20f, FRAME);
		CHECK(hold == 1.0f, "held (%f)", (double)hold);
		/* a little further: kept */
		hold = vr_seat_hold_eased(hold, &held, 0.30f, FRAME);
		CHECK(held && hold == 1.0f, "kept at 30 cm (%f)", (double)hold);
		/* beyond: let go, eased to nothing */
		hold = vr_seat_hold_eased(hold, &held, 0.40f, FRAME);
		CHECK(!held && hold > 0.8f && hold < 1.0f, "letting go (%f)", (double)hold);
		for (index = 0; index < 30; index++)
			hold = vr_seat_hold_eased(hold, &held, 0.40f, FRAME);
		CHECK(hold == 0.0f, "let go (%f)", (double)hold);
		return 0;
	}
	return 2;
}
