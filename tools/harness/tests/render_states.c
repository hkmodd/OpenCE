#include "harness.h"

/* The render and texture states the game sets (D3D__RenderState,
D3D__TextureState), sent to the renderer before each draw (d3d8_gl.c):
states_sync records the pairs that changed, which states_command, run by
the renderer, applies to its own copy (render_states, texture_states). */
typedef unsigned long DWORD;
typedef int BOOL;

#include "config.inc"

static DWORD D3D__RenderState[D3DRS_MAX];
static DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
static DWORD render_states[D3DRS_MAX];
static DWORD texture_states[D3DTSS_MAXSTAGES][D3DTSS_MAX];
static struct
{
	DWORD render_states[D3DRS_MAX];
	DWORD texture_states[D3DTSS_MAXSTAGES][D3DTSS_MAX];
} sent;

/* the queue: one command recorded at most between draws */
static DWORD recorded[1 + 2 * (D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX)];
static void (*recorded_command)(void *payload);
static unsigned long recorded_size;

static void *render_thread_record(void (*command)(void *payload), unsigned long size)
{
	CHECK(!recorded_command, "two commands for one draw");
	CHECK(size <= sizeof(recorded), "a command of %lu bytes", size);
	recorded_command = command;
	recorded_size = size;
	return recorded;
}

#include "under_test.inc"

#define WORDS (D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX)

static DWORD *game_word(unsigned long index)
{
	return index < D3DRS_MAX ? &D3D__RenderState[index] : &(&D3D__TextureState[0][0])[index - D3DRS_MAX];
}

static DWORD before[WORDS];

/* a draw begun: the states as they were */
static void draw_begin(void)
{
	unsigned long index;

	for (index = 0; index < WORDS; index++)
		before[index] = *game_word(index);
}

/* ... and drawn, the states changed since synced and the command run: the
renderer's states must be the game's, and the command must name each state
that changed once and no other */
static void draw_end(void)
{
	unsigned long index, changed = 0;

	for (index = 0; index < WORDS; index++)
		changed += before[index] != *game_word(index);
	recorded_command = NULL;
	states_sync();
	if (!changed)
	{
		CHECK(!recorded_command, "a command with nothing changed");
		return;
	}
	CHECK(recorded_command == states_command, "no states command");
	CHECK(recorded[0] == changed, "%lu pairs for %lu states changed", (unsigned long)recorded[0], changed);
	CHECK(recorded_size == (1 + changed * 2) * sizeof(DWORD), "a command of %lu bytes", recorded_size);
	states_command(recorded);
	CHECK(!memcmp(render_states, D3D__RenderState, sizeof(render_states)), "the render states differ");
	CHECK(!memcmp(texture_states, D3D__TextureState, sizeof(texture_states)), "the texture states differ");
}

int main(int argc, char **argv)
{
	const char *test = argc > 1 ? argv[1] : "";
	int round, i;

	srand(1);
	if (!strcmp(test, "random"))
	{
		/* a few states a draw, anywhere */
		for (round = 0; round < 20000; round++)
		{
			int count = rand() % 8;

			draw_begin();
			for (i = 0; i < count; i++)
			{
				DWORD *word = game_word((unsigned long)rand() % WORDS);

				/* (a quarter left as they were) */
				if (rand() & 3)
					*word = (DWORD)rand() * 2654435761UL;
			}
			draw_end();
		}
	}
	else if (!strcmp(test, "edges"))
	{
		/* each state alone, and either side of every group's edge */
		unsigned long index;

		for (index = 0; index < WORDS; index++)
		{
			draw_begin();
			*game_word(index) += 1;
			draw_end();
			draw_begin();
			*game_word(index ? index - 1 : WORDS - 1) += 1;
			*game_word((index + 1) % WORDS) += 1;
			draw_end();
		}
	}
	else
	{
		fprintf(stderr, "no case %s\n", test);
		return 2;
	}
	return 0;
}
