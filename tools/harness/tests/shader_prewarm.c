/*
SHADER_PREWARM.C (test)

The device's shaders kept by their text and a map's programs written down
and linked ahead as it next loads (xgpu_shaders.c, the whole file:
test_shader_prewarm.py takes it as under_test.inc), with the compiler, the
linker and the settings faked and the files real, in a folder of the test's:
a text is compiled once; a program is written down once for each map, not
again when linked ahead, and linked ahead, in its order, the next time the
map loads; of a file cut short what reads is kept, and added to; a file
that anything may have written links only what reads as it should; nothing
is written with the setting off; a map's name makes a file name of its own.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned int GLenum;
typedef unsigned int GLuint;

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31

#include "world.inc"

static int setting = 1;
static unsigned long compiles;
static GLuint next_shader = 1;

static int config_boolean(const char *name)
{
	CHECK(!strcmp(name, "display.shader_prewarm"), "setting %s", name);
	return setting;
}

static const char *platform_data_root(void)
{
	return DATA_ROOT;
}

static int posix_make_directory(const char *path)
{
	(void)path;
	return 0;
}

static void platform_log(const char *format, ...)
{
	(void)format;
}

/* a text with "broken" in it does not compile */
static GLuint xgpu_compile_shader(GLenum type, const char *source, const char *what)
{
	(void)type;
	(void)what;
	compiles++;
	return strstr(source, "broken") ? 0 : next_shader++;
}

/* a program of the device (d3d8_gl.c program_entry) */
struct program
{
	GLuint vertex, fragment;
	unsigned long serial;
};

static void xgpu_shaders_linked(GLuint vertex_shader, GLuint fragment_shader, unsigned long *serial);

/* the device's programs, which program_get tells xgpu_shaders_linked of
as it links one or finds it linked */
static struct program *table;
static unsigned long table_count;

/* the programs linked ahead, in order */
#define MAXIMUM_LINKED 64
static GLuint linked[MAXIMUM_LINKED][2];
static unsigned long linked_count;

static void xgpu_program_prewarm(GLuint vertex_shader, GLuint fragment_shader)
{
	unsigned long index;

	CHECK(linked_count < MAXIMUM_LINKED, "too many linked");
	linked[linked_count][0] = vertex_shader;
	linked[linked_count][1] = fragment_shader;
	linked_count++;
	for (index = 0; index < table_count; index++)
	{
		if (table[index].vertex == vertex_shader && table[index].fragment == fragment_shader)
			xgpu_shaders_linked(vertex_shader, fragment_shader, &table[index].serial);
	}
}

#include "under_test.inc"

static char *copy(const char *text)
{
	char *result = malloc(strlen(text) + 1);

	strcpy(result, text);
	return result;
}

static GLuint vertex(const char *text)
{
	return xgpu_shader_get(GL_VERTEX_SHADER, copy(text), "vertex");
}

static GLuint pixel(const char *text)
{
	return xgpu_shader_get(GL_FRAGMENT_SHADER, copy(text), "pixel");
}

static void map_path(const char *map, char *path, unsigned long size)
{
	snprintf(path, size, "%s/shaders/%s.txt", DATA_ROOT, map);
}

static long file_size(const char *map)
{
	char path[512];
	FILE *file;
	long size;

	map_path(map, path, sizeof(path));
	if ((file = fopen(path, "rb")) == NULL)
		return -1;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fclose(file);
	return size;
}

static void file_write(const char *map, const char *contents, unsigned long size)
{
	char path[512];
	FILE *file;

	map_path(map, path, sizeof(path));
	file = fopen(path, "wb");
	CHECK(file && fwrite(contents, 1, size, file) == size, "cannot write %s", path);
	fclose(file);
}

static void file_remove(const char *map)
{
	char path[512];

	map_path(map, path, sizeof(path));
	remove(path);
}

/* as the game starts again: nothing compiled, the file closed */
static void restart(void)
{
	if (shaders.file)
		fclose(shaders.file);
	memset(&shaders, 0, sizeof(shaders));
	table_count = 0;
	next_shader = 1;
	compiles = 0;
}

static void load(const char *map)
{
	linked_count = 0;
	compiles = 0;
	xgpu_shaders_prewarm(map);
}

