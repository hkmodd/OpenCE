#include "harness.h"

/* A tag found by its group and name (cache_files.c): tag_loaded_at, given
the index a reference holds, must find what tag_loaded's walk of the tags
finds, for any index, right or not. */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

static struct { boolean tags_loaded; } cache_file_globals;
static struct cache_file_tag_instance *global_tag_instances;
static long global_tag_count;

/* (the C library's, which the Windows build calls) */
static int _stricmp(char const *a, char const *b)
{
	int x, y;

	do
	{
		x = (unsigned char)*a++;
		y = (unsigned char)*b++;
		if (x >= 'A' && x <= 'Z')
			x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z')
			y += 'a' - 'A';
	} while (x && x == y);
	return x - y;
}

#include "under_test.inc"

#define TAGS 3000
#define ROOM (TAGS + 64)
/* every absolute index a tag's index can name (a lookup that does not
check the count reads inside them, empty past the room) */
#define INDICES 0x10000
static struct cache_file_tag_instance instances[INDICES];
static char names[INDICES][32];
static long const groups[] = { 0x6269746D, 0x756E6869, 0x68756467, 0x736E6421, 0x77706869 };

static unsigned long random_state = 2342;

static unsigned long random_next(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return random_state >> 8;
}

/* a map's tags: each name of a group once, at the index its tag's index
names, with a salt; the room past the count holds tags of names in it */
static void make_tags(void)
{
	long index;

	for (index = 0; index < ROOM; index++)
	{
		instances[index].group_tag = groups[random_next() % 5];
		instances[index].tag_index = (long)(((0xE741UL + index) << 16) | (unsigned long)index);
		sprintf(names[index], "%s\\tag %ld", index % 3 ? "ui\\hud\\bitmaps" : "Characters\\Elite", index);
		instances[index].name = names[index];
		instances[index].base_address = &instances[index];
	}
	/* past the count: a copy of the first tags under other indices */
	for (index = TAGS; index < ROOM; index++)
	{
		instances[index].group_tag = instances[index - TAGS].group_tag;
		instances[index].name = names[index - TAGS];
	}
	global_tag_instances = instances;
	global_tag_count = TAGS;
	cache_file_globals.tags_loaded = TRUE;
}

/* the name with its letters' case changed */
static char const *other_case(char const *name)
{
	static char changed[32];
	int i;

	for (i = 0; name[i]; i++)
	{
		char c = name[i];

		changed[i] = c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
	}
	changed[i] = 0;
	return changed;
}

static void check_lookup(long group_tag, char const *name, long tag_index)
{
	long walked = tag_loaded(group_tag, name), found = tag_loaded_at(group_tag, name, tag_index);

	CHECK(found == walked, "group %08lX \"%s\" at %08lX: %08lX, the walk finds %08lX", group_tag, name, tag_index, found,
		walked);
}

/* every tag looked up by its own index, others, and none */
static void check_every_tag(void)
{
	long index;

	for (index = 0; index < ROOM; index++)
	{
		struct cache_file_tag_instance const *instance = &instances[index];
		long other = instances[random_next() % TAGS].tag_index;
		long group;

		check_lookup(instance->group_tag, instance->name, instance->tag_index);
		check_lookup(instance->group_tag, other_case(instance->name), instance->tag_index);
		check_lookup(instance->group_tag, instance->name, other);
		check_lookup(instance->group_tag, instance->name, NONE);
		check_lookup(instance->group_tag, instance->name, instance->tag_index ^ 0x10000);
		check_lookup(instance->group_tag, instance->name, (long)(instance->tag_index | 0xFFFF));
		if (index < ROOM - TAGS)
			check_lookup(instance->group_tag, instance->name, instances[TAGS + index].tag_index);
		for (group = 0; group < 5; group++)
			check_lookup(groups[group], instance->name, instance->tag_index);
		check_lookup(instance->group_tag, "ui\\hud\\bitmaps\\no tag", instance->tag_index);
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	make_tags();
	CASE("every-tag")
	{
		check_every_tag();
		return 0;
	}
	CASE("unloaded")
	{
		/* no map's tags: nothing found */
		cache_file_globals.tags_loaded = FALSE;
		check_every_tag();
		CHECK(tag_loaded_at(instances[5].group_tag, instances[5].name, instances[5].tag_index) == NONE,
			"a tag found with no tags loaded");
		return 0;
	}
	CASE("menu-tags")
	{
		/* the menus' table (menu_tags.c): its slots not made yet have no
		group, no index and an empty name */
		long index;

		for (index = TAGS - 40; index < TAGS; index++)
		{
			instances[index].group_tag = NONE;
			instances[index].tag_index = NONE;
			names[index][0] = 0;
		}
		check_every_tag();
		check_lookup(NONE, "", NONE);
		check_lookup(NONE, "", (long)(TAGS - 1));
		return 0;
	}
	return 2;
}
