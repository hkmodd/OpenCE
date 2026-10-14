/*
XGPU_SHADERS.C

The shaders the device compiles (d3d8_gl.c), kept by their GLSL so that
each text is compiled once, and the programs linked from them written down
for each map in the data root's shaders folder: the next time the map loads,
they are compiled and linked as it loads (display.shader_prewarm), not at
their first draw, where a burst of new ones in the middle of a game holds
the frame up.

A map's file is read as anything may have written it, every length and
number checked:

	OpenCE shaders 1
	v <length>
	<the vertex shader's GLSL, length bytes>
	f <length>
	<the pixel shader's GLSL>
	p <vertex shader> <pixel shader>

the shaders numbered from 0 in the order they come, a program after its
two shaders. Of a file that does not read so to its end (one the game
stopped writing part way), what does is kept.
*/

#include "xgpu.h"
#include "posix.h"
#include "port_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADER_BUCKETS 1024

/* a map's file: past these it is started again */
#define MAXIMUM_FILE_SIZE (16 * 1024 * 1024)
#define MAXIMUM_FILE_SHADERS 8192
#define MAXIMUM_FILE_PROGRAMS 4096
#define MAXIMUM_SOURCE_LENGTH (256 * 1024)

static const char file_header[] = "OpenCE shaders 1\n";

struct shader_entry
{
	struct shader_entry *next_by_text;
	struct shader_entry *next_by_name;
	unsigned long hash;
	GLenum type;
	/* 0 if it did not compile */
	GLuint shader;
	char *source;
	/* its number in the map's file, while file_serial is the map's */
	unsigned long file_serial;
	unsigned long file_index;
};

static struct
{
	struct shader_entry *by_text[SHADER_BUCKETS];
	struct shader_entry *by_name[SHADER_BUCKETS];
	/* counts the maps loaded; the programs written down for the one loaded
	have its number (xgpu_shaders_linked) */
	unsigned long serial;
	/* the map's file, open to add to (NULL: none), its size and what it has */
	FILE *file;
	unsigned long file_size;
	unsigned long file_shaders;
	unsigned long file_programs;
} shaders;

static unsigned long text_hash(GLenum type, const char *source)
{
	unsigned long hash = 2166136261UL ^ (unsigned long)type;

	while (*source)
		hash = (hash ^ (unsigned char)*source++) * 16777619UL;
	return hash;
}

/* source is taken (kept, or freed); NULL if there is no memory, with the
source freed */
static struct shader_entry *shader_entry_get(GLenum type, char *source, const char *what)
{
	unsigned long hash = text_hash(type, source);
	struct shader_entry **bucket = &shaders.by_text[hash % SHADER_BUCKETS];
	struct shader_entry *entry;

	for (entry = *bucket; entry; entry = entry->next_by_text)
	{
		if (entry->hash == hash && entry->type == type && !strcmp(entry->source, source))
		{
			free(source);
			return entry;
		}
	}
	if ((entry = calloc(1, sizeof(*entry))) == NULL)
	{
		free(source);
		return NULL;
	}
	entry->hash = hash;
	entry->type = type;
	entry->source = source;
	entry->shader = xgpu_compile_shader(type, source, what);
	entry->next_by_text = *bucket;
	*bucket = entry;
	if (entry->shader)
	{
		bucket = &shaders.by_name[entry->shader % SHADER_BUCKETS];
		entry->next_by_name = *bucket;
		*bucket = entry;
	}
	return entry;
}

static struct shader_entry *shader_entry_named(GLuint shader)
{
	struct shader_entry *entry;

	for (entry = shaders.by_name[shader % SHADER_BUCKETS]; entry; entry = entry->next_by_name)
	{
		if (entry->shader == shader)
			return entry;
	}
	return NULL;
}

GLuint xgpu_shader_get(GLenum type, char *source, const char *what)
{
	struct shader_entry *entry = shader_entry_get(type, source, what);

	return entry ? entry->shader : 0;
}

/* a shader in the map's file, written to it if it is not yet */
static BOOL file_shader(struct shader_entry *entry)
{
	unsigned long length;
	int written;

	if (entry->file_serial == shaders.serial)
		return TRUE;
	length = (unsigned long)strlen(entry->source);
	if (shaders.file_shaders >= MAXIMUM_FILE_SHADERS || length > MAXIMUM_SOURCE_LENGTH ||
		shaders.file_size + length + 32 > MAXIMUM_FILE_SIZE)
		return FALSE;
	written = fprintf(shaders.file, "%c %lu\n", entry->type == GL_VERTEX_SHADER ? 'v' : 'f', length);
	if (written < 0 || fwrite(entry->source, 1, length, shaders.file) != length || fputc('\n', shaders.file) == EOF)
		return FALSE;
	shaders.file_size += (unsigned long)written + length + 1;
	entry->file_serial = shaders.serial;
	entry->file_index = shaders.file_shaders++;
	return TRUE;
}

void xgpu_shaders_linked(GLuint vertex_shader, GLuint fragment_shader, unsigned long *serial)
{
	struct shader_entry *vertex, *fragment;
	int written;

	if (*serial == shaders.serial)
		return;
	*serial = shaders.serial;
	if (!shaders.file || shaders.file_programs >= MAXIMUM_FILE_PROGRAMS)
		return;
	vertex = shader_entry_named(vertex_shader);
	fragment = shader_entry_named(fragment_shader);
	if (!vertex || !fragment || vertex->type != GL_VERTEX_SHADER || fragment->type != GL_FRAGMENT_SHADER ||
		!file_shader(vertex) || !file_shader(fragment))
		return;
	written = fprintf(shaders.file, "p %lu %lu\n", vertex->file_index, fragment->file_index);
	if (written > 0)
	{
		shaders.file_size += (unsigned long)written;
		shaders.file_programs++;
	}
}

