#include "harness.h"

#include <limits.h>
#include <stdarg.h>

/* the C library's, counted, and a realloc that can be made to fail */
static int formatted;
static int counted_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	formatted++;
	return vsnprintf(buffer, count, format, arguments);
}
static int refuse;
static void *refusing_realloc(void *memory, size_t size)
{
	return refuse ? NULL : realloc(memory, size);
}
#define vsnprintf counted_vsnprintf
#define realloc refusing_realloc
#include "types.inc"
#include "under_test.inc"
#undef vsnprintf
#undef realloc

static struct xgpu_text text;
static char reference[1 << 17];
static unsigned long reference_length;

static void reference_append(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void reference_append(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	reference_length += (unsigned long)vsnprintf(reference + reference_length, sizeof(reference) - reference_length,
		format, arguments);
	va_end(arguments);
}

/* a text whose unused room is not zeroed, as realloc's need not be */
static void begin(unsigned long capacity)
{
	text.buffer = malloc(capacity);
	memset(text.buffer, 'X', capacity);
	text.buffer[0] = '\0';
	text.length = 0;
	text.capacity = capacity;
	reference_length = 0;
	reference[0] = '\0';
}

#define SAME(...) \
	do \
	{ \
		xgpu_text_append(&text, __VA_ARGS__); \
		reference_append(__VA_ARGS__); \
		CHECK(text.length == reference_length && text.length < text.capacity && \
			!memcmp(text.buffer, reference, reference_length + 1), \
			"after %s: \"%s\" (%lu), not \"%s\" (%lu)", #__VA_ARGS__, text.buffer, text.length, reference, \
			reference_length); \
	} while (0)

static const char not_terminated[3] = { 'x', 'y', 'z' };

/* every conversion the shader translators use */
static void quick_formats(void)
{
	SAME("%s", "");
	SAME("plain");
	SAME("");
	SAME("100%% and %%%s%%", "s");
	SAME("\t\t%s %sA = ", "vec3", "c");
	SAME("%d %d %d %d %d %d %d", 0, 7, -7, 10, -10, INT_MAX, INT_MIN);
	SAME("[%lu|%lu|%lu|%lu|%lu]", 0UL, 9UL, 10UL, 1234567890UL, ULONG_MAX);
	SAME("%c%c%c", 'a', '%', 'z');
	SAME("<%.*s|%.*s|%.*s|%.*s>", 0, "abc", 2, "abc", 5, "abc", 3, not_terminated);
	SAME("vc(clamp(a0 + %lu, 0, %d))", 95UL, 191);
	SAME("%s", "a long string to make the text grow past its first capacity, more than once over, "
		"a long string to make the text grow past its first capacity, more than once over");
	SAME("ends with a conversion %d", 3);
}

/* conversions the translators do not use, for vsnprintf */
static void other_formats(void)
{
	SAME("%x %X", 0xbeefu, 0xbeefu);
	SAME("[%5d|%-5d|%05d]", 42, 42, 42);
	SAME("[%-4s|%4s]", "ab", "ab");
	SAME("%u %ld %lx %03lu %hd", 3u, -4L, 255UL, 7UL, 5);
	SAME("%.2f %g", 1.5, 0.25);
	SAME("half %s, then %x and %s", "quick", 0x10u, "the rest");
	SAME("%p", (void *)0);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("quick")
	{
		begin(4);
		quick_formats();
		CHECK(formatted == 0, "the translators' formats went to vsnprintf %d times", formatted);
	}
	CASE("other")
	{
		begin(4);
		other_formats();
		CHECK(formatted >= 7, "the other formats went to vsnprintf %d times, not each", formatted);
	}
	CASE("empty")
	{
		/* a text never written to grows from nothing */
		memset(&text, 0, sizeof(text));
		reference_length = 0;
		SAME("%s", "");
		memset(&text, 0, sizeof(text));
		reference_length = 0;
		SAME("t%d = %s;\n", 3, "xT3");
	}
	CASE("growth")
	{
		int index;

		memset(&text, 0, sizeof(text));
		reference_length = 0;
		for (index = 0; index < 2000; index++)
		{
			SAME("\t/* texture stage %d, mode %lu */\n", index, (unsigned long)index * 7919UL);
			if (index % 7 == 0)
				SAME("%x", (unsigned)index);
			if (index % 5 == 0)
				SAME("%.*s%c", index % 11, "0123456789", 'q');
		}
	}
	CASE("out_of_memory")
	{
		begin(8);
		SAME("abc");
		refuse = 1;
		/* "de" fits; the string does not, and nothing of it stays */
		xgpu_text_append(&text, "de%s", "a string longer than the room left in the text");
		CHECK(text.length == 3 && !strcmp(text.buffer, "abc"), "out of memory left \"%s\" (%lu), not \"abc\"",
			text.buffer, text.length);
		refuse = 0;
		reference_length = 3;
		SAME("de%s", "f");
	}
	return 0;
}
