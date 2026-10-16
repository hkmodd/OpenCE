/* (the fiber file's clock is the real one: the code reads the test's; and
Windows' own boolean is not the game's) */
#define platform_fiber_clock platform_fiber_real_clock
#define platform_fiber_clock_rate platform_fiber_real_clock_rate
#ifdef _WIN32
#define boolean windows_boolean
#include "win32_fiber.inc"
#undef boolean
#else
#include "posix_fiber.inc"
#endif
#undef platform_fiber_clock
#undef platform_fiber_clock_rate

#include "harness.h"
#include "tick_slices.h"

/* A tick in pieces (port/linux/game/tick_slices.c) on the platform's own
second stack (posix_fiber.c, win32_fiber.c). The tick is tick_points steps
of STEP microseconds of a clock of the test's (CLOCK_RATE counts a second), each followed by a yield
point; the render thread's spare time is the idle work the code hands it. */
#define STEP 100
#define CLOCK_RATE 10000000u

static boolean slicing_enabled;
static unsigned long setting_changes;
static unsigned long long now;
static int tick_points, points_done, ticks_run;
static int finish_at_point = -1;
static int periods_ended, periods_continued;
static int (*idle_work)(void);
/* the frames ended, and how long the latest end of a frame had to spare */
static unsigned long frames_counted;
static unsigned long long room_counts = CLOCK_RATE;

static int config_boolean(const char *name)
{
	CHECK(!strcmp(name, "display.tick_slices"), "not the setting: %s", name);
	return slicing_enabled;
}
static unsigned long config_changes(void) { return setting_changes; }
static int render_thread_running(void) { return 1; }
static void render_thread_set_idle_work(int (*work)(void)) { idle_work = work; }
static unsigned long long render_thread_frame_room(unsigned long *frame)
{
	*frame = frames_counted;
	return room_counts;
}
static unsigned long long platform_fiber_clock(void) { return now; }
static unsigned long long platform_fiber_clock_rate(void) { return CLOCK_RATE; }
static void collision_log_end_period(void) { periods_ended++; }
static void collision_log_continue_period(short time_period)
{
	CHECK(periods_continued < periods_ended, "a period continued that had not ended");
	periods_continued++;
}

static void game_tick(void)
{
	int point;

	for (point = 0; point < tick_points; point++)
	{
		now += STEP * (CLOCK_RATE / 1000000u);
		points_done++;
		if (point == finish_at_point)
			tick_slices_finish();
		tick_slices_yield();
	}
	ticks_run++;
}

#include "under_test.inc"

static void enable(boolean enabled)
{
	slicing_enabled = enabled;
	setting_changes++;
}

/* the spare time of one frame: the idle work until it has no more */
static int spare_time(void)
{
	int calls = 0;

	CHECK(idle_work != NULL, "no idle work handed to the render thread");
	while (idle_work())
		calls++;
	return calls + 1;
}

