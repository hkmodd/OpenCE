/*
TEXTURE_HUD_SETTING.C (test)

The texture cache's choice of a texture for a draw (xbox_textures.c) over fake
textures: display.high_res_hud is asked for each texture of each draw, and must
be read from the settings only once they have changed, with its value then
followed. test_texture_hud_setting.py takes the code (under_test.inc) from the
sources.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_CUBE_MAP 0x8513

/* the settings: how often the setting is read, and the count of changes */
static unsigned long changes, reads;
static int setting;
static unsigned long config_changes(void) { return changes; }
static int config_boolean(const char *name)
{
	CHECK(strcmp(name, "display.high_res_hud") == 0, "read %s", name);
	reads++;
	return setting;
}

/* textures: the members the code reads */
struct xgpu_texture_description
{
	DWORD format;
	unsigned long width, height, depth, levels;
	BOOL cube_map, linear, compressed;
	unsigned long pitch;
	BOOL hires, hires_coverage, hires_point_threshold;
};
struct texture_entry
{
	DWORD data;
	GLuint texture;
	GLenum target;
	struct xgpu_texture_description description;
	unsigned long address, size;
	long override;
	GLuint hud_upscale;
	unsigned long hud_upscale_levels;
	BOOL hud_upscale_checked;
};

/* the other replacements: none */
static GLuint text_hires_atlas_texture(DWORD data) { return 0; }
static GLuint menu_art_texture(DWORD data, unsigned long *levels) { return 0; }
static GLuint hud_hires_placeholder_texture(DWORD data, unsigned long *levels) { return 0; }
static GLuint hud_hires_override_texture(long override, unsigned long *levels) { return 0; }
static BOOL hud_hires_override_coverage(long override) { return FALSE; }
static BOOL hud_hires_override_point_threshold(long override) { return FALSE; }

/* a HUD sheet upscaled: every texture is one, as texture 1000 + its own */
enum { _texel_p8 = 1, _texel_plain, _custom_edition_channels_xbox = 0 };
struct format_information { int kind; };
static struct format_information format_information(DWORD format)
{
	struct format_information information = { _texel_plain };

	return information;
}
static long upscale_checks;
static long hud_hires_upscale_at(unsigned long address, unsigned long width, unsigned long height)
{
	upscale_checks++;
	return 1;
}
static int custom_edition_texels_order(unsigned long address) { return _custom_edition_channels_xbox; }
static BOOL platform_is_contiguous(void *address) { return TRUE; }
static BOOL decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const void *palette, unsigned long *pixels) { return TRUE; }
static GLuint upscaled_for;
static GLuint hud_hires_upscale_texture(const unsigned char *pixels, unsigned long width, unsigned long height,
	long kind, unsigned long *levels)
{
	*levels = 1;
	return 1000 + upscaled_for;
}

#include "under_test.inc"

enum { TEXTURES = 64, DRAWS = 2000 };
static unsigned char texels[TEXTURES][16 * 16 * 4];
static struct texture_entry entries[TEXTURES];

static void textures_new(void)
{
	int i;

	memset(entries, 0, sizeof(entries));
	for (i = 0; i < TEXTURES; i++)
	{
		entries[i].texture = (GLuint)(i + 1);
		entries[i].target = GL_TEXTURE_2D;
		entries[i].description.width = entries[i].description.height = 16;
		entries[i].description.levels = 5;
		entries[i].address = (unsigned long)texels[i];
		entries[i].size = sizeof(texels[i]);
		entries[i].override = NONE;
	}
}

/* a frame's draws, each texture of each draw asked for; returns how many drew an upscaled sheet */
static int frame(void)
{
	int draw, upscaled = 0;

	for (draw = 0; draw < DRAWS; draw++)
	{
		struct texture_entry *entry = &entries[draw % TEXTURES];
		struct xgpu_texture_description description;
		GLenum target;
		GLuint texture;

		upscaled_for = entry->texture;
		texture = texture_entry_result(entry, &target, &description);
		CHECK(target == GL_TEXTURE_2D, "target %u", target);
		if (texture == 1000 + entry->texture)
		{
			CHECK(description.hires && description.levels == 1, "upscaled sheet's description");
			upscaled++;
		}
		else
		{
			CHECK(texture == entry->texture && !description.hires && description.levels == 5,
				"texture %u for %u", texture, entry->texture);
		}
	}
	return upscaled;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	int frames;

	textures_new();
	CASE("reads-once")
	{
		/* frames of draws, the settings unchanged: one read in all */
		setting = TRUE;
		for (frames = 0; frames < 10; frames++)
			CHECK(frame() == DRAWS, "frame %d drew the map's sheets", frames);
		CHECK(reads == 1, "the setting read %lu times for %d draws", reads, 10 * DRAWS);
		return 0;
	}
	CASE("follows-a-change")
	{
		setting = FALSE;
		CHECK(frame() == 0, "upscaled with the setting off");
		setting = TRUE;
		CHECK(frame() == 0, "the setting followed before the settings changed");
		changes++;
		CHECK(frame() == DRAWS, "the setting turned on not followed");
		setting = FALSE;
		changes++;
		CHECK(frame() == 0, "the setting turned off not followed");
		CHECK(reads == 3, "the setting read %lu times for 3 changes", reads);
		return 0;
	}
	CASE("off-keeps-the-map")
	{
		/* off, a sheet is never looked at for an upscale */
		setting = FALSE;
		for (frames = 0; frames < 3; frames++)
			CHECK(frame() == 0, "upscaled with the setting off");
		CHECK(upscale_checks == 0, "%ld sheets looked at with the setting off", upscale_checks);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
