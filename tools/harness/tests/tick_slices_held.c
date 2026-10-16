/* A tick under way (display.tick_slices) when a client's clock begins to
wait for the host (game_time.c): the menus' game, its ticks begun ahead of
their time after a crowded game, when the server browser makes its client.
The clock takes no tick while it waits, and the main loop runs the network
update it waits for only between ticks (main.c), so a tick left under way
then is never taken: the client never searches, and no game is joined. */
#include <math.h>
#include "harness.h"
#include "types.inc"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define profile_scope(section, statement) statement

struct network_game_client
{
	boolean started;
};
static short connection;
static struct network_game_client the_client;
static struct network_game_client *client;

static short game_connection(void) { return connection; }
static struct network_game_client *global_network_game_client_get(void) { return client; }
static boolean network_game_client_server_has_started_game(struct network_game_client *game_client)
{
	return game_client->started;
}
static void *global_network_game_server_get(void) { return NULL; }
static void network_game_server_update_ticks(void *server, short ticks) { }
static void update_client_local_ticks(long ticks) { }
static long update_client_get_maximum_possible_server_time(void) { return 0x7FFFFFFF; }
static void render_interpolation_tick(void) { }
static void render_interpolation_frame_begin(void) { }
static void render_interpolation_frame_end(void) { }
static void network_distributed_tick(void) { }
static void game_frame(real dt) { }

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
	tick_run(FALSE);
}
static boolean tick_slices_tick(boolean may_slice)
{
	if (!may_slice || tick.pending)
		return FALSE;
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
	if (!tick.pending)
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
static boolean network_game_distributed_client(void) { return FALSE; }

static struct game_time_globals_struct globals;
static struct game_time_globals_struct *game_time_globals = &globals;

#include "under_test.inc"

/* frames of a 360 Hz display */
#define FRAME_TIME (1.f / 360.f)

/* a frame of the main loop: the network's update only between ticks
(main.c); returns whether it ran */
static boolean frame(void)
{
	boolean network = !tick_slices_in_flight();

	game_time_update_sliced(FRAME_TIME);
	game_time_sliced_frame_end();
	return network;
}

static void begin(short game_connection, double tick_lead)
{
	globals.initialized = TRUE;
	globals.active = TRUE;
	globals.speed = 1.f;
	connection = game_connection;
	lead = tick_lead;
}

/* frames until a tick is under way that was begun ahead of its time (or,
counted, as one due) */
static void until_under_way(boolean counted)
{
	int index;

	for (index = 0; index < 1000 && !(tick.pending && tick.counted == counted && !tick.ended); index++)
		frame();
	CHECK(tick.pending && tick.counted == counted && !tick.ended, "no %s tick under way in %d frames",
		counted ? "due" : "ahead", index);
}

/* the server browser makes a client, which waits for a host's game (held) */
static void browser_opens(void)
{
	client = &the_client;
	the_client.started = FALSE;
	connection = _game_connection_network_client;
}

/* within a few frames (as a tick due is run whole on the eighth, tick_slices.c)
the clock no longer moves, and the network's update runs on every frame */
static void held_frames(void)
{
	long server_time;
	int index;

	for (index = 0; index < 10 && !frame(); index++)
		;
	CHECK(index < 10, "a tick under way when the clock began to wait still holds off the network");
	CHECK(!tick_slices_in_flight(), "a tick is still under way while the clock waits");
	server_time = globals.server_time;
	for (index = 0; index < 360; index++)
		CHECK(frame(), "frame %d after the clock began to wait ran no network update", index);
	CHECK(globals.server_time == server_time, "the clock took %ld ticks while it waits",
		globals.server_time - server_time);
}

/* a second of frames takes a second's ticks, with ticks under way */
static void ticks_taken(void)
{
	long server_time = globals.server_time;
	int index, under_way = 0;

	for (index = 0; index < 360; index++)
	{
		frame();
		under_way += tick_slices_in_flight();
	}
	CHECK(under_way > 0, "no tick was under way");
	CHECK(globals.server_time - server_time >= 29 && globals.server_time - server_time <= 31,
		"a second took %ld ticks", globals.server_time - server_time);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("held-after-ahead")
	{
		begin(_game_connection_local, 0.01);
		until_under_way(FALSE);
		browser_opens();
		held_frames();
		return 0;
	}
	CASE("held-after-due")
	{
		begin(_game_connection_local, 0.0);
		until_under_way(TRUE);
		browser_opens();
		held_frames();
		return 0;
	}
	CASE("local")
	{
		begin(_game_connection_local, 0.01);
		ticks_taken();
		return 0;
	}
	CASE("client")
	{
		begin(_game_connection_network_client, 0.01);
		client = &the_client;
		the_client.started = TRUE;
		ticks_taken();
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
