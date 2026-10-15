/* A game tick's timer ended (profile.c): in the frame it began in, its own;
in a later frame, which a tick run in pieces (display.tick_slices) ends in,
none, rather than the one before the frame's first. */
#include "harness.h"
#include "types.inc"

#define MAXIMUM_GAME_TICKS_PER_FRAME TEST_MAXIMUM_GAME_TICKS_PER_FRAME
struct profile_timer
{
	long ended;
};
static struct
{
	struct
	{
		short game_tick_count;
		struct profile_timer before_first;
		struct profile_timer game_ticks[MAXIMUM_GAME_TICKS_PER_FRAME];
	} current_frame;
} profile_globals;
static struct profile_timer *ended;

static void profile_timesection_end_now(struct profile_timer *timer)
{
	ended = timer;
	timer->ended++;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("in-frame")
	{
		profile_globals.current_frame.game_tick_count = 3;
		profile_tick_end();
		CHECK(ended == &profile_globals.current_frame.game_ticks[2], "another timer ended");
		return 0;
	}
	CASE("later-frame")
	{
		profile_globals.current_frame.game_tick_count = 0;
		profile_tick_end();
		CHECK(ended == NULL && profile_globals.current_frame.before_first.ended == 0, "a timer ended in a frame without the tick's");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
