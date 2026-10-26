#include "harness.h"
#include <stdarg.h>
/* Which advertised hosts a client joins (network_client_manager.c's
network_game_client_advertised_game_compatible, by halo_port_limits.h's
versions): a host of its own network version, or a VR server (its version
with HALO_PORT_NETWORK_VR_SERVER_BIT) of its version and VR revision; the
others refused, the player told why. And the kind the server browser names. */
typedef unsigned short word;
#define MAXIMUM_NETWORK_ADVERTISED_GAMES 4
#define csprintf sprintf
struct network_advertised_game { int unused; };
struct network_game_client { struct network_advertised_game available_games[MAXIMUM_NETWORK_ADVERTISED_GAMES]; };
/* the client's log and the message the player is shown */
static char event[400];
static char shown[400];
static int shown_count;
static void network_event(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(event, sizeof(event), format, arguments);
	va_end(arguments);
}
static void platform_show_message(char const *title, char const *message)
{
	(void)title;
	snprintf(shown, sizeof(shown), "%s", message);
	shown_count++;
}
#include "under_test.inc"

static struct network_game_client client;

/* whether the client joins a host advertising version, flags and revision,
telling the player */
static boolean joins(unsigned version, byte flags, byte revision)
{
	network_game_client_advertised_versions[1].version = (word)version;
	network_game_client_advertised_versions[1].flags = flags;
	network_game_client_advertised_versions[1].kind = 3;
	network_game_client_advertised_versions[1].vr_revision = revision;
	event[0] = shown[0] = 0;
	shown_count = 0;
	return network_game_client_advertised_game_compatible(&client, &client.available_games[1], TRUE);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned ours = HALO_PORT_NETWORK_VERSION;
	unsigned vr = HALO_PORT_NETWORK_VERSION | HALO_PORT_NETWORK_VR_SERVER_BIT;
	byte distributed = HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG;

	CASE("standard") {
		CHECK(joins(ours, distributed, 0), "a host of this version");
		CHECK(strstr(event, "joining a host of network version") && !strstr(event, "VR"), "%s", event);
		CHECK(!shown_count, "nothing shown");
		CHECK(joins(ours, distributed, HALO_PORT_NETWORK_VR_REVISION + 1), "no VR server: its revision byte unread");
		CHECK(network_game_client_advertised_game_kind(&client, &client.available_games[1]) == 3,
			"its kind as advertised");
	} else CASE("vr") {
		CHECK(joins(vr, distributed, HALO_PORT_NETWORK_VR_REVISION), "a VR server of this version and revision");
		CHECK(strstr(event, "(a VR server)"), "%s", event);
		CHECK(network_game_client_advertised_game_kind(&client, &client.available_games[1]) ==
			(3 | HALO_PORT_ADVERTISED_KIND_VR), "named a VR server's by its version");
	} else CASE("vr_revision") {
		CHECK(!joins(vr, distributed, 0), "a VR server with no revision");
		CHECK(!joins(vr, distributed, HALO_PORT_NETWORK_VR_REVISION + 1), "a newer VR revision");
		CHECK(shown_count == 1 && strstr(shown, "VR version") && strstr(shown, "Update the game"), "%s", shown);
		CHECK(strstr(event, "not joining") && strstr(event, "(a VR server)"), "%s", event);
		CHECK(HALO_PORT_NETWORK_VR_REVISION > 1 ? !joins(vr, distributed, HALO_PORT_NETWORK_VR_REVISION - 1) : 1,
			"an older VR revision");
	} else CASE("others") {
		CHECK(!joins(ours - 1, distributed, 0) && strstr(shown, "older"), "an older version: %s", shown);
		CHECK(!joins(ours + 1, distributed, 0) && strstr(shown, "newer"), "a newer version: %s", shown);
		CHECK(!joins(vr - 1, distributed, HALO_PORT_NETWORK_VR_REVISION) && strstr(shown, "older"),
			"an older version's VR server: %s", shown);
		CHECK(!joins(vr + 1, distributed, HALO_PORT_NETWORK_VR_REVISION) && strstr(shown, "newer"),
			"a newer version's VR server: %s", shown);
		CHECK(!joins(0, distributed, 0), "a host built before there was a version");
	} else CASE("lockstep") {
		CHECK(!joins(ours, 0, 0) && strstr(shown, "lockstep"), "%s", shown);
		CHECK(!joins(vr, 0, HALO_PORT_NETWORK_VR_REVISION), "a VR server of the lockstep netcode");
	} else CASE("official") {
		/* (every build before VR servers, released ones too, joins a host
		of its version alone, and calls a larger one newer) */
		CHECK(vr != ours && vr > ours, "an official build of this version takes a VR server's for a newer one");
		CHECK(!joins(vr, distributed, 0) || HALO_PORT_NETWORK_VR_REVISION == 0, "not a VR server without revision");
	} else return 2;
	return 0;
}
