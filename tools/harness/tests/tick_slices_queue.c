/* A local game's updates with its ticks in pieces (display.tick_slices,
game_time.c): each holds the input a tick takes as it begins (players.c's
players_update_before_game, update_client_dequeue). A tick begun ahead of
its time took none, as its update was made only when it fell due; that
update then went to the tick after it, and so on, each tick taking the
input of the tick before for the rest of the game. */
#include <math.h>
#include "harness.h"
#include "types.inc"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define profile_scope(section, statement) statement

static short connection;

static short game_connection(void) { return connection; }
static void *global_network_game_client_get(void) { return NULL; }
static boolean network_game_client_server_has_started_game(void *game_client) { return FALSE; }
static void *global_network_game_server_get(void) { return NULL; }
static void network_game_server_update_ticks(void *server, short ticks) { }
static void render_interpolation_tick(void) { }
static void render_interpolation_frame_begin(void) { }
static void render_interpolation_frame_end(void) { }
static void network_distributed_tick(void) { }
static void game_frame(real dt) { }
static boolean network_game_distributed_client(void) { return FALSE; }
static boolean game_time_menu_scene(void) { return FALSE; }
static int halo_fixed_step(void) { return 0; }

/* the updates (player_queues_new.c): the frame each was made on, the latest
made, the next a tick takes; and the ticks that took none, or one made on a
frame before theirs */
#define MAXIMUM_UPDATES 4096
static long made_on_frame[MAXIMUM_UPDATES];
static long latest_made = -1;
static long next_taken;
static long frame_index;
static long ticks_begun, ticks_ahead, misses, stale;

static void update_client_local_ticks(short ticks)
{
	while (ticks-- > 0)
	{
		latest_made++;
		CHECK(latest_made < MAXIMUM_UPDATES, "too many updates made");
		made_on_frame[latest_made] = frame_index;
	}
}
static long update_client_get_maximum_possible_server_time(void)
{
	return latest_made + 1 > next_taken ? latest_made + 1 : next_taken;
}
/* a tick takes its update as it begins (a missed one is not taken) */
static void tick_takes_update(void)
{
	ticks_begun++;
	if (next_taken > latest_made)
	{
		misses++;
		return;
	}
	if (made_on_frame[next_taken] != frame_index)
		stale++;
	next_taken++;
}

/* the ticks: one begun runs a share at once and one on each frame after it,
and ends on its third (tick_slices.c) */
#define TICK_SHARES 3
static struct
{
	boolean pending, ended, counted;
	int shares;
} tick;
static double lead;

static void tick_slices_release(void) { memset(&tick, 0, sizeof(tick)); }
static boolean tick_slices_in_flight(void) { return tick.pending; }
static boolean tick_slices_counted(void) { return tick.pending && tick.counted; }
static boolean tick_slices_ended(void) { return tick.pending && tick.ended; }
static void tick_slices_count(void) { tick.counted = TRUE; }
static double tick_slices_lead(void) { return lead; }
static void tick_run(boolean whole)
{
	if (!tick.ended && (whole || ++tick.shares >= TICK_SHARES))
		tick.ended = TRUE;
}
static void tick_begin(boolean counted)
{
	tick.pending = TRUE;
	tick.ended = FALSE;
	tick.counted = counted;
	tick.shares = 0;
	tick_takes_update();
	tick_run(FALSE);
}
static boolean tick_slices_tick(boolean may_slice)
{
	if (!may_slice || tick.pending)
	{
		/* (game_tick, whole) */
		tick_takes_update();
		return FALSE;
	}
	tick_begin(TRUE);
	if (tick.ended)
	{
		tick_slices_release();
		return FALSE;
	}
	return TRUE;
}
static void tick_slices_tick_ahead(void)
{
	if (tick.pending)
		return;
	ticks_ahead++;
	tick_begin(FALSE);
}
static boolean tick_slices_continue(boolean whole)
{
	if (!tick.pending)
		return TRUE;
	tick_run(whole);
	return tick.ended;
}
static void tick_slices_finish(void)
{
	if (!tick.pending)
		return;
	tick_run(TRUE);
	tick_slices_release();
}

static struct game_time_globals_struct globals;
static struct game_time_globals_struct *game_time_globals = &globals;

#include "under_test.inc"

/* a second's frames of a display of the rate given, sliced or whole: every
tick takes the update made on its frame, and a second takes a second's ticks */
static void second_of_frames(double tick_lead, int frames_per_second, boolean sliced, boolean ahead_expected)
{
	int index;

	globals.initialized = TRUE;
	globals.active = TRUE;
	globals.speed = 1.f;
	connection = _game_connection_local;
	lead = tick_lead;
	for (index = 0; index < frames_per_second * 2; index++)
	{
		frame_index++;
		if (sliced)
			game_time_update_sliced(1.f / frames_per_second);
		else
			game_time_update(1.f / frames_per_second);
		game_time_sliced_frame_end();
	}
	CHECK(ahead_expected ? ticks_ahead >= ticks_begun / 2 : ticks_ahead == 0, "%ld of %ld ticks were begun ahead of their time",
		ticks_ahead, ticks_begun);
	CHECK(misses == 0, "%ld of %ld ticks took no update", misses, ticks_begun);
	CHECK(stale == 0, "%ld of %ld ticks took an update made on a frame before theirs", stale, ticks_begun);
	CHECK(latest_made - next_taken <= 0, "%ld updates made and not taken", latest_made - next_taken + 1);
	CHECK(globals.server_time >= 59 && globals.server_time <= 61, "two seconds took %ld ticks", globals.server_time);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("ahead")
	{
		second_of_frames(0.01, 360, TRUE, TRUE);
		return 0;
	}
	CASE("ahead-slow-display")
	{
		second_of_frames(0.01, 144, TRUE, TRUE);
		return 0;
	}
	CASE("due")
	{
		second_of_frames(0.0, 360, TRUE, FALSE);
		return 0;
	}
	CASE("whole")
	{
		second_of_frames(0.01, 360, FALSE, FALSE);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
