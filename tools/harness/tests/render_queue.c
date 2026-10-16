/* the render thread's queue (port/linux/src/render_thread.c) with a real
reader thread: commands run once each, in order, with their payloads intact
across many turns of the ring; a call returns once its command has run; a
resource is busy until the command that reads it has run; a frame ends once
no more than the frames asked for are queued; a writer waiting is woken once
the position it waits for has run, not at each command before */

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

static unsigned long long platform_fiber_clock(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000000u + (unsigned long long)now.tv_nsec;
}

/* the times the reader wakes a waiting writer */
static unsigned long writer_wakes;

static int counted_broadcast(pthread_cond_t *condition)
{
	__atomic_add_fetch(&writer_wakes, 1, __ATOMIC_RELAXED);
	return pthread_cond_broadcast(condition);
}

#define pthread_cond_broadcast counted_broadcast
#include "under_test.inc"
#undef pthread_cond_broadcast

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

/* gates[index], opened after its delay (open_gate_after) */
static int gates_open[2];
static int gates_passed[2];

static void indexed_gate_command(void *payload)
{
	int index = *(int *)payload;

	while (!__atomic_load_n(&gates_open[index], __ATOMIC_ACQUIRE))
		;
	__atomic_store_n(&gates_passed[index], 1, __ATOMIC_RELEASE);
}

static void record_gate(int index)
{
	*(int *)render_thread_record(indexed_gate_command, sizeof(int)) = index;
}

struct gate_opening
{
	int index;
	long milliseconds;
};

static void *open_gate_after(void *payload)
{
	struct gate_opening *opening = (struct gate_opening *)payload;
	struct timespec delay;

	delay.tv_sec = opening->milliseconds / 1000;
	delay.tv_nsec = (opening->milliseconds % 1000) * 1000000L;
	nanosleep(&delay, NULL);
	__atomic_store_n(&gates_open[opening->index], 1, __ATOMIC_RELEASE);
	return NULL;
}