/* the file written so far, readable */
static void flush(void)
{
	if (shaders.file)
		fflush(shaders.file);
}

/* a program drawn with in a map */
static void used(struct program *program)
{
	xgpu_shaders_linked(program->vertex, program->fragment, &program->serial);
}

static void three_programs(struct program *programs)
{
	memset(programs, 0, 3 * sizeof(*programs));
	programs[0].vertex = vertex("vertex one");
	programs[0].fragment = pixel("pixel one");
	programs[1].vertex = vertex("vertex two");
	programs[1].fragment = pixel("pixel one");
	programs[2].vertex = vertex("vertex one");
	programs[2].fragment = pixel("pixel two\nof two lines");
	table = programs;
	table_count = 3;
}

/* the three programs linked ahead, in order, as compiled again */
static void check_three_linked(void)
{
	GLuint vertex_one = vertex("vertex one"), vertex_two = vertex("vertex two");
	GLuint pixel_one = pixel("pixel one"), pixel_two = pixel("pixel two\nof two lines");

	CHECK(linked_count == 3, "%lu programs linked ahead, not 3", linked_count);
	CHECK(linked[0][0] == vertex_one && linked[0][1] == pixel_one, "first program %u %u", linked[0][0], linked[0][1]);
	CHECK(linked[1][0] == vertex_two && linked[1][1] == pixel_one, "second program %u %u", linked[1][0], linked[1][1]);
	CHECK(linked[2][0] == vertex_one && linked[2][1] == pixel_two, "third program %u %u", linked[2][0], linked[2][1]);
}

