#include "harness.h"
#include "config.inc"
/* The host's budget of a client's hits (port/linux/game/network_damage.c,
distributed_report_paid): a bucket of seconds of fire, filling at a second a
second, twice that while the player held a weapon in each hand in the last
REPORT_MAXIMUM_AGE_TICKS; each hit takes 1/rate. Explosions are not used
here (each case's hits are single shots). */
#define TICKS_PER_SECOND 30
#define REPORT_MAXIMUM_AGE_TICKS (3 * TICKS_PER_SECOND)
#define MAXIMUM_TRACKED_PLAYERS 16
#define MIN(a,b) ((a)<(b)?(a):(b))
struct distributed_damage { long definition_index; real_point3d epicenter; };
struct distributed_hit_report { long object_index; struct distributed_damage damage; };
static struct
{
	real hit_seconds;
	long hit_seconds_time;
	long dual_time;
} damage_players[MAXIMUM_TRACKED_PLAYERS];
static struct
{
	long time;
	short count;
	struct { long definition_index; real_point3d epicenter; long object_index; } explosions[MAXIMUM_EXPLOSIONS_PER_TICK];
} damage_explosions[MAXIMUM_TRACKED_PLAYERS];
static long now = 1000;
static long game_time_get(void){return now;}
#include "under_test.inc"

/* the hits an empty bucket pays for over a second and a tick (a second of
ticks adds up to a little under a second), of a weapon firing twice a second */
static int hits_in_a_second(void)
{
	struct distributed_hit_report report = { 7, { 3, { 0.0f, 0.0f, 0.0f } } };
	int hits = 0, tick;

	damage_players[0].hit_seconds = 0.0f;
	damage_players[0].hit_seconds_time = now;
	for (tick = 0; tick <= TICKS_PER_SECOND; tick++)
	{
		now++;
		while (distributed_report_paid(0, &report, 2.0f, FALSE, FALSE))
			hits++;
	}
	return hits;
}

int main(int argc,char **argv)
{
	const char *case_name=argc>1?argv[1]:"";
	damage_players[0].dual_time = -REPORT_MAXIMUM_AGE_TICKS - 1;
	damage_explosions[0].time = NONE;
	CASE("one-weapon") {
		struct distributed_hit_report report = { 7, { 3, { 0.0f, 0.0f, 0.0f } } };
		int burst = 0;

		damage_players[0].hit_seconds = HIT_REPORT_BURST_SECONDS;
		damage_players[0].hit_seconds_time = now;
		while (burst < 100 && distributed_report_paid(0, &report, 2.0f, FALSE, FALSE))
			burst++;
		CHECK(burst == 2 * HIT_REPORT_BURST_SECONDS, "a full bucket: %d hits of a weapon firing twice a second",
			burst);
		CHECK(hits_in_a_second() == 2, "a second's fire: 2 hits");
	} else CASE("two-weapons") {
		damage_players[0].dual_time = now;
		CHECK(hits_in_a_second() == 4, "a weapon in each hand: a second's fire is 4 hits");
	} else CASE("two-weapons-long-past") {
		damage_players[0].dual_time = now - REPORT_MAXIMUM_AGE_TICKS - 1;
		CHECK(hits_in_a_second() == 2, "a weapon in each hand long past: 2 hits");
	}
	return 0;
}
