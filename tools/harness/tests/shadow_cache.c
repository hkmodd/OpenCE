/* The shadow cache's choices (rasterizer_xbox_shadows.c): which record is drawn from a kept map, which map is kept. */
#include "harness.h"
typedef struct fake_texture { int name; } D3DTexture;
typedef struct fake_surface { int name; } D3DSurface;
#include "types.inc"
enum { _rasterizer_target_render_primary = 1 };
static struct { short rasterizer_target; } global_window_parameters = { _rasterizer_target_render_primary };
static struct { boolean draw_environment_shadows, shadow_convolution_enabled; } rasterizer_debug_options = { TRUE, TRUE };
static boolean shadow_recording, shadow_active, shadow_convolved, shadow_setup, shadow_used, shadow_restored = TRUE;
static long shadow_record_size, shadow_record[SHADOW_RECORD_MAXIMUM];
static short shadow_cache_entry = NONE, shadow_store_entry = NONE;
static unsigned long shadow_cache_pass;
static long shadow_cache_scale;
static struct shadow_cache_entry shadow_cache[SHADOW_CACHE_ENTRIES];
static int replays, creates, copies, refuse_maps;
static D3DTexture textures[SHADOW_CACHE_ENTRIES + 1];
static D3DSurface surfaces[SHADOW_CACHE_ENTRIES + 1], primary = { -1 }, secondary = { -2 };
static D3DSurface *copied_from, *copied_to;
#define csmemcpy memcpy
#define csmemset memset
#define error(...) ((void)0)
#define rasterizer_set_target(...) ((void)0)
static void shadow_record_replay(void) { shadow_recording = FALSE; replays++; }
static boolean rasterizer_shadow_map_create(D3DTexture **texture, D3DSurface **surface)
{
	if (refuse_maps)
		return FALSE;
	CHECK(creates < SHADOW_CACHE_ENTRIES, "a map is made for each entry once");
	*texture = &textures[creates];
	*surface = &surfaces[creates++];
	return TRUE;
}
static void rasterizer_shadow_maps_get(D3DSurface **first, D3DSurface **second) { *first = &primary; *second = &secondary; }
static void halo_shadow_map_copy(D3DSurface *source, D3DSurface *destination)
{
	copies++;
	copied_from = source;
	copied_to = destination;
}
#include "under_test.inc"

#define HEADER_LONGS ((long)(sizeof(struct shadow_record_header) / sizeof(long)))
#define PAYLOAD 2

/* a pass of shadows begins (_rasterizer_environment_shadows_begin) */
static void pass(void) { shadow_cache_pass++; }

/* a shadow begun and its object's list drawn (rasterizer_environment_shadow_begin, render_object's cast) */
static void cast(long object_index, long scale, long first, long second)
{
	struct shadow_record_header *header = (struct shadow_record_header *)shadow_record;

	memset(shadow_record, 0, sizeof(shadow_record));
	header->scale = scale;
	header->convolution = rasterizer_debug_options.shadow_convolution_enabled;
	shadow_record[HEADER_LONGS] = first;
	shadow_record[HEADER_LONGS + 1] = second;
	shadow_record_size = HEADER_LONGS + PAYLOAD;
	shadow_recording = shadow_active = TRUE;
	shadow_convolved = shadow_setup = FALSE;
	shadow_cache_entry = shadow_store_entry = NONE;
	rasterizer_environment_shadow_cast(object_index);
}

/* the shadow projected (its maps blurred as it is, or by its batch) and ended */
static void end(boolean setup, boolean batch_convolved)
{
	shadow_setup = setup;
	if (batch_convolved)
		shadow_convolved = TRUE;
	shadow_used = TRUE;
	_rasterizer_environment_shadow_end();
}

/* the hash of the record cast makes, its payload changed */
static unsigned long hash_of(long first, long second)
{
	unsigned long hash = 2166136261UL;
	long index;

	shadow_record[HEADER_LONGS] = first;
	shadow_record[HEADER_LONGS + 1] = second;
	for (index = 0; index < shadow_record_size; index++)
		hash = (hash ^ (unsigned long)shadow_record[index]) * 16777619UL;
	return hash;
}

