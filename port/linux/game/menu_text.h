/*
MENU_TEXT.H

The port's own lines (those of the menus' files with no loc.map string, and
those of its code) in the language of the PC menus (menu_text.c).
*/

#ifndef MENU_TEXT_H
#define MENU_TEXT_H

struct halo_menus;

/* the lines in the language of the maps' Custom Edition (language: the
menus' <language> its loc.map has, or NULL), with its loc.map open (locale)
or not; the same as before if both are */
void menu_text_build(struct halo_menus const *menus, char const *language, boolean locale);
/* a line of the menus' files (as written there) in the language: its UTF-16
characters, its terminator included (length of them); NULL if it has none */
unsigned short const *menu_text_line(char const *english, long *length);
/* the language's shorter words for a string of the PC version's (its English)
that its loc.map says wider than its widgets have room for, as
menu_text_line's; NULL if it has none */
unsigned short const *menu_text_shorter(char const *english, long *length);
/* a line of the port's code in the language, else the line itself */
wchar_t const *menu_text(wchar_t const *english);

/* a line of the port's code written where it is not shown (a table's), to
be shown through menu_text: tools/menu_languages.py finds it, as it finds
menu_text's */
#define MENU_LINE(english) english

#endif
