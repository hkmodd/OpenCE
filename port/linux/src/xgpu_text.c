/*
XGPU_TEXT.C

A growable string for the shader translators.
*/

#include "xgpu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* room for count more characters and the terminator from at; 0 out of memory */
static int text_reserve(struct xgpu_text *text, unsigned long at, unsigned long count)
{
	unsigned long capacity;
	char *buffer;

	if (text->buffer && at + count < text->capacity)
		return 1;
	capacity = (text->capacity + count + 1) * 2;
	buffer = realloc(text->buffer, capacity);
	if (!buffer)
		return 0;
	text->buffer = buffer;
	text->capacity = capacity;
	return 1;
}

static int text_put(struct xgpu_text *text, unsigned long *at, const char *characters, unsigned long count)
{
	if (!text_reserve(text, *at, count))
		return 0;
	memcpy(text->buffer + *at, characters, count);
	*at += count;
	return 1;
}

static int text_put_number(struct xgpu_text *text, unsigned long *at, unsigned long value, int negative)
{
	char digits[24];
	unsigned long count = 0;

	do
	{
		digits[sizeof(digits) - 1 - count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	if (negative)
		digits[sizeof(digits) - 1 - count++] = '-';
	return text_put(text, at, digits + sizeof(digits) - count, count);
}

/* the conversions the translators use (%s, %.*s, %d, %lu, %c, %%), written
without the C library's vsnprintf, which took most of a shader's
translation; 0 for any other format, which vsnprintf writes */
static int text_append_quickly(struct xgpu_text *text, const char *format, va_list arguments)
{
	unsigned long at = text->length;
	const char *literal = format;
	const char *next;

	for (next = format; *next; next++)
	{
		int written;

		if (*next != '%')
			continue;
		if (!text_put(text, &at, literal, (unsigned long)(next - literal)))
			goto out_of_memory;
		next++;
		if (*next == '%')
			written = text_put(text, &at, "%", 1);
		else if (*next == 's')
		{
			const char *string = va_arg(arguments, const char *);

			if (!string)
				return 0;
			written = text_put(text, &at, string, (unsigned long)strlen(string));
		}
		else if (next[0] == '.' && next[1] == '*' && next[2] == 's')
		{
			int precision = va_arg(arguments, int);
			const char *string = va_arg(arguments, const char *);
			unsigned long length = 0;

			if (precision < 0 || !string)
				return 0;
			while (length < (unsigned long)precision && string[length])
				length++;
			written = text_put(text, &at, string, length);
			next += 2;
		}
		else if (*next == 'd')
		{
			int value = va_arg(arguments, int);

			written = text_put_number(text, &at, value < 0 ? 0UL - (unsigned long)value : (unsigned long)value,
				value < 0);
		}
		else if (next[0] == 'l' && next[1] == 'u')
		{
			written = text_put_number(text, &at, va_arg(arguments, unsigned long), 0);
			next++;
		}
		else if (*next == 'c')
		{
			char character = (char)va_arg(arguments, int);

			written = text_put(text, &at, &character, 1);
		}
		else
			return 0;
		if (!written)
			goto out_of_memory;
		literal = next + 1;
	}
	if (!text_put(text, &at, literal, (unsigned long)(next - literal)))
		goto out_of_memory;
	text->buffer[at] = '\0';
	text->length = at;
	return 1;

out_of_memory:
	/* (the text stays as it is, short) */
	if (text->buffer)
		text->buffer[text->length] = '\0';
	return 1;
}

void xgpu_text_append(struct xgpu_text *text, const char *format, ...)
{
	va_list arguments;
	int needed;
	int appended;

	va_start(arguments, format);
	appended = text_append_quickly(text, format, arguments);
	va_end(arguments);
	if (appended)
		return;
	for (;;)
	{
		va_start(arguments, format);
		needed = vsnprintf(text->buffer ? text->buffer + text->length : NULL,
			text->buffer ? text->capacity - text->length : 0, format, arguments);
		va_end(arguments);
		if (text->buffer && text->length + (unsigned long)needed < text->capacity)
		{
			text->length += (unsigned long)needed;
			return;
		}
		{
			unsigned long capacity = (text->capacity + (unsigned long)needed + 1) * 2;
			char *buffer = realloc(text->buffer, capacity);

			/* (out of memory: the text stays as it is, short) */
			if (!buffer)
				return;
			text->buffer = buffer;
			text->capacity = capacity;
		}
	}
}