/* a tick of points steps begun due, left under way and run in the spare time */
static void sliced_tick(int points)
{
	int ticks = ticks_run;

	tick_points = points;
	points_done = 0;
	CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
	spare_time();
	CHECK(tick_slices_ended() && ticks_run == ticks + 1, "the tick did not end in the spare time");
	CHECK(tick_slices_continue(FALSE), "an ended tick ran on");
	tick_slices_release();
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("disabled")
	{
		enable(FALSE);
		tick_points = 20;
		CHECK(!tick_slices_tick(TRUE), "a tick left under way while slicing is off");
		CHECK(ticks_run == 1 && points_done == 20, "the tick did not run whole");
		CHECK(!tick_slices_in_flight() && tick_slices_lead() == 0.0, "a tick under way, or a lead");
		tick_slices_tick_ahead();
		CHECK(!tick_slices_in_flight(), "a tick begun ahead while slicing is off");
		return 0;
	}
	CASE("short")
	{
		enable(TRUE);
		tick_points = 2;
		CHECK(!tick_slices_tick(TRUE), "a tick that ends in its share left under way");
		CHECK(ticks_run == 1 && !tick_slices_in_flight(), "the short tick was not taken whole");
		CHECK(tick_slices_lead() == 0.0, "a lead for ticks that end in their share");
		CHECK(!tick_slices_tick(FALSE) && ticks_run == 2, "a tick that may not be sliced did not run");
		return 0;
	}
	CASE("spare")
	{
		enable(TRUE);
		tick_points = 20;
		CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
		CHECK(tick_slices_in_flight() && tick_slices_counted() && !tick_slices_ended(),
			"the tick left under way is not due and unfinished");
		CHECK(points_done == 3 && ticks_run == 0, "the frame's share ran %d steps", points_done);
		CHECK(spare_time() == 18, "the spare time did not run it a step at a time");
		CHECK(tick_slices_ended() && ticks_run == 1 && points_done == 20, "the tick did not end in the spare time");
		CHECK(!idle_work(), "idle work for an ended tick");
		CHECK(tick_slices_continue(FALSE), "an ended tick ran on");
		tick_slices_release();
		CHECK(!tick_slices_in_flight(), "the released tick still under way");
		sliced_tick(20);
		CHECK(ticks_run == 2, "the second stack did not run a second tick");
		return 0;
	}
	CASE("held")
	{
		int frame;

		enable(TRUE);
		tick_points = 20;
		CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
		CHECK(points_done == 3, "the frame's share ran %d steps", points_done);
		for (frame = 1; frame < 6; frame++)
		{
			CHECK(!tick_slices_continue(FALSE), "the tick ended on frame %d", frame);
			CHECK(points_done == 3 + 3 * frame, "frame %d's share ran to step %d", frame, points_done);
		}
		CHECK(tick_slices_continue(FALSE) && ticks_run == 1, "the tick did not end on its seventh frame");
		tick_slices_release();
		return 0;
	}
	CASE("forced")
	{
		int frame;

		enable(TRUE);
		tick_points = 100;
		CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
		for (frame = 1; frame < 8; frame++)
			CHECK(!tick_slices_continue(FALSE), "the tick ended on frame %d", frame);
		CHECK(tick_slices_continue(FALSE) && ticks_run == 1 && points_done == 100,
			"the eighth frame after it did not run it whole");
		tick_slices_release();
		tick_points = 100;
		points_done = 0;
		CHECK(tick_slices_tick(TRUE) && tick_slices_continue(TRUE) && points_done == 100,
			"a tick asked whole did not run whole");
		tick_slices_release();
		return 0;
	}
	CASE("ahead")
	{
		int frame;

		/* one begun ahead of its time holds nothing up: it runs on in
		pieces, and is run whole on the eighth frame after it falls due */
		enable(TRUE);
		tick_points = 100;
		tick_slices_tick_ahead();
		CHECK(tick_slices_in_flight() && !tick_slices_counted(), "the tick begun ahead counted by the clock");
		for (frame = 1; frame < 12; frame++)
			CHECK(!tick_slices_continue(FALSE), "the tick not yet due ended on frame %d", frame);
		CHECK(points_done == 36, "the frames' shares ran to step %d", points_done);
		tick_slices_count();
		for (frame = 1; frame < 8; frame++)
			CHECK(!tick_slices_continue(FALSE), "the tick ended on frame %d after it fell due", frame);
		CHECK(tick_slices_continue(FALSE) && ticks_run == 1 && points_done == 100,
			"the eighth frame after it fell due did not run it whole");
		tick_slices_release();
		/* (the next counts its own frames) */
		points_done = 0;
		CHECK(tick_slices_tick(TRUE) && !tick_slices_continue(FALSE), "the next tick ran whole on its first frame after");
		tick_slices_finish();
		CHECK(ticks_run == 2 && points_done == 100, "the next tick was not finished");
		return 0;
	}
	CASE("room")
	{
		/* one begun ahead of its time runs at the start of a frame only
		for what the frame before had to spare, less 200 us */
		enable(TRUE);
		tick_points = 100;
		room_counts = 0;
		tick_slices_tick_ahead();
		CHECK(tick_slices_in_flight() && points_done == 0, "a frame with no time to spare ran %d steps", points_done);
		frames_counted = 1;
		room_counts = 350 * (CLOCK_RATE / 1000000u);
		CHECK(!tick_slices_continue(FALSE) && points_done == 2, "350 us to spare ran to step %d", points_done);
		/* (with the share it ran itself) */
		frames_counted = 2;
		room_counts = 100 * (CLOCK_RATE / 1000000u);
		CHECK(!tick_slices_continue(FALSE) && points_done == 3, "100 us to spare after a share of 200 ran to step %d",
			points_done);
		frames_counted = 3;
		room_counts = 0;
		CHECK(!tick_slices_continue(FALSE) && points_done == 3, "no time to spare ran to step %d", points_done);
		/* (a share two frames before is not this frame's) */
		frames_counted = 4;
		room_counts = 200 * (CLOCK_RATE / 1000000u);
		CHECK(!tick_slices_continue(FALSE) && points_done == 3, "200 us to spare after no share ran to step %d",
			points_done);
		spare_time();
		CHECK(tick_slices_ended() && ticks_run == 1 && points_done == 100, "the spare time did not end the tick");
		tick_slices_release();
		return 0;
	}
	CASE("room-due")
	{
		/* one that is due runs its whole share, time to spare or not */
		enable(TRUE);
		tick_points = 100;
		room_counts = 0;
		CHECK(tick_slices_tick(TRUE) && points_done == 3, "the tick due ran %d steps", points_done);
		frames_counted = 1;
		CHECK(!tick_slices_continue(FALSE) && points_done == 6, "the tick due ran to step %d", points_done);
		tick_slices_finish();
		return 0;
	}
	CASE("lead")
	{
		int tick;
		double lead;

		enable(TRUE);
		for (tick = 0; tick < 64; tick++)
			sliced_tick(20);
		lead = tick_slices_lead();
		CHECK(lead > 0.0024 && lead <= 0.0025, "a lead of %f s for ticks of 2 ms", lead);
		tick_points = 20;
		tick_slices_tick_ahead();
		CHECK(tick_slices_in_flight() && !tick_slices_counted(), "the tick begun ahead counted by the clock");
		spare_time();
		CHECK(tick_slices_ended() && !tick_slices_counted(), "the tick begun ahead did not end uncounted");
		tick_slices_release();
		tick_slices_tick_ahead();
		tick_slices_count();
		CHECK(tick_slices_counted() && !tick_slices_ended(), "the tick that fell due not counted");
		tick_slices_finish();
		CHECK(!tick_slices_in_flight() && ticks_run == 66, "the counted tick was not finished");
		return 0;
	}
	CASE("finish")
	{
		enable(TRUE);
		tick_points = 20;
		finish_at_point = 5;
		CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
		tick_slices_finish();
		CHECK(!tick_slices_in_flight() && ticks_run == 1 && points_done == 20, "the tick was not finished");
		tick_slices_finish();
		CHECK(ticks_run == 1, "finishing with no tick under way ran one");
		finish_at_point = -1;
		sliced_tick(20);
		CHECK(ticks_run == 2, "the second stack did not run the next tick");
		return 0;
	}
	CASE("collision")
	{
		enable(TRUE);
		tick_points = 20;
		CHECK(tick_slices_tick(TRUE), "a long tick ended in its frame's share");
		CHECK(periods_ended == 1 && periods_continued == 0, "the tick's collision period not ended when it handed back");
		spare_time();
		CHECK(periods_ended == 18 && periods_continued == 18,
			"the collision periods were not ended and continued in pairs: %d, %d", periods_ended, periods_continued);
		tick_slices_release();
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
