/*
OBJECTS_YIELD.C (test)

The real objects_update (objects.c) over a fake world: a tick in pieces
(display.tick_slices) hands back between the updates of the objects that
were active, and between the first updates of those made this tick (a
squad a script placed), never in the middle of one.
test_objects_yield.py takes the enums (enums.inc) and the code
(under_test.inc) from the sources.
*/

#include "harness.h"
#include "enums.inc"

#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define DATUM_INDEX_NEW(absolute_index, identifier) ((long)(((unsigned long)(unsigned short)(identifier) << 16) | (absolute_index)))
#define BIT_VECTOR_SIZE_IN_BYTES(count) ((((count) + 31) >> 5) * 4)
#define BIT_VECTOR_TEST_FLAG(vector, bit) (((vector)[(bit) >> 5] & (1UL << ((bit) & 31))) != 0)
#define csmemcmp memcmp

struct profile_section { const char *name; long index; boolean enabled; };
#define profile_enter(section) ((void)(section))
#define profile_exit(section) ((void)(section))

/* objects: the members objects_update reads */
struct object_datum { struct { unsigned long flags; } object; };
struct object_header_datum
{
	short identifier;
	unsigned short flags;
	short cluster_index;
	unsigned char type;
	struct object_datum *datum;
};
enum { OBJECTS = 64, CLUSTERS = 4 };
static struct object_datum object_data[OBJECTS];
static struct object_header_datum headers[OBJECTS];
struct object_header_array { long count; struct object_header_datum *data; };
static struct object_header_array object_header_table = { 0, headers }, *object_header_data = &object_header_table;
static unsigned long last_bits[1], active_bits[1], pvs_bits[1] = { 0xF };
static struct { long active_garbage_object_count; unsigned long *last_active_cluster_bits, *active_cluster_bits; }
	object_globals_storage = { 0, last_bits, active_bits }, *object_globals = &object_globals_storage;
static struct { struct { short count; } clusters; } bsp = { { CLUSTERS } };
#define global_structure_bsp_get() (&bsp)
#define players_get_combined_pvs() (pvs_bits)
#define game_time_get() 0L
#define game_players_are_double_speed() FALSE
#define unit_get(object_index) ((struct { struct { short player_index; } unit; } *)NULL)
#define object_activate(index) ((void)0)
#define object_deactivate(index) ((void)0)
#define object_delete_initial_recursive(index, b) ((void)0)
#define structure_decals_update(last, active, count) ((void)0)
#define objects_garbage_collection() ((void)0)

/* what happens, in order: an object's update (its index) or a hand back
(-1), and the objects deleted */
static long events[4 * OBJECTS];
static int event_count, updating, deleted;
static void record(long event)
{
	CHECK(event_count < 4 * OBJECTS, "too many events");
	events[event_count++] = event;
}
static boolean object_update(long object_index)
{
	CHECK(!updating, "an object's update began within another's");
	updating = 1;
	record(DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index));
	updating = 0;
	return TRUE;
}
static void tick_slices_yield(void)
{
	CHECK(!updating, "the tick handed back within an object's update");
	record(-1);
}
static void object_delete_recursive(long object_index, boolean b)
{
	deleted++;
}

#pragma clang diagnostic ignored "-Wlogical-op-parentheses" /* (the game's code, as it is) */
#include "under_test.inc"

/* objects 0..active-1 active, then created ones, made this tick */
static void world(int active, int created)
{
	int index;

	object_header_table.count = active + created;
	for (index = 0; index < active + created; index++)
	{
		headers[index].identifier = (short)(0x1000 + index);
		headers[index].datum = &object_data[index];
		headers[index].cluster_index = 0;
		headers[index].type = 0;
		headers[index].flags = index < active ? FLAG(_object_header_active_bit) :
			FLAG(_object_header_active_bit) | FLAG(_object_header_being_created_bit);
	}
}

/* each update in the events is followed by a hand back */
static void each_update_handed_back(int updates)
{
	int index, seen = 0;

	for (index = 0; index < event_count; index++)
	{
		if (events[index] < 0)
			continue;
		seen++;
		CHECK(index + 1 < event_count && events[index + 1] == -1, "object %ld's update not followed by a hand back",
			events[index]);
	}
	CHECK(seen == updates, "%d updates, not %d", seen, updates);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("active")
	{
		world(10, 0);
		objects_update();
		each_update_handed_back(10);
		return 0;
	}
	CASE("created")
	{
		int index;

		/* a squad of 40 placed this tick: their first updates come after
		the active objects', each followed by a hand back */
		world(8, 40);
		objects_update();
		each_update_handed_back(48);
		for (index = 0; index < 48; index++)
			CHECK(headers[index].flags == FLAG(_object_header_active_bit), "object %d's flags %x", index,
				headers[index].flags);
		CHECK(events[2 * 8] == 8 && events[2 * 47] == 47, "the created objects not updated after the active ones");
		return 0;
	}
	CASE("deleted")
	{
		/* (one made and deleted the same tick: updated, handed back, deleted) */
		world(2, 2);
		headers[3].flags |= FLAG(_object_header_being_deleted_bit);
		objects_update();
		each_update_handed_back(4);
		CHECK(deleted == 1, "%d objects deleted", deleted);
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
