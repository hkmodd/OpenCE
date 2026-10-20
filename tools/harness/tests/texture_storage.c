#include "harness.h"
#include "defines.inc"

typedef unsigned long DWORD;
typedef unsigned long D3DCOLOR;
typedef int BOOL;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_RED 0x1903
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#define GL_ALPHA 0x1906
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_RGBA8 0x8058
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define DXT1 0x83F1

#include "types.inc"

/* the formats of the cases: DXT1, or 32-bit texels */
enum { FORMAT_ARGB = 0x06, FORMAT_DXT1 = 0x0C };
enum { _texel_argb, _texel_dxt1 };
enum { _custom_edition_channels_xbox, _custom_edition_channels_pc, NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS };
static const unsigned char custom_edition_channel_sources[NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS][4] =
	{ { 0, 1, 2, 3 }, { 2, 1, 0, 3 } };
#ifdef HALO_GLES
static struct { BOOL s3tc; } xgpu_capabilities = { TRUE };
static void dxt_decode_level(unsigned char kind, const unsigned char *source, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long *destination)
{
}
#endif

static struct format_information format_information(DWORD format)
{
	struct format_information information = { 0 };

	information.kind = format == FORMAT_DXT1 ? _texel_dxt1 : _texel_argb;
	return information;
}

static GLenum compressed_format(unsigned char kind)
{
	return DXT1;
}

#include "helpers.inc"

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * 8 * depth;
	return width * height * depth * 4;
}

static unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0, index;

	for (index = 0; index < level; index++)
		offset += level_bytes(description, index);
	return offset;
}

static unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	return xgpu_texture_level_offset(description, description->levels);
}

static BOOL decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination)
{
	return TRUE;
}

static void texture_dump(GLenum target, const struct xgpu_texture_description *description)
{
}

static void xgpu_gl_state_invalidate(void)
{
}

#define platform_log(...) ((void)0)

/* ---------- a driver's textures: what it refuses is counted as an error */

#define FAKE_TEXTURES 4
#define FAKE_LEVELS 16
static struct fake_texture
{
	BOOL immutable;
	GLsizei storage_levels;
	GLenum storage_format;
	BOOL defined[6][FAKE_LEVELS];
	GLenum format[6][FAKE_LEVELS];
	GLsizei width[6][FAKE_LEVELS], height[6][FAKE_LEVELS];
	const void *texels[6][FAKE_LEVELS];
} textures[FAKE_TEXTURES];
static GLuint bound_2d, bound_3d, bound_cube;
/* refused calls; glTexStorage2D calls; levels defined (glTexImage, glCompressedTexImage), of them levels defined
again, and levels replaced (glTexSubImage, glCompressedTexSubImage) */
static unsigned long errors, storage_calls, defines, redefines, replaces;

static void refused(const char *what)
{
	fprintf(stderr, "refused: %s\n", what);
	errors++;
}

static struct fake_texture *bound(GLenum target, unsigned long *face)
{
	GLuint name;

	*face = 0;
	if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6)
	{
		*face = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
		name = bound_cube;
	}
	else
	{
		name = target == GL_TEXTURE_2D ? bound_2d : target == GL_TEXTURE_3D ? bound_3d : 0;
	}
	return name && name < FAKE_TEXTURES ? &textures[name] : NULL;
}

static void glBindTexture(GLenum target, GLuint texture)
{
	if (target == GL_TEXTURE_2D)
		bound_2d = texture;
	else if (target == GL_TEXTURE_3D)
		bound_3d = texture;
	else if (target == GL_TEXTURE_CUBE_MAP)
		bound_cube = texture;
	else
		refused("binding to an unknown target");
}

static void glTexParameteri(GLenum target, GLenum name, GLint value)
{
}

static void glPixelStorei(GLenum name, GLint value)
{
}

static void glTexStorage2D(GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height)
{
	unsigned long face, level;
	GLsizei side = width > height ? width : height, full = 1;
	struct fake_texture *texture = bound(target, &face);

	storage_calls++;
	while (side >>= 1)
		full++;
	if (target != GL_TEXTURE_2D || !texture)
		{ refused("storage for no 2D texture"); return; }
	if (texture->immutable)
		{ refused("storage for a texture that has it"); return; }
	if (levels < 1 || levels > full || levels > FAKE_LEVELS)
		{ refused("storage for more levels than the size has"); return; }
	texture->immutable = TRUE;
	texture->storage_levels = levels;
	texture->storage_format = format;
	for (level = 0; level < (unsigned long)levels; level++)
	{
		texture->defined[0][level] = TRUE;
		texture->format[0][level] = format;
		texture->width[0][level] = (GLsizei)level_dimension((unsigned long)width, level);
		texture->height[0][level] = (GLsizei)level_dimension((unsigned long)height, level);
		texture->texels[0][level] = NULL;
	}
}

static void define(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, const void *texels)
{
	unsigned long face;
	struct fake_texture *texture = bound(target, &face);

	if (!texture || level < 0 || level >= FAKE_LEVELS)
		{ refused("a level of no texture"); return; }
	if (texture->immutable)
		{ refused("a level of a texture whose storage is immutable defined again"); return; }
	defines++;
	redefines += texture->defined[face][level];
	texture->defined[face][level] = TRUE;
	texture->format[face][level] = format;
	texture->width[face][level] = width;
	texture->height[face][level] = height;
	texture->texels[face][level] = texels;
}

