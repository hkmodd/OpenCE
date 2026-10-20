#include "harness.h"
#include <stddef.h>

/* The tag cache that a structure bsp loads into: its pages that the
renderer copied from are watched for writes (port/linux/src/memory_watch.c),
and a write to one takes a fault. The read and the fill are the only writes */
#include "types.inc"

#define PAGE_SIZE 0x1000
#define PAGE_COUNT (TAG_CACHE_SIZE / PAGE_SIZE)
#define TAG_DATA_SIZE 0x0012A9C4
#define STRUCTURE_BSP_OFFSET 0x01154000
#define STRUCTURE_BSP_SIZE 0x004A7A31
#define TAG_BYTE 0x5A
#define STALE_BYTE 0x77

static byte *tag_cache;
static byte *structure_bsp_file;
static boolean watched[PAGE_COUNT];
static long faults, reads, prepared;
static boolean custom_edition;
static struct cache_file_globals cache_file_globals;
static struct cache_file_tag_instance structure_bsp_instance;

/* a write to size bytes at address: a fault for each watched page, which
it leaves unwatched */
static void written(void const *address, unsigned long size)
{
	long page;

	CHECK((byte const *)address >= tag_cache && (byte const *)address + size <= tag_cache + TAG_CACHE_SIZE,
		"a write of %lx bytes at %lx leaves the tag cache", size, (unsigned long)((byte const *)address - tag_cache));
	for (page = ((byte const *)address - tag_cache) / PAGE_SIZE;
		size && page <= ((byte const *)address - tag_cache + (long)size - 1) / PAGE_SIZE;
		page++)
	{
		if (watched[page])
			faults++;
		watched[page] = FALSE;
	}
}

static void memory_watch_prepare_write(void *address, unsigned long size)
{
	long page;

	for (page = ((byte *)address - tag_cache) / PAGE_SIZE;
		size && page <= ((byte *)address - tag_cache + (long)size - 1) / PAGE_SIZE;
		page++)
	{
		if (page >= 0 && page < PAGE_COUNT)
		{
			prepared += watched[page];
			watched[page] = FALSE;
		}
	}
}

static void *csmemset(void *buffer, long c, unsigned long size)
{
	written(buffer, size);
	return memset(buffer, (int)c, size);
}

/* the read's size rounded up to the sectors, as cache_file_windows.c reads;
the file layer makes its pages writable before each write (xbox_files.c) */
static short cache_file_read(long tag_index, long offset, long size, void *buffer, boolean *completion_flag_reference,
	boolean blocking)
{
	long rounded = (size + CACHE_FILE_SECTOR_SIZE - 1) & ~(CACHE_FILE_SECTOR_SIZE - 1);

	reads++;
	memory_watch_prepare_write(buffer, rounded);
	written(buffer, rounded);
	memcpy(buffer, structure_bsp_file + offset, rounded);
	*completion_flag_reference = TRUE;
	return 0;
}

#define physical_memory_get_tag_cache_base_address() ((void *)tag_cache)
#define custom_edition_cache_tags_loaded() custom_edition
#define custom_edition_structure_bsp_reference_valid(reference) TRUE
#define cache_file_structure_bsp_tag_valid(reference) TRUE
#define custom_edition_structure_bsp_load(header) TRUE
#define cache_file_structure_bsp_reference_verify(reference) TRUE
#define tag_validate_structure_bsp(index, address, size) TRUE
#define structure_bsp_header_register_vertex_buffers(header) ((void)0)
#define cache_get_tag_instance(index) (&structure_bsp_instance)
#define error(...) ((void)0)
#define SwitchToThread() ((void)0)
#define system_milliseconds() 0
#define sound_render_time() 0
#define sound_idle() ((void)0)
/* no bsp kept as its image: each is read (structure_bsp_images.c) */
static boolean cache_file_structure_bsps_keeping;
#define structure_bsp_images_kept() TRUE
#define cache_file_structure_bsps_keep() ((void)0)
static boolean structure_bsp_image_restore(long tag_index, long file_offset, long size, void *address) { return FALSE; }
static void structure_bsp_image_keep(long tag_index, long file_offset, long size, void const *address) { }
#define structure_culling_structure_bsp_kept(structure) ((void)0)

#include "under_test.inc"

/* a map's tags, then what is left of the last bsp: every page of it
watched, as the renderer leaves those it drew from */
static void map_loaded(void)
{
	long page;

	memset(tag_cache, TAG_BYTE, TAG_DATA_SIZE);
	memset(tag_cache + TAG_DATA_SIZE, STALE_BYTE, TAG_CACHE_SIZE - TAG_DATA_SIZE);
	for (page = TAG_DATA_SIZE / PAGE_SIZE; page < PAGE_COUNT; page++)
		watched[page] = TRUE;
	cache_file_globals.header.tag_data_size = TAG_DATA_SIZE;
}