static int gate_passed_now(int index)
{
	return __atomic_load_n(&gates_passed[index], __ATOMIC_ACQUIRE);
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

/* opens the gate after much longer than the writer takes to go round the
ring (waits-for-room) */
static void *open_gate_much_later(void *unused)
{
	struct timespec delay = { 1, 0 };

	(void)unused;
	nanosleep(&delay, NULL);
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
	else if (!strcmp(name, "waits-for-room"))
	{
		/* the reader held at a gate half way round the ring, after commands
		it runs as they come: the writer must not go a turn of the ring past
		the command it is held at (in the middle of a turn, not only at its
		end, where the start of the ring is waited for), however slow or
		fast the reader was before */
		pthread_t opener;
		unsigned long number, total = 0;
		int gated = 0;

		for (number = 0; total < 2 * RING_SIZE; number++)
		{
			unsigned long index, *words;

			if (!gated && total >= RING_SIZE / 2)
			{
				render_thread_record(gate_command, 0);
				render_thread_submit();
				pthread_create(&opener, NULL, open_gate_much_later, NULL);
				gated = 1;
			}
			words = (unsigned long *)render_thread_record(numbered_command, (2 + 4096) * sizeof(unsigned long));
			words[0] = number;
			words[1] = 4096;
			for (index = 0; index < 4096; index++)
				words[2 + index] = word(number, index);
			total += (2 + 4096) * sizeof(unsigned long);
			render_thread_submit();
		}
		CHECK(__atomic_load_n(&gate_passed, __ATOMIC_ACQUIRE),
			"the writer went a turn of the ring past a command not yet run");
		render_thread_drain();
		CHECK(next_number == number, "%lu of %lu commands ran", next_number, number);
		CHECK(!wrong_number && !wrong_words, "commands ran out of order or overwritten");
		pthread_join(opener, NULL);
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
		unsigned long data[3] = { 0, 0x10000, 0 };

		render_thread_record(gate_command, 0);
		render_thread_mark_busy(data, 3);
		CHECK(render_thread_resource_busy(0x10000), "a resource a queued command reads is not busy");
		CHECK(!render_thread_resource_busy(0x20000), "a resource no command reads is busy");
		CHECK(!__atomic_load_n(&gate_passed, __ATOMIC_ACQUIRE), "the command ran with its gate shut");
		pthread_create(&opener, NULL, open_gate_later, NULL);
		render_thread_wait_resource(0x10000);
		CHECK(__atomic_load_n(&gate_passed, __ATOMIC_ACQUIRE), "the wait returned before the command ran");
		CHECK(!render_thread_resource_busy(0x10000), "the resource is busy once its command ran");
		pthread_join(opener, NULL);
	}
	else if (!strcmp(name, "one-frame-queued"))
	{
		/* the frame before must have run, not the frame ending */
		struct gate_opening first = { 0, 50 }, second = { 1, 1000 };
		pthread_t openers[2];

		record_gate(0);
		render_thread_frame_end(1);
		CHECK(!gate_passed_now(0), "the first frame ran with its gate shut");
		record_gate(1);
		pthread_create(&openers[0], NULL, open_gate_after, &first);
		pthread_create(&openers[1], NULL, open_gate_after, &second);
		render_thread_frame_end(1);
		CHECK(gate_passed_now(0), "the second frame ended before the first ran");
		CHECK(!gate_passed_now(1), "the second frame waited for itself to run");
		pthread_join(openers[0], NULL);
		pthread_join(openers[1], NULL);
		render_thread_drain();
	}
	else if (!strcmp(name, "frame-room"))
	{
		/* how long the end of a frame waited for the frame before to run */
		struct gate_opening first = { 0, 50 };
		pthread_t opener;
		unsigned long frame;
		unsigned long long room;

		record_gate(0);
		render_thread_frame_end(1);
		room = render_thread_frame_room(&frame);
		CHECK(room == 0 && frame == 1, "the first frame's end had %llu to spare, frame %lu", room, frame);
		record_gate(1);
		pthread_create(&opener, NULL, open_gate_after, &first);
		render_thread_frame_end(1);
		room = render_thread_frame_room(&frame);
		CHECK(room >= 40000000u && frame == 2, "a wait of 50 ms had %llu ns to spare, frame %lu", room, frame);
		__atomic_store_n(&gates_open[1], 1, __ATOMIC_RELEASE);
		pthread_join(opener, NULL);
		render_thread_drain();
	}
	else if (!strcmp(name, "frame-handed-over"))
	{
		/* the render thread draws a frame ended with room for more */
		struct timespec millisecond = { 0, 1000000L };
		int waited;

		gates_open[0] = 1;
		record_gate(0);
		render_thread_frame_end(2);
		for (waited = 0; waited < 2000 && !gate_passed_now(0); waited++)
			nanosleep(&millisecond, NULL);
		CHECK(gate_passed_now(0), "a frame ended was not handed to the render thread");
	}
	else if (!strcmp(name, "two-frames-queued"))
	{
		/* two frames end without waiting; the third waits for the first */
		struct gate_opening first = { 0, 50 }, second = { 1, 1000 };
		pthread_t openers[2];

		record_gate(0);
		render_thread_frame_end(2);
		record_gate(1);
		render_thread_frame_end(2);
		CHECK(!gate_passed_now(0), "the second frame waited for the first to run");
		pthread_create(&openers[0], NULL, open_gate_after, &first);
		pthread_create(&openers[1], NULL, open_gate_after, &second);
		render_thread_frame_end(2);
		CHECK(gate_passed_now(0), "the third frame ended before the first ran");
		CHECK(!gate_passed_now(1), "the third frame waited for the second to run");
		pthread_join(openers[0], NULL);
		pthread_join(openers[1], NULL);
		render_thread_drain();
	}
	else if (!strcmp(name, "wakes-writer-once-reached"))
	{
		/* the writer waits, asleep, behind a gate for the end of 1000
		commands: woken once they have run, not at each of them */
		struct gate_opening opening = { 0, 200 };
		pthread_t opener;
		unsigned long number;

		record_gate(0);
		for (number = 0; number < 1000; number++)
		{
			unsigned long *words = (unsigned long *)render_thread_record(numbered_command, 2 * sizeof(unsigned long));

			words[0] = number;
			words[1] = 0;
		}
		render_thread_submit();
		pthread_create(&opener, NULL, open_gate_after, &opening);
		render_thread_wait();
		CHECK(next_number == 1000, "the wait returned after %lu of 1000 commands", next_number);
		CHECK(writer_wakes <= 2, "the writer was woken %lu times for one wait", writer_wakes);
		pthread_join(opener, NULL);
	}
	else
	{
		CHECK(0, "no case %s", name);
	}
	return 0;
}
