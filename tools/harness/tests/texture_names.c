#include "harness.h"

typedef unsigned int GLuint;
typedef int GLsizei;
/* glGenTextures: the names in the order made, and the calls counted (each
waits for the driver) */
static GLuint names_made;
static unsigned long name_calls;
static void glGenTextures(GLsizei count, GLuint *names)
{
	GLsizei index;

	for (index = 0; index < count; index++)
		names[index] = ++names_made;
	name_calls++;
}
#include "config.inc"
#include "under_test.inc"

/* the names of a number of new textures: each one made, given once, in order,
and the calls to glGenTextures at most the blocks they fill */
static void give(unsigned long count)
{
	unsigned long index;
	GLuint previous = 0;

	for (index = 0; index < count; index++)
	{
		GLuint name = texture_name();

		CHECK(name != 0, "texture %lu has no name", index);
		CHECK(name == previous + 1, "texture %lu named %u, after %u: not the next one made", index, name, previous);
		previous = name;
	}
	CHECK(name_calls <= (count + TEXTURE_NAME_BLOCK - 1) / TEXTURE_NAME_BLOCK,
		"%lu calls to glGenTextures for %lu names", name_calls, count);
	CHECK(TEXTURE_NAME_BLOCK >= 64, "names made %d at a time", TEXTURE_NAME_BLOCK);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("one")
	{
		give(1);
	}
	CASE("burst")
	{
		/* the first frames after a map has loaded */
		give(70);
	}
	CASE("many")
	{
		/* across blocks */
		give(1000);
	}
	return 0;
}