struct pair { unsigned long hash; long first, second; };
static int pair_order(const void *a, const void *b)
{
	unsigned long x = ((const struct pair *)a)->hash, y = ((const struct pair *)b)->hash;
	return (x > y) - (x < y);
}

/* two payloads whose records hash alike (by the birthday bound in 2^18 tries) */
static void collision(long *first, long *second)
{
	enum { TRIES = 1 << 18 };
	struct pair *pairs = malloc(TRIES * sizeof(*pairs));
	long index;

	cast(0x7FFF0000, 1, 0, 0); /* the record as cast makes it */
	for (index = 0; index < TRIES; index++)
	{
		pairs[index].first = index;
		pairs[index].second = (long)((unsigned long)index * 2654435761UL);
		pairs[index].hash = hash_of(pairs[index].first, pairs[index].second);
	}
	qsort(pairs, TRIES, sizeof(*pairs), pair_order);
	for (index = 1; index < TRIES && pairs[index].hash != pairs[index - 1].hash; index++)
		;
	CHECK(index < TRIES, "no two records hash alike");
	first[0] = pairs[index - 1].first;
	second[0] = pairs[index - 1].second;
	first[1] = pairs[index].first;
	second[1] = pairs[index].second;
	free(pairs);
	rasterizer_environment_shadow_cache_flush();
	replays = 0;
}

/* object 7's shadow drawn the same in passes 1 and 2: its map kept in pass 2 */
static short steady(long first, long second)
{
	short kept;

	pass();
	cast(7, 1, first, second);
	end(TRUE, FALSE);
	pass();
	cast(7, 1, first, second);
	kept = shadow_store_entry;
	CHECK(kept != NONE, "the map of an object that stood still is kept");
	CHECK(replays == 2 && shadow_active, "a shadow whose map is being kept is drawn");
	end(TRUE, FALSE);
	return kept;
}

