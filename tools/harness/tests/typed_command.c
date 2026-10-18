/* A command typed while a tick is under way (display.tick_slices) runs
after the tick has run to its end (hs.c, hs_compile_and_evaluate): a tick
may have handed back in the middle of a script, whose thread a command would
take over. The tick is left for the clock (tick_slices_complete). */
#include "harness.h"

static boolean terminal_command_running;

/* the tick: under way, handed back part way, run from its own stack */
static boolean in_flight, part_way, in_tick;
static int commands;

static void tick_slices_complete(void)
{
	if (in_flight && !in_tick)
		part_way = FALSE;
}

static boolean hs_compile_and_evaluate_command(char const *expression)
{
	CHECK(terminal_command_running, "the command not run as typed");
	CHECK(!part_way || in_tick, "the command ran between the parts of a tick");
	commands++;
	return TRUE;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("typed")
	{
		in_flight = part_way = TRUE;
		CHECK(hs_compile_and_evaluate("(ai_place x)") && commands == 1, "the command did not run");
		CHECK(in_flight, "the tick taken from the clock");
		CHECK(!terminal_command_running, "still running a typed command");
		return 0;
	}
	CASE("idle")
	{
		CHECK(hs_compile_and_evaluate("(ai_place x)") && commands == 1, "the command did not run");
		return 0;
	}
	CASE("from-tick")
	{
		in_flight = part_way = in_tick = TRUE;
		CHECK(hs_compile_and_evaluate("(ai_place x)") && commands == 1, "the command did not run");
		CHECK(part_way, "the tick under way ended from itself");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