/* a bsp of the map's file: its header, then bytes that are not the fill */
static void structure_bsp_file_written(struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_structure_bsp_header *header;
	long i;

	structure_bsp_file = calloc(1, STRUCTURE_BSP_SIZE + CACHE_FILE_SECTOR_SIZE);
	for (i = 0; i < STRUCTURE_BSP_SIZE + CACHE_FILE_SECTOR_SIZE; i++)
		structure_bsp_file[i] = (byte)(i * 7 + 1) == 0xCD ? 0x01 : (byte)(i * 7 + 1);
	header = (struct cache_file_structure_bsp_header *)structure_bsp_file;
	memset(header, 0, sizeof(*header));
	header->signature = CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE;
	header->base_address = tag_cache + STRUCTURE_BSP_OFFSET + sizeof(*header);
	reference->file_offset = 0;
	reference->file_size = STRUCTURE_BSP_SIZE;
	reference->base_address = tag_cache + STRUCTURE_BSP_OFFSET;
	structure_bsp_instance.group_tag = STRUCTURE_BSP_TAG;
	structure_bsp_instance.base_address = NULL;
}

/* bytes count at offset of the tag cache, each value */
static long bytes_not(long offset, long count, byte value)
{
	long i, wrong = 0;

	for (i = 0; i < count; i++)
		wrong += tag_cache[offset + i] != value;
	return wrong;
}

int main(int argc, char **argv)
{
	struct scenario_structure_bsp_reference reference;
	long rounded = (STRUCTURE_BSP_SIZE + CACHE_FILE_SECTOR_SIZE - 1) & ~(CACHE_FILE_SECTOR_SIZE - 1);
	char const *name = argc > 1 ? argv[1] : "";

	tag_cache = malloc(TAG_CACHE_SIZE);
	memset(&reference, 0, sizeof(reference));
	map_loaded();
	structure_bsp_file_written(&reference);

	if (!strcmp(name, "fill"))
	{
		/* the stock fill, of everything past the tags: no fault for each
		watched page, and the tag cache left as the stock load left it */
		CHECK(scenario_structure_bsp_load(&reference), "the bsp did not load");
		CHECK(faults == 0, "the fill took %ld faults (%ld pages made writable before it)", faults, prepared);
		CHECK(prepared == PAGE_COUNT - TAG_DATA_SIZE / PAGE_SIZE, "%ld pages were made writable", prepared);
		CHECK(bytes_not(0, TAG_DATA_SIZE, TAG_BYTE) == 0, "the tags were written");
		CHECK(bytes_not(TAG_DATA_SIZE, STRUCTURE_BSP_OFFSET - TAG_DATA_SIZE, 0xCD) == 0,
			"%ld bytes between the tags and the bsp are not the fill",
			bytes_not(TAG_DATA_SIZE, STRUCTURE_BSP_OFFSET - TAG_DATA_SIZE, 0xCD));
		CHECK(!memcmp(tag_cache + STRUCTURE_BSP_OFFSET, structure_bsp_file, rounded), "the bsp is not what was read");
		CHECK(bytes_not(STRUCTURE_BSP_OFFSET + rounded, TAG_CACHE_SIZE - STRUCTURE_BSP_OFFSET - rounded, 0xCD) == 0,
			"the bytes past the bsp are not the fill");
		CHECK(reads == 1 && structure_bsp_instance.base_address == tag_cache + STRUCTURE_BSP_OFFSET + sizeof(struct cache_file_structure_bsp_header),
			"the bsp's tag is not where it was read");
	}
	else if (!strcmp(name, "custom-edition"))
	{
		/* a Custom Edition map's bsp is served in place: nothing filled, and
		no page made writable that is not written */
		custom_edition = TRUE;
		CHECK(scenario_structure_bsp_load(&reference), "the bsp did not load");
		CHECK(bytes_not(TAG_DATA_SIZE, STRUCTURE_BSP_OFFSET - TAG_DATA_SIZE, STALE_BYTE) == 0, "the tag cache was filled");
		CHECK(faults == 0 && prepared == rounded / PAGE_SIZE + (rounded % PAGE_SIZE != 0),
			"%ld faults, %ld pages made writable", faults, prepared);
	}
	else
	{
		CHECK(FALSE, "no case %s", name);
	}
	return 0;
}
