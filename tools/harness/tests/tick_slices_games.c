/* Which games may leave a tick under way or begin one ahead of its time
(game_time.c, display.tick_slices): any but a film, a client's as well
once the host has started the game, and only in the main loop's frames. */
#include "harness.h"
#include "types.inc"

struct network_game_client
{
	boolean started;
};
static short connection;
static struct network_game_client the_client;
static struct network_game_client *client;
boolean game_time_slicing_allowed;

static short game_connection(void)
{
	return connection;
}

static struct network_game_client *global_network_game_client_get(void)
{
	return client;
}

static boolean network_game_client_server_has_started_game(struct network_game_client *game_client)
{
	return game_client->started;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	game_time_slicing_allowed = TRUE;
	client = &the_client;
	the_client.started = TRUE;
	CASE("local")
	{
		connection = _game_connection_local;
		CHECK(game_time_ticks_may_slice(), "a local game's ticks are run whole");
		return 0;
	}
	CASE("host")
	{
		connection = _game_connection_network_server;
		CHECK(game_time_ticks_may_slice(), "the host's ticks are run whole");
		return 0;
	}
	CASE("client")
	{
		connection = _game_connection_network_client;
		CHECK(game_time_ticks_may_slice(), "a client's ticks are run whole");
		return 0;
	}
	CASE("held-client")
	{
		connection = _game_connection_network_client;
		the_client.started = FALSE;
		CHECK(!game_time_ticks_may_slice(), "a client's tick was sliced while its clock waits for the host");
		return 0;
	}
	CASE("film")
	{
		connection = _game_connection_film_playback;
		CHECK(!game_time_ticks_may_slice(), "a film's tick was sliced");
		return 0;
	}
	CASE("not-main-loop")
	{
		connection = _game_connection_local;
		game_time_slicing_allowed = FALSE;
		CHECK(!game_time_ticks_may_slice(), "a tick was sliced outside the main loop's frames");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
