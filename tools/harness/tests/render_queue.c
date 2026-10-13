/* the render thread's queue (port/linux/src/render_thread.c) with a real
reader thread: commands run once each, in order, with their payloads intact
across many turns of the ring; a call returns once its command has run; a
resource is busy until the command that reads it has run */

#include "harness.h"

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>

static void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

static int config_boolean(const char *name)
{
	(void)name;
	return TRUE;
}

static void platform_video_make_current(int current)
{
	(void)current;
}

#include "under_test.inc"

/* commands, as the render thread runs them */
static unsigned long next_number;
static unsigned long wrong_number;
static unsigned long wrong_words;
static int gate_open;
static int gate_passed;
static int call_ran;

static unsigned long word(unsigned long number, unsigned long index)
{
	return number * 2654435761UL + index;
}

/* {number, count, count words}: the numbers come in order, the words as written */
static void numbered_command(void *payload)
{
	unsigned long *words = (unsigned long *)payload;
	unsigned long index;
	volatile unsigned long slow;

	if (words[0] != next_number)
		wrong_number++;
	for (index = 0; index < words[1]; index++)
	{
		if (words[2 + index] != word(words[0], index))
			wrong_words++;
	}
	/* a reader slower than the writer, which keeps the ring full */
	for (slow = 0; slow < 200; slow++)
		;
	next_number = words[0] + 1;
}

static void gate_command(void *payload)
{
	(void)payload;
	while (!__atomic_load_n(&gate_open, __ATOMIC_ACQUIRE))
		;
	__atomic_store_n(&gate_passed, 1, __ATOMIC_RELEASE);
}

static void call_command(void *payload)
{
	volatile unsigned long slow;

	for (slow = 0; slow < 20000000; slow++)
		;
	call_ran = *(int *)payload;
}

static void *open_gate_later(void *unused)
{
	volatile unsigned long slow;

	(void)unused;
	for (slow = 0; slow < 20000000; slow++)
		;
	__atomic_store_n(&gate_open, 1, __ATOMIC_RELEASE);
	return NULL;
}

/* a queue that breaks may crash or stop: either is a failed check */
static void *watchdog(void *unused)
{
	struct timespec second = { 1, 0 };
	int seconds;

	(void)unused;
	for (seconds = 0; seconds < 30; seconds++)
		nanosleep(&second, NULL);
	fprintf(stderr, "check failed: the queue stopped\n");
	fflush(stderr);
	_Exit(1); /* as CHECK */
}

static void crashed(int signal_number)
{
	(void)signal_number;
	fprintf(stderr, "check failed: the queue crashed\n");
	fflush(stderr);
	_Exit(1); /* as CHECK */
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";
	pthread_t watcher;

	signal(SIGSEGV, crashed);
	signal(SIGILL, crashed);
#ifdef SIGBUS
	signal(SIGBUS, crashed);
#endif
	pthread_create(&watcher, NULL, watchdog, NULL);
	CHECK(render_thread_start(), "the render thread did not start");

	if (!strcmp(name, "runs-in-order-across-turns"))
	{
		/* about eight turns of the ring, in commands of up to 32 KB */
		unsigned long number, total = 0, random = 1;

		for (number = 0; total < 8 * RING_SIZE; number++)
		{
			unsigned long count, index, *words;

			random = random * 1103515245UL + 12345;
			count = (random >> 8) % 8192;
			words = (unsigned long *)render_thread_record(numbered_command, (2 + count) * sizeof(unsigned long));
			words[0] = number;
			words[1] = count;
			for (index = 0; index < count; index++)
				words[2 + index] = word(number, index);
			total += (2 + count) * sizeof(unsigned long);
			if (number % 8 == 0)
				render_thread_submit();
		}
		render_thread_drain();
		CHECK(next_number == number, "%lu of %lu commands ran", next_number, number);
		CHECK(!wrong_number, "%lu commands ran out of order", wrong_number);
		CHECK(!wrong_words, "%lu words were overwritten before they ran", wrong_words);
		CHECK(!render_thread_busy(), "the queue is not empty once drained");
	}
	else if (!strcmp(name, "call-returns-once-run"))
	{
		int value = 7;

		render_thread_call(call_command, &value, sizeof(value));
		CHECK(call_ran == 7, "the call returned before its command ran");
	}
	else if (!strcmp(name, "resource-busy-until-read"))
	{
		pthread_t opener;

		render_thread_record(gate_command, 0);
		render_thread_mark_busy(0x10000);
		CHECK(render_thread_resource_busy(0x10000), "a resource a queued command reads is not busy");
		CHECK(!render_thread_resource_busy(0x20000), "a resource no command reads is busy");
		CHECK(!__atomic_load_n(&gate_passed, __ATOMIC_ACQUIRE), "the command ran with its gate shut");
		pthread_create(&opener, NULL, open_gate_later, NULL);
		render_thread_wait_resource(0x10000);
		CHECK(__atomic_load_n(&gate_passed, __ATOMIC_ACQUIRE), "the wait returned before the command ran");
		CHECK(!render_thread_resource_busy(0x10000), "the resource is busy once its command ran");
		pthread_join(opener, NULL);
	}
	else
	{
		CHECK(0, "no case %s", name);
	}
	return 0;
}