int main(int argc, char **argv)
{
	const char *case_name;
	short kept;
	short entry;

	CHECK(argc == 2, "case name");
	case_name = argv[1];
	CASE("first")
	{
		pass();
		cast(7, 1, 10, 20);
		CHECK(replays == 1 && shadow_active && !shadow_recording, "a new shadow is drawn as it was recorded");
		CHECK(shadow_cache_entry == NONE && shadow_store_entry == NONE, "nor kept nor drawn from a kept map");
		end(TRUE, FALSE);
		CHECK(copies == 0, "nothing copied");
	}
	CASE("steady")
	{
		kept = steady(10, 20);
		CHECK(copies == 1 && copied_from == &secondary && copied_to == shadow_cache[kept].surface,
			"the blurred map is copied into the kept one");
		CHECK(shadow_cache[kept].size == HEADER_LONGS + PAYLOAD && shadow_cache[kept].pass == 2,
			"the kept map's record is known once its map is copied");
		CHECK(shadow_store_entry == NONE, "the store is done");
	}
	CASE("hit")
	{
		kept = steady(10, 20);
		pass();
		cast(7, 1, 10, 20);
		CHECK(replays == 2, "a kept shadow is not drawn");
		CHECK(shadow_cache_entry == kept && !shadow_active && shadow_convolved && !shadow_recording,
			"it is drawn from its kept map, with no maps of its own to blur");
		CHECK(shadow_store_entry == NONE && shadow_cache[kept].pass == 3, "nor kept again, and in use this pass");
		end(TRUE, FALSE);
		CHECK(copies == 1, "its map is not copied again");
		pass();
		cast(8, 1, 10, 20);
		CHECK(shadow_cache_entry == kept && replays == 2, "another object drawn the same is drawn from it too");
	}
	CASE("changed")
	{
		steady(10, 20);
		pass();
		cast(7, 1, 10, 21);
		CHECK(replays == 3 && shadow_active && shadow_cache_entry == NONE, "a shadow drawn otherwise is drawn");
		CHECK(shadow_store_entry == NONE, "and not kept: its object moved");
	}
	CASE("unsteady")
	{
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		pass();
		pass();
		cast(7, 1, 10, 20);
		CHECK(shadow_store_entry == NONE, "an object not drawn the pass before is not kept");
	}
	CASE("collision")
	{
		long first[2], second[2];

		collision(first, second);
		kept = steady(first[0], second[0]);
		pass();
		cast(9, 1, first[1], second[1]);
		CHECK(replays == 3 && shadow_cache_entry == NONE, "a record that only hashes alike is drawn");
		pass();
		cast(7, 1, first[0], second[0]);
		CHECK(shadow_cache_entry == kept, "the kept record still is");
	}
	CASE("pending")
	{
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		for (entry = 0; entry < SHADOW_CACHE_ENTRIES; entry++)
			shadow_cache[entry].size = HEADER_LONGS + PAYLOAD; /* entries once kept with records as long */
		pass();
		cast(7, 1, 10, 20);
		kept = shadow_store_entry;
		CHECK(kept != NONE, "kept");
		cast(8, 1, 10, 20);
		CHECK(shadow_cache_entry == NONE && replays == 3, "a map not copied yet is not drawn from");
	}
	CASE("busy")
	{
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		pass();
		for (entry = 0; entry < SHADOW_CACHE_ENTRIES; entry++)
			shadow_cache[entry].pass = shadow_cache_pass; /* every map drawn from this pass */
		cast(7, 1, 10, 20);
		CHECK(shadow_store_entry == NONE, "no map drawn from this pass is replaced");
	}
	CASE("unblurred")
	{
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		pass();
		cast(7, 1, 10, 20);
		kept = shadow_store_entry;
		end(FALSE, FALSE);
		CHECK(copies == 0 && shadow_cache[kept].size == 0, "a map never blurred (projected on nothing) is not kept");
		pass();
		cast(7, 1, 10, 20);
		CHECK(shadow_cache_entry == NONE && replays == 3, "and not drawn from");
	}
	CASE("batch-blurred")
	{
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		pass();
		cast(7, 1, 10, 20);
		kept = shadow_store_entry;
		end(FALSE, TRUE);
		CHECK(copies == 1 && copied_from == &secondary && shadow_cache[kept].size != 0, "a map its batch blurred is kept");
		rasterizer_debug_options.shadow_convolution_enabled = FALSE;
		pass();
		cast(8, 1, 30, 40);
		end(FALSE, FALSE);
		pass();
		cast(8, 1, 30, 40);
		end(FALSE, FALSE);
		CHECK(copies == 2 && copied_from == &primary, "unblurred shadows keep the map they draw");
	}
	CASE("map-refused")
	{
		refuse_maps = TRUE;
		pass();
		cast(7, 1, 10, 20);
		end(TRUE, FALSE);
		pass();
		cast(7, 1, 10, 20);
		CHECK(shadow_store_entry == NONE && replays == 2, "with no map to keep it in, a shadow is only drawn");
		end(TRUE, FALSE);
		CHECK(copies == 0, "nothing copied");
	}
	CASE("scale")
	{
		kept = steady(10, 20);
		pass();
		cast(8, 2, 30, 40);
		CHECK(shadow_cache_scale == 2, "the size is noted");
		CHECK(shadow_cache[kept].size == 0, "the maps kept at another size are forgotten");
	}
	CASE("flush")
	{
		kept = steady(10, 20);
		rasterizer_environment_shadow_cache_flush();
		pass();
		cast(7, 1, 10, 20);
		CHECK(shadow_cache_entry == NONE && replays == 3, "a forgotten map is not drawn from");
		CHECK(shadow_store_entry == NONE, "nor is the object known");
		CHECK(shadow_cache[kept].texture != NULL, "its map is kept for reuse");
	}
	CASE("not-recording")
	{
		pass();
		shadow_recording = FALSE;
		shadow_active = TRUE;
		shadow_cache_entry = shadow_store_entry = NONE;
		rasterizer_environment_shadow_cast(7);
		CHECK(replays == 0 && shadow_active && shadow_cache_entry == NONE, "a shadow not recorded was drawn as it was begun");
	}
	return 0;
}
