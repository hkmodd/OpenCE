#include "harness.h"
/* By whose rules a headset's player plays (port/linux/game/vr_view.c's
vr_rules_active): a game with no network, a local one, and a VR server's
(its host's word: _network_game_port_vr_rules_bit); none in a network game
before its host's word comes. And the melee through the fist, by a VR
server's rules alone (vr_render_melee_point, vr_render_off_hand_strike). */
enum { _game_connection_local = 0, _game_connection_network_client, _game_connection_network_server,
	_game_connection_film_playback };
enum { _network_game_port_vr_rules_bit = 2 };
enum { VR_HAND_LEFT = 0, VR_HAND_RIGHT, VR_HAND_COUNT };
struct network_game { byte cooperative_flags; };
typedef struct { real_point3d position; } vr_grip;
static struct
{
	int stereo, hand_aiming, main_hand;
	long unit_index;
	int grip_valid[VR_HAND_COUNT];
	vr_grip grips[VR_HAND_COUNT];
} vr_view;
/* the world: the connection, the network game (none: NULL), a split
screen's own server, the hand that struck */
static int connection;
static struct network_game game;
static struct network_game *network_game;
static boolean splitscreen;
static int melee_hand;
static int game_connection(void){return connection;}
static struct network_game *network_game_get_game(void){return network_game;}
static boolean network_game_is_splitscreen_local(void){return splitscreen;}
static int vr_input_melee_hand(void){return melee_hand;}
static void platform_log(char const *format, ...){(void)format;}
#include "under_test.inc"

static void world(int connected, struct network_game *of, byte flags)
{
	connection = connected;
	network_game = of;
	game.cooperative_flags = flags;
	splitscreen = FALSE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	byte vr = FLAG(_network_game_port_vr_rules_bit);

	CASE("rules") {
		world(_game_connection_local, NULL, 0);
		CHECK(vr_rules_active(), "a game with no network");
		world(_game_connection_film_playback, NULL, 0);
		CHECK(vr_rules_active(), "a film");
		world(_game_connection_network_server, &game, 0);
		splitscreen = TRUE;
		CHECK(vr_rules_active(), "a split screen's own server");
		world(_game_connection_network_client, &game, vr);
		CHECK(vr_rules_active(), "a VR server's client");
		world(_game_connection_network_server, &game, vr);
		CHECK(vr_rules_active(), "a VR server");
		world(_game_connection_network_client, &game, 0);
		CHECK(!vr_rules_active(), "an ordinary server's client");
		world(_game_connection_network_server, &game, 0);
		CHECK(!vr_rules_active(), "an ordinary server");
	} else CASE("closed") {
		world(_game_connection_network_client, NULL, vr);
		CHECK(!vr_rules_active(), "a network game before its host's word");
		world(_game_connection_network_server, NULL, vr);
		CHECK(!vr_rules_active(), "a server with no game yet");
	} else CASE("melee") {
		real_point3d point = { 0 };

		vr_view.stereo = vr_view.hand_aiming = TRUE;
		vr_view.unit_index = 7;
		vr_view.main_hand = VR_HAND_RIGHT;
		vr_view.grip_valid[VR_HAND_LEFT] = TRUE;
		vr_view.grips[VR_HAND_LEFT].position.x = 3.0f;
		melee_hand = VR_HAND_LEFT;
		world(_game_connection_network_client, &game, vr);
		CHECK(vr_render_melee_point(7, &point) && point.x == 3.0f, "a VR server's: through the fist");
		CHECK(vr_render_off_hand_strike(7), "the other hand drawn striking");
		world(_game_connection_network_client, &game, 0);
		point.x = 0.0f;
		CHECK(!vr_render_melee_point(7, &point) && point.x == 0.0f, "an ordinary server's: where the weapon aims");
		CHECK(!vr_render_off_hand_strike(7), "no other hand drawn striking");
		world(_game_connection_local, NULL, 0);
		CHECK(vr_render_melee_point(7, &point), "a game with no network: through the fist");
	} else return 2;
	return 0;
}
