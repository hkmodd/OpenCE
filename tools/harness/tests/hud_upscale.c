/*
HUD_UPSCALE.C (test)

The high-res HUD's upscale of a map's sheet (hud_hires.c,
hud_hires_upscale_pixels) gives, texel for texel, what the upscale written
texel by texel gives: the same Catmull-Rom sum, in the same order, for each
of the 16 output texels of a source texel and each channel, red and blue
swapped. test_hud_upscale.py takes the code (under_test.inc) from the
sources, with its SSE2 path and without it.
*/

#include "harness.h"

/* (every function's single-precision arithmetic in SSE registers, as the
game's builds have it, whatever processor the compiler takes by default:
x87's wider intermediates would round otherwise) */
#pragma clang attribute push (__attribute__((target("sse2"))), apply_to = function)

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "under_test.inc"

/* the upscale as it was first written: each output texel's sixteen taps
clamped to the sheet, texel by texel */
static unsigned char *texel_by_texel(const unsigned char *bgra, unsigned long width, unsigned long height)
{
	unsigned char *out = malloc(width * height * 16 * 4);
	unsigned long x, y, channel;

	for (y = 0; y < height * 4; y++)
	{
		float sy = (y + 0.5f) * 0.25f - 0.5f, wy[4];
		long iy = (long)sy;

		if (sy < iy) iy--;
		upscale_weights(sy - iy, wy);
		for (x = 0; x < width * 4; x++)
		{
			float sx = (x + 0.5f) * 0.25f - 0.5f, wx[4];
			long ix = (long)sx;

			if (sx < ix) ix--;
			upscale_weights(sx - ix, wx);
			for (channel = 0; channel < 4; channel++)
			{
				float value = 0.0f;
				long row, column;
				unsigned long source_channel = channel == 0 ? 2 : channel == 2 ? 0 : channel;

				for (row = 0; row < 4; row++)
				{
					long py = iy + row - 1;

					if (py < 0) py = 0;
					if (py >= (long)height) py = height - 1;
					for (column = 0; column < 4; column++)
					{
						long px = ix + column - 1;

						if (px < 0) px = 0;
						if (px >= (long)width) px = width - 1;
						value += bgra[(py * width + px) * 4 + source_channel] * wy[row] * wx[column];
					}
				}
				out[(y * width * 4 + x) * 4 + channel] =
					(unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value + 0.5f);
			}
		}
	}
	return out;
}

static unsigned long random_state;
static unsigned char random_byte(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (unsigned char)(random_state >> 16);
}

/* a sheet: random texels, or only 0 and 255 (the sums overshoot most), or
one colour */
static void compare(unsigned long width, unsigned long height, int kind)
{
	unsigned char *bgra = malloc(width * height * 4), *expected, *upscaled;
	unsigned long index;

	for (index = 0; index < width * height * 4; index++)
		bgra[index] = kind == 0 ? random_byte() : kind == 1 ? (random_byte() & 1) * 255 : (unsigned char)(index & 3) * 60;
	expected = texel_by_texel(bgra, width, height);
	upscaled = hud_hires_upscale_pixels(bgra, width, height);
	CHECK(upscaled != NULL, "no texels for a %lu by %lu sheet", width, height);
	for (index = 0; index < width * height * 64; index++)
		CHECK(upscaled[index] == expected[index], "%lu by %lu sheet (%d): output texel %lu, channel %lu is %d, not %d",
			width, height, kind, index / 4, index % 4, upscaled[index], expected[index]);
	free(bgra);
	free(expected);
	free(upscaled);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("sheets")
	{
		/* the sizes upscaled (the unit backgrounds' 128 by 32, the message
		icons' 256 by 256) and the smallest and narrowest */
		static const unsigned long sizes[][2] = {{128, 32}, {256, 256}, {64, 64}, {1, 1}, {1, 9}, {9, 1}, {3, 7},
			{256, 1}, {2, 256}, {17, 33}};
		int size, kind;

		for (size = 0; size < (int)(sizeof(sizes) / sizeof(sizes[0])); size++)
			for (kind = 0; kind < 3; kind++)
				compare(sizes[size][0], sizes[size][1], kind);
		return 0;
	}
	CASE("random")
	{
		int sheet;

		for (sheet = 0; sheet < 200; sheet++)
			compare(1 + random_byte() % 48, 1 + random_byte() % 48, sheet % 2);
		return 0;
	}
	CASE("refused")
	{
		unsigned char texel[4] = {0};

		CHECK(!hud_hires_upscale_pixels(NULL, 4, 4), "texels for no sheet");
		CHECK(!hud_hires_upscale_pixels(texel, 0, 1) && !hud_hires_upscale_pixels(texel, 1, 0), "texels for an empty sheet");
		CHECK(!hud_hires_upscale_pixels(texel, 257, 1) && !hud_hires_upscale_pixels(texel, 1, 257),
			"texels for a sheet larger than 256");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}

#pragma clang attribute pop
