/*
MENU_TEXT.C

The port's own lines in the language of the PC menus (display.menu_language
"maps"): the menus' files' lines that have no string of the PC version's
(menu_tags.c finds them by their English), and the lines of the port's code
(menu_text). languages.xml's <line>s (tools/menu_languages.py) give them:
those written for the language of the maps' Custom Edition, then those its
loc.map says in a string of its own (a word of the PC version's menus that
means the same, as the controls' keys); a line neither gives is English, as
every line is in English or a language languages.xml has no <language> for.

A line of one row in English is one row in a string of the loc.map's too
(its line breaks made spaces).
*/

#include "cseries.h"

#include "halo_menus.h"
#include "custom_edition_cache.h"
#include "menu_text.h"

#include <stdlib.h>
#include <string.h>

/* (the port's code's lines are UTF-16, as the game's text is) */
typedef char verify_wide_characters[sizeof(wchar_t) == sizeof(unsigned short) ? 1 : -1];

struct menu_text_line
{
	char const *english;
	unsigned short *english_text;
	/* its characters in the language, its terminator included */
	unsigned short *text;
	long length;
};

static struct
{
	boolean built;
	boolean locale;
	char language[32];
	struct menu_text_line *lines;
	long count;
} menu_text_globals;

/* ---------- private code */

/* (the game's free takes no NULL: a line kept has both texts) */
static void menu_text_forget(void)
{
	long index;

	for (index = 0; index < menu_text_globals.count; index++)
	{
		free(menu_text_globals.lines[index].english_text);
		free(menu_text_globals.lines[index].text);
	}
	if (menu_text_globals.lines)
		free(menu_text_globals.lines);
	menu_text_globals.lines = NULL;
	menu_text_globals.count = 0;
	menu_text_globals.built = FALSE;
}

/* text as UTF-16 (halo_menus_utf16), allocated; NULL if out of memory */
static unsigned short *menu_text_utf16(char const *text, long *length)
{
	long capacity = (long)strlen(text) * 2 + 2;
	unsigned short *characters = malloc(capacity * sizeof(unsigned short));

	if (characters)
		*length = halo_menus_utf16(text, characters, capacity);
	return characters;
}

/* a loc.map string as the line's: one row if the English is, in capitals
if it asks for them (Latin-1's letters) */
static unsigned short *menu_text_from_map(struct halo_menu_line const *line, unsigned short const *string,
	long string_length, long *length)
{
	unsigned short *characters = malloc((string_length + 1) * sizeof(unsigned short));
	boolean one_row = strstr(line->text, "\\n") == NULL;
	boolean capitals = line->casing != NULL;
	long index, count = 0;

	if (!characters)
		return NULL;
	for (index = 0; index < string_length && string[index]; index++)
	{
		unsigned short character = string[index];

		if (one_row && (character == '\r' || character == '\n'))
			character = ' ';
		if (one_row && character == ' ' && (count == 0 || characters[count - 1] == ' '))
			continue;
		if (capitals && ((character >= 'a' && character <= 'z') ||
			(character >= 0xE0 && character <= 0xFE && character != 0xF7)))
		{
			character -= 0x20;
		}
		characters[count++] = character;
	}
	while (one_row && count && characters[count - 1] == ' ')
		count--;
	characters[count++] = 0;
	*length = count;
	return characters;
}

static struct menu_text_line const *menu_text_find(char const *english)
{
	long index;

	for (index = 0; index < menu_text_globals.count; index++)
	{
		if (!strcmp(menu_text_globals.lines[index].english, english))
			return &menu_text_globals.lines[index];
	}
	return NULL;
}

/* the line's text in the language: written for it, or (locale) its
loc.map's string; FALSE if it has neither, or there is no room */
static boolean menu_text_add(struct halo_menu_line const *line, boolean locale)
{
	struct menu_text_line *added = &menu_text_globals.lines[menu_text_globals.count];
	long english_length;

	if (line->language)
	{
		added->text = menu_text_utf16(line->translation, &added->length);
	}
	else
	{
		struct custom_edition_string_list *map = locale ? custom_edition_locale_strings(line->strings) : NULL;

		added->text = NULL;
		if (map && line->index < map->count && map->lengths[line->index] > 1)
		{
			added->text = menu_text_from_map(line, map->strings[line->index], map->lengths[line->index],
				&added->length);
		}
		if (map)
			free(map);
	}
	added->english = line->text;
	added->english_text = added->text ? menu_text_utf16(line->text, &english_length) : NULL;
	if (!added->english_text)
	{
		if (added->text)
			free(added->text);
		return FALSE;
	}
	menu_text_globals.count++;
	return TRUE;
}

/* ---------- public code */

void menu_text_build(struct halo_menus const *menus, char const *language, boolean locale)
{
	long pass, index;

	if (!language)
		language = "";
	if (menu_text_globals.built && menu_text_globals.locale == locale &&
		!strcmp(menu_text_globals.language, language))
	{
		return;
	}
	menu_text_forget();
	menu_text_globals.built = TRUE;
	menu_text_globals.locale = locale;
	snprintf(menu_text_globals.language, sizeof(menu_text_globals.language), "%s", language);
	/* (in English, or a language the menus do not know, the lines are as
	the port writes them) */
	if (!*language || !menus->line_count)
		return;
	menu_text_globals.lines = malloc(menus->line_count * sizeof(struct menu_text_line));
	if (!menu_text_globals.lines)
		return;
	/* (those written for the language first: the first of a line is kept) */
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < menus->line_count; index++)
		{
			struct halo_menu_line const *line = &menus->lines[index];

			if (pass == 0 ? !line->language || strcmp(line->language, language) : line->language != NULL)
				continue;
			if (!menu_text_find(line->text))
				menu_text_add(line, locale);
		}
	}
}

unsigned short const *menu_text_line(char const *english, long *length)
{
	struct menu_text_line const *line = menu_text_find(english);

	if (!line)
		return NULL;
	*length = line->length;
	return line->text;
}

wchar_t const *menu_text(wchar_t const *english)
{
	long index;

	for (index = 0; index < menu_text_globals.count; index++)
	{
		unsigned short const *text = menu_text_globals.lines[index].english_text;
		long character;

		for (character = 0; text[character] && text[character] == (unsigned short)english[character]; character++);
		if (text[character] == (unsigned short)english[character])
			return (wchar_t const *)menu_text_globals.lines[index].text;
	}
	return english;
}