/* a malformed file: the programs it links, and the size it is kept to */
static void check_malformed(const char *what, const char *contents, unsigned long links, long kept)
{
	restart();
	file_write("malformed", contents, strlen(contents));
	load("malformed");
	flush();
	CHECK(linked_count == links, "%s: %lu programs linked, not %lu", what, linked_count, links);
	CHECK(file_size("malformed") == kept, "%s: the file is %ld bytes, not %ld", what, file_size("malformed"), kept);
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";
	struct program programs[3];
	unsigned long index;

	if (!strcmp(which, "once"))
	{
		GLuint first = vertex("same text");

		CHECK(first && vertex("same text") == first && compiles == 1, "a text compiled %lu times", compiles);
		CHECK(pixel("same text") != first && compiles == 2, "a pixel shader of a vertex shader's text");
		CHECK(vertex("broken text") == 0 && vertex("broken text") == 0 && compiles == 3,
			"a text that does not compile compiled %lu times", compiles);
		return 0;
	}
	if (!strcmp(which, "record"))
	{
		file_remove("record");
		load("record");
		CHECK(linked_count == 0, "%lu programs linked from no file", linked_count);
		three_programs(programs);
		for (index = 0; index < 3; index++)
			used(&programs[index]);
		restart();
		load("record");
		/* (two vertex shaders and two pixel shaders) */
		CHECK(compiles == 4, "%lu shaders compiled ahead, not 4", compiles);
		check_three_linked();
		return 0;
	}
	if (!strcmp(which, "once_a_map"))
	{
		long size;

		file_remove("first");
		file_remove("second");
		load("first");
		three_programs(programs);
		for (index = 0; index < 3; index++)
			used(&programs[index]);
		flush();
		size = file_size("first");
		used(&programs[0]);
		used(&programs[2]);
		flush();
		CHECK(file_size("first") == size, "a program written down again (%ld bytes, not %ld)", file_size("first"),
			size);
		/* another map: written down there too */
		load("second");
		used(&programs[1]);
		used(&programs[0]);
		used(&programs[2]);
		load("first");
		flush();
		check_three_linked();
		CHECK(file_size("first") == size, "the programs linked ahead written down again (%ld bytes, not %ld)",
			file_size("first"), size);
		for (index = 0; index < 3; index++)
			used(&programs[index]);
		flush();
		CHECK(file_size("first") == size, "a program linked ahead written down when used (%ld bytes, not %ld)",
			file_size("first"), size);
		restart();
		load("second");
		CHECK(linked_count == 3, "%lu programs linked ahead in the second map, not 3", linked_count);
		return 0;
	}
	if (!strcmp(which, "cut"))
	{
		static char contents[4096];
		long size;
		FILE *file;
		char path[512];
		struct program fourth = { 0 };

		file_remove("cut");
		load("cut");
		three_programs(programs);
		for (index = 0; index < 3; index++)
			used(&programs[index]);
		restart();
		/* the third program's line cut short */
		map_path("cut", path, sizeof(path));
		file = fopen(path, "rb");
		size = (long)fread(contents, 1, sizeof(contents), file);
		fclose(file);
		CHECK(size > 4 && contents[size - 1] == '\n', "the file ends with %d", contents[size - 1]);
		file_write("cut", contents, (unsigned long)size - 2);
		load("cut");
		flush();
		CHECK(linked_count == 2, "%lu programs linked from a file cut short, not 2", linked_count);
		/* kept to its last line that reads, and added to */
		three_programs(programs);
		fourth.vertex = vertex("vertex two");
		fourth.fragment = pixel("pixel two\nof two lines");
		used(&programs[2]);
		used(&fourth);
		restart();
		load("cut");
		CHECK(linked_count == 4, "%lu programs linked after one cut short was added to, not 4", linked_count);
		return 0;
	}
	if (!strcmp(which, "untrusted"))
	{
		static const char header[] = "OpenCE shaders 1\n";
		static const char good[] = "OpenCE shaders 1\nv 3\nabc\nf 3\ndef\np 0 1\n";
		static char contents[256];
		long kept = (long)strlen(good);

		check_malformed("no header", "Shaders 1\nv 3\nabc\nf 3\ndef\np 0 1\n", 0, (long)strlen(header));
		check_malformed("empty", "", 0, (long)strlen(header));
		check_malformed("good", good, 1, kept);
		snprintf(contents, sizeof(contents), "%sp 0 2\n", good);
		check_malformed("a shader past the last", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sp 1 0\n", good);
		check_malformed("the two swapped", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sp 0 0\n", good);
		check_malformed("a vertex shader as the pixel shader", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sv 9\nabc\n", good);
		check_malformed("a length past the end", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sv 4294967295\nabc\n", good);
		check_malformed("a length of ten digits", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sv 0\n\n", good);
		check_malformed("a length of 0", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sv 3\nabcp 0 1\n", good);
		check_malformed("no end of the text", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sp 0  1\n", good);
		check_malformed("two spaces", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sx 0 1\n", good);
		check_malformed("an unknown line", contents, 1, kept);
		snprintf(contents, sizeof(contents), "%sp -1 1\n", good);
		check_malformed("a sign", contents, 1, kept);
		memcpy(contents, good, (unsigned long)kept);
		memcpy(contents + kept, "v 3\na\0c\n", 8);
		restart();
		file_write("malformed", contents, (unsigned long)kept + 8);
		load("malformed");
		flush();
		CHECK(linked_count == 1 && file_size("malformed") == kept, "a text with a 0 in it: %lu linked, %ld bytes",
			linked_count, file_size("malformed"));
		snprintf(contents, sizeof(contents), "%sv 6\nbroken\np 2 1\n", good);
		check_malformed("a shader that does not compile", contents, 1, (long)strlen(contents));
		return 0;
	}
	if (!strcmp(which, "off"))
	{
		file_remove("off");
		setting = 0;
		load("off");
		three_programs(programs);
		used(&programs[0]);
		CHECK(file_size("off") < 0 && !shaders.file, "a file written with the setting off");
		return 0;
	}
	if (!strcmp(which, "name"))
	{
		file_remove("___x____b30");
		load("../x/..\\B30");
		flush();
		CHECK(file_size("___x____b30") > 0, "the map's file not at its name's characters");
		load("");
		CHECK(!shaders.file, "a file for a map of no name");
		load("+++");
		flush();
		CHECK(file_size("___") > 0, "a map's name of no letters: no file");
		/* (a cache file header's name, which need not end) */
		file_remove("abcdefghijklmnopqrstuvwxyz012345");
		load("abcdefghijklmnopqrstuvwxyz0123456789");
		flush();
		CHECK(file_size("abcdefghijklmnopqrstuvwxyz012345") > 0, "a map's name read past 32 characters");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", which);
	return 2;
}