/* a decimal number of at most nine digits at buffer[*at], and the character
after it, end; FALSE if there is none */
static BOOL read_number(const char *buffer, unsigned long size, unsigned long *at, char end, unsigned long *value)
{
	unsigned long digits = 0;

	*value = 0;
	while (*at < size && buffer[*at] >= '0' && buffer[*at] <= '9' && digits < 9)
	{
		*value = *value * 10 + (unsigned long)(buffer[*at] - '0');
		(*at)++;
		digits++;
	}
	if (!digits || *at >= size || buffer[*at] != end)
		return FALSE;
	(*at)++;
	return TRUE;
}

/* compiles and links what a map's file has; the size of its part that
reads as it should (0: none, not even the header) */
static unsigned long file_read(const char *buffer, unsigned long size)
{
	unsigned long header = sizeof(file_header) - 1;
	unsigned long at, good, count = 0, programs = 0;
	struct shader_entry **records;
	GLenum *types;

	if (size < header || memcmp(buffer, file_header, header))
		return 0;
	records = malloc(MAXIMUM_FILE_SHADERS * sizeof(*records));
	types = malloc(MAXIMUM_FILE_SHADERS * sizeof(*types));
	if (!records || !types)
	{
		free(records);
		free(types);
		return 0;
	}
	for (at = good = header; at + 2 <= size && buffer[at + 1] == ' '; good = at)
	{
		char kind = buffer[at];
		unsigned long length, vertex, fragment;
		char *source;

		at += 2;
		if (kind == 'v' || kind == 'f')
		{
			if (count >= MAXIMUM_FILE_SHADERS || !read_number(buffer, size, &at, '\n', &length) || !length ||
				length > MAXIMUM_SOURCE_LENGTH || length >= size - at || buffer[at + length] != '\n' ||
				memchr(buffer + at, '\0', length))
				break;
			if ((source = malloc(length + 1)) == NULL)
				break;
			memcpy(source, buffer + at, length);
			source[length] = '\0';
			types[count] = kind == 'v' ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
			records[count] = shader_entry_get(types[count], source, kind == 'v' ? "vertex" : "pixel");
			if (records[count])
			{
				records[count]->file_serial = shaders.serial;
				records[count]->file_index = count;
			}
			count++;
			at += length + 1;
		}
		else if (kind == 'p')
		{
			if (programs >= MAXIMUM_FILE_PROGRAMS || !read_number(buffer, size, &at, ' ', &vertex) ||
				!read_number(buffer, size, &at, '\n', &fragment) || vertex >= count || fragment >= count ||
				types[vertex] != GL_VERTEX_SHADER || types[fragment] != GL_FRAGMENT_SHADER)
				break;
			if (records[vertex] && records[fragment] && records[vertex]->shader && records[fragment]->shader)
				xgpu_program_prewarm(records[vertex]->shader, records[fragment]->shader);
			programs++;
		}
		else
			break;
	}
	free(records);
	free(types);
	shaders.file_shaders = count;
	shaders.file_programs = programs;
	return good;
}

/* the map's file's path, from its name of at most 32 characters (a cache file
header's, which need not end it); FALSE for a name of none */
static BOOL file_path(const char *map_name, char *path, unsigned long size)
{
	char name[32 + 1];
	unsigned long length = 0;

	for (; *map_name && length + 1 < sizeof(name); map_name++)
	{
		char character = *map_name;

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		name[length++] = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ? character : '_';
	}
	name[length] = '\0';
	if (!length || snprintf(path, size, "%s/shaders", platform_data_root()) >= (int)size)
		return FALSE;
	posix_make_directory(path);
	return snprintf(path, size, "%s/shaders/%s.txt", platform_data_root(), name) < (int)size;
}

void xgpu_shaders_prewarm(const char *map_name)
{
	char path[1024];
	char *buffer = NULL;
	unsigned long size = 0, good = 0;
	FILE *file;

	if (shaders.file)
		fclose(shaders.file);
	shaders.file = NULL;
	shaders.serial++;
	shaders.file_size = shaders.file_shaders = shaders.file_programs = 0;
	if (!config_boolean("display.shader_prewarm") || !map_name || !file_path(map_name, path, sizeof(path)))
		return;
	if ((file = fopen(path, "rb")) != NULL)
	{
		long length;

		if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 && length <= MAXIMUM_FILE_SIZE &&
			!fseek(file, 0, SEEK_SET) && (buffer = malloc((unsigned long)length)) != NULL)
			size = (unsigned long)fread(buffer, 1, (unsigned long)length, file);
		fclose(file);
	}
	if (buffer)
	{
		/* (with the file not yet open: the programs linked ahead are not
		written down again) */
		good = file_read(buffer, size);
		if (good)
			platform_log("shaders: %lu programs of %s linked ahead", shaders.file_programs, path);
	}
	if (good && good == size)
		shaders.file = fopen(path, "ab");
	else if ((shaders.file = fopen(path, "wb")) != NULL)
	{
		/* (what reads of it, or the header) */
		if (!good)
		{
			good = sizeof(file_header) - 1;
			shaders.file_shaders = shaders.file_programs = 0;
			if (fwrite(file_header, 1, good, shaders.file) != good)
				good = 0;
		}
		else if (fwrite(buffer, 1, good, shaders.file) != good)
			good = 0;
		if (!good)
		{
			fclose(shaders.file);
			shaders.file = NULL;
		}
	}
	shaders.file_size = good;
	free(buffer);
}