static void replace(GLenum target, GLint level, GLint x, GLint y, GLenum format, GLsizei width, GLsizei height,
	const void *texels)
{
	unsigned long face;
	struct fake_texture *texture = bound(target, &face);

	if (!texture || level < 0 || level >= FAKE_LEVELS || !texture->defined[face][level])
		{ refused("texels replaced in a level that has no storage"); return; }
	if (x || y || width != texture->width[face][level] || height != texture->height[face][level])
		{ refused("texels replaced in part of a level, or outside it"); return; }
	if (format != texture->format[face][level])
		{ refused("texels replaced in a level of another format"); return; }
	replaces++;
	texture->texels[face][level] = texels;
}

static void glTexImage2D(GLenum target, GLint level, GLint format, GLsizei width, GLsizei height, GLint border,
	GLenum layout, GLenum type, const void *texels)
{
	define(target, level, (GLenum)format, width, height, texels);
}

static void glTexImage3D(GLenum target, GLint level, GLint format, GLsizei width, GLsizei height, GLsizei depth,
	GLint border, GLenum layout, GLenum type, const void *texels)
{
	define(target, level, (GLenum)format, width, height, texels);
}

static void glCompressedTexImage2D(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height,
	GLint border, GLsizei bytes, const void *texels)
{
	define(target, level, format, width, height, texels);
}

static void glCompressedTexImage3D(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height,
	GLsizei depth, GLint border, GLsizei bytes, const void *texels)
{
	define(target, level, format, width, height, texels);
}

static void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum layout, GLenum type, const void *texels)
{
	/* (the levels are RGBA8; the texels given are BGRA bytes) */
	if (layout != GL_BGRA || type != GL_UNSIGNED_BYTE)
		{ refused("texels of another layout"); return; }
	replace(target, level, x, y, GL_RGBA8, width, height, texels);
}

static void glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum format, GLsizei bytes, const void *texels)
{
	replace(target, level, x, y, format, width, height, texels);
}

#include "under_test.inc"

/* ---------- cases */

static unsigned char memory[1 << 20];

static struct xgpu_texture_description describe(DWORD format, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long levels, BOOL cube_map)
{
	struct xgpu_texture_description description;

	memset(&description, 0, sizeof(description));
	description.format = format;
	description.width = width;
	description.height = height;
	description.depth = depth;
	description.levels = levels;
	description.cube_map = cube_map;
	description.compressed = format == FORMAT_DXT1;
	return description;
}

static GLenum target_of(const struct xgpu_texture_description *description)
{
	return description->cube_map ? GL_TEXTURE_CUBE_MAP : description->depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
}

/* a texture uploaded, then uploaded again (its pages written since): with storage, made once and its levels'
texels replaced, each from its place in the texels; without, its levels defined as before */
static void uploads(struct xgpu_texture_description description, BOOL expect_storage)
{
	GLuint name = 1;
	BOOL storage = FALSE;
	unsigned long level, pass, levels = description.levels * (description.cube_map ? 6 : 1);

	for (pass = 0; pass < 2; pass++)
	{
		upload(name, target_of(&description), &description, memory, NULL, _custom_edition_channels_xbox, &storage);
		CHECK(errors == 0, "upload %lu: %lu calls the driver refuses", pass + 1, errors);
	}
	CHECK(storage == expect_storage, "the texture's storage remembered as %d", storage);
	if (expect_storage)
	{
		CHECK(storage_calls == 1, "storage made %lu times", storage_calls);
		CHECK(textures[name].storage_levels == (GLsizei)description.levels, "storage of %d levels for %lu",
			textures[name].storage_levels, description.levels);
		CHECK(textures[name].storage_format == (description.compressed ? DXT1 : GL_RGBA8), "storage of format %04x",
			textures[name].storage_format);
		CHECK(defines == 0, "%lu levels defined, %lu of them again", defines, redefines);
		CHECK(replaces == 2 * levels, "%lu levels' texels replaced, of %lu", replaces, 2 * levels);
	}
	else
	{
		CHECK(storage_calls == 0, "storage made %lu times", storage_calls);
		CHECK(replaces == 0, "%lu levels' texels replaced", replaces);
		CHECK(defines == 2 * levels, "%lu levels defined, of %lu", defines, 2 * levels);
	}
	if (description.compressed)
	{
		for (level = 0; level < description.levels; level++)
		{
			CHECK(textures[name].texels[0][level] == memory + xgpu_texture_level_offset(&description, level),
				"level %lu's texels not from its place", level);
		}
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("dxt")
	{
		uploads(describe(FORMAT_DXT1, 256, 256, 1, 9, FALSE), TRUE);
	}
	CASE("dxt_part")
	{
		/* fewer levels than the size has */
		uploads(describe(FORMAT_DXT1, 128, 128, 1, 4, FALSE), TRUE);
	}
	CASE("converted")
	{
		uploads(describe(FORMAT_ARGB, 64, 32, 1, 7, FALSE), TRUE);
	}
	CASE("narrow")
	{
		uploads(describe(FORMAT_ARGB, 128, 8, 1, 8, FALSE), TRUE);
	}
	CASE("linear")
	{
		uploads(describe(FORMAT_ARGB, 640, 480, 1, 1, FALSE), TRUE);
	}
	CASE("too_many_levels")
	{
		/* a map's header can say more levels than the size has: uploaded as before */
		uploads(describe(FORMAT_DXT1, 4, 4, 1, 5, FALSE), FALSE);
	}
	CASE("cube")
	{
		uploads(describe(FORMAT_DXT1, 16, 16, 1, 5, TRUE), FALSE);
	}
	CASE("volume")
	{
		uploads(describe(FORMAT_ARGB, 16, 16, 4, 3, FALSE), FALSE);
	}
	CASE("gles")
	{
		/* OpenGL ES uploads as before */
		uploads(describe(FORMAT_DXT1, 256, 256, 1, 9, FALSE), FALSE);
	}
	return 0;
}
