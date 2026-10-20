/*
STRUCTURE_BSP_IMAGES.C

A map's structure bsps as their loads left them, read and checked
(cache_files.c), kept while the map's tags are loaded: a bsp loaded again is
copied back to where it was read, not read from the map's file and checked
again. A copy is the same bytes as that read and check make: the map's file
does not change while it is open, and what the checks correct depends only on
the bsp's bytes and the map's tags. The map's first bsp load reads and keeps
every bsp of the map, in its loading screen, so that a bsp switch while it is
played neither reads nor checks one. What a bsp's load builds from its bytes
(its culling buckets: structure_culling.c) may be kept with it, attached.
The desktop builds only, and no more than MAXIMUM_KEPT_BYTES: a bsp not kept
is read and checked as before.
*/

/* ---------- headers */

#include "cseries.h"
#include "structure_bsp_images.h"
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#ifdef HALO_GLES
#define MAXIMUM_KEPT_BYTES 0UL
#else
#define MAXIMUM_KEPT_BYTES (128UL * 1024 * 1024)
#endif
/* a scenario's bsps (MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO) */
#define MAXIMUM_KEPT_IMAGES 16

/* ---------- structures */

struct structure_bsp_image
{
	long tag_index;
	long file_offset;
	long size;
	void *address;
	void *bytes;
	void *attachment;
	void (*attachment_dispose)(void *data);
};

/* ---------- globals */

static struct
{
	struct structure_bsp_image images[MAXIMUM_KEPT_IMAGES];
	short count;
	/* the image the bsp loaded last is, or NONE */
	short current;
	unsigned long bytes;
	/* the map's bsps were read and kept */
	boolean kept;
} structure_bsp_images = {{{0}}, 0, NONE, 0, FALSE};

/* ---------- private code */

/* what was attached to an image goes (dispose, then its bytes) */
static void structure_bsp_image_detach(
	struct structure_bsp_image *image)
{
	if (image->attachment)
	{
		image->attachment_dispose(image->attachment);
		free(image->attachment);
	}
	image->attachment = NULL;
	image->attachment_dispose = NULL;

	return;
}

static short structure_bsp_image_find(
	long tag_index,
	long file_offset,
	long size,
	void const *address)
{
	short image_index;

	for (image_index = 0; image_index < structure_bsp_images.count; image_index++)
	{
		struct structure_bsp_image const *image = &structure_bsp_images.images[image_index];

		if (image->tag_index == tag_index && image->file_offset == file_offset && image->size == size &&
			image->address == address)
		{
			return image_index;
		}
	}

	return NONE;
}

/* ---------- public code */

/* whether the map's bsps were read and kept (structure_bsp_images_keep_begun),
or none can be (and they are not read ahead) */
boolean structure_bsp_images_kept(
	void)
{
	return structure_bsp_images.kept || MAXIMUM_KEPT_BYTES == 0;
}

/* the map's bsps are being read and kept: they are not again until its tags
are unloaded */
void structure_bsp_images_keep_begun(
	void)
{
	structure_bsp_images.kept = TRUE;

	return;
}

/* the size bytes kept for the bsp read from file_offset to address, copied
back there: FALSE when it was not kept (and must be read and checked) */
boolean structure_bsp_image_restore(
	long tag_index,
	long file_offset,
	long size,
	void *address)
{
	short image_index = structure_bsp_image_find(tag_index, file_offset, size, address);

	structure_bsp_images.current = image_index;
	if (image_index == NONE)
	{
		return FALSE;
	}
	memcpy(address, structure_bsp_images.images[image_index].bytes, (size_t)size);

	return TRUE;
}

/* the size bytes at address, a bsp just read from file_offset and checked,
kept for when it is loaded again (if they fit) */
void structure_bsp_image_keep(
	long tag_index,
	long file_offset,
	long size,
	void const *address)
{
	struct structure_bsp_image *image;
	void *bytes;

	structure_bsp_images.current = NONE;
	if (size <= 0 || structure_bsp_images.count >= MAXIMUM_KEPT_IMAGES ||
		(unsigned long)size > MAXIMUM_KEPT_BYTES - structure_bsp_images.bytes ||
		structure_bsp_image_find(tag_index, file_offset, size, address) != NONE)
	{
		return;
	}
	bytes = malloc((size_t)size);
	if (!bytes)
	{
		return;
	}
	memcpy(bytes, address, (size_t)size);
	image = &structure_bsp_images.images[structure_bsp_images.count];
	image->tag_index = tag_index;
	image->file_offset = file_offset;
	image->size = size;
	image->address = (void *)address;
	image->bytes = bytes;
	image->attachment = NULL;
	image->attachment_dispose = NULL;
	structure_bsp_images.bytes += (unsigned long)size;
	structure_bsp_images.current = structure_bsp_images.count++;

	return;
}

/* what was attached to the image the bsp loaded last is, or NULL */
void *structure_bsp_image_attachment(
	void)
{
	return structure_bsp_images.current != NONE ?
		structure_bsp_images.images[structure_bsp_images.current].attachment :
		NULL;
}

/* size bytes, zeroed, kept with the image the bsp loaded last is, for what is
built from it (in place of what was attached, which goes): NULL when it has no
image, or they cannot be allocated. dispose is given them before they go */
void *structure_bsp_image_attach(
	long size,
	void (*dispose)(void *data))
{
	struct structure_bsp_image *image;
	void *data;

	if (structure_bsp_images.current == NONE || size <= 0)
	{
		return NULL;
	}
	/* the game's malloc (cseries.h), which its free takes: not the C library's calloc */
	data = malloc((size_t)size);
	if (!data)
	{
		return NULL;
	}
	memset(data, 0, (size_t)size);
	image = &structure_bsp_images.images[structure_bsp_images.current];
	structure_bsp_image_detach(image);
	image->attachment = data;
	image->attachment_dispose = dispose;

	return data;
}

/* the map's tags are unloaded: every image goes, with what was attached */
void structure_bsp_images_dispose(
	void)
{
	short image_index;

	for (image_index = 0; image_index < structure_bsp_images.count; image_index++)
	{
		struct structure_bsp_image *image = &structure_bsp_images.images[image_index];

		structure_bsp_image_detach(image);
		free(image->bytes);
	}
	memset(&structure_bsp_images, 0, sizeof(structure_bsp_images));
	structure_bsp_images.current = NONE;

	return;
}
