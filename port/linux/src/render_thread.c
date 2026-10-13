/*
RENDER_THREAD.C

The renderer's own thread (display.render_thread). It takes the GL context
from the game's thread when the device is made and runs the commands that
thread queues, in the order they were queued, as the Xbox's GPU ran its
push buffer: the game goes on with the next frame while the last is drawn.

The queue is a ring with one writer (the game's thread) and one reader
(the render thread). A command is a header, the function that runs it and
how far the next one is, then its payload. The writer publishes how far it
has written (submitted), the reader how far it has run (completed); each
waits for the other by spinning a little, then on a condition variable.
Positions count bytes from the start and wrap: they are compared by their
differences, which the ring's size bounds.

A resource the game may write again (a texture, a palette) is busy from a
command that reads it until that command has run: the writer notes the
position after the command against the resource's data, and the game's
IsBusy, BlockUntilNotBusy and locks wait for the reader to pass it.
*/

#include "render_thread.h"

#include "platform.h"
#include "port_config.h"
#include "sdl_platform.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

/* a power of two; a frame's commands are far smaller */
#define RING_SIZE (32UL * 1024 * 1024)
#define RING_ALIGNMENT 16UL
/* checks of the other thread's position before sleeping */
#define SPIN_COUNT 4000
/* busy resources, by their data's address (a power of two) */
#define BUSY_SLOTS 4096

struct command_header
{
	/* NULL: none here, the next is at the start of the ring */
	render_thread_command command;
	/* bytes from this header to the next */
	unsigned long advance;
};

#define HEADER_SIZE ((sizeof(struct command_header) + RING_ALIGNMENT - 1) & ~(RING_ALIGNMENT - 1))

static unsigned char *ring;
static pthread_t render_thread;
static pthread_t writer_thread;
static int running;
/* the writer's position: recorded (its own), submitted (published) */
static unsigned long recorded;
static unsigned long submitted;
/* the reader's position */
static unsigned long completed;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_condition = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_condition = PTHREAD_COND_INITIALIZER;
/* set, under lock, by the reader about to sleep; and the threads asleep
until the reader reaches a position */
static int reader_sleeping;
static int waiters;
/* the position after the latest command recorded that reads each busy
resource (two resources in a slot: one waits for the other's commands) */
static unsigned long busy_until[BUSY_SLOTS];

static void spin_pause(void)
{
#if defined(__i386__) || defined(__x86_64__)
	__builtin_ia32_pause();
#endif
}

/* whether the reader has run everything before position (which is no
more than a ring behind or ahead of it) */
static int reached(unsigned long position, int order)
{
	return (long)(__atomic_load_n(&completed, order) - position) >= 0;
}

/* returns once the reader has run everything before position, which the
writer has submitted */
static void wait_for_reader(unsigned long position)
{
	int spin;

	for (spin = 0; spin < SPIN_COUNT; spin++)
	{
		if (reached(position, __ATOMIC_ACQUIRE))
			return;
		spin_pause();
	}
	pthread_mutex_lock(&lock);
	__atomic_add_fetch(&waiters, 1, __ATOMIC_SEQ_CST);
	while (!reached(position, __ATOMIC_SEQ_CST))
		pthread_cond_wait(&done_condition, &lock);
	__atomic_sub_fetch(&waiters, 1, __ATOMIC_RELAXED);
	pthread_mutex_unlock(&lock);
}

/* whether the caller is the ring's writer, with the thread running */
static int writing(void)
{
	return running && pthread_equal(pthread_self(), writer_thread);
}

static void *render_thread_main(void *unused)
{
	unsigned long position = 0;

	(void)unused;
	platform_video_make_current(TRUE);
	for (;;)
	{
		unsigned long end = __atomic_load_n(&submitted, __ATOMIC_ACQUIRE);
		int spin;

		for (spin = 0; end == position && spin < SPIN_COUNT; spin++)
		{
			spin_pause();
			end = __atomic_load_n(&submitted, __ATOMIC_ACQUIRE);
		}
		if (end == position)
		{
			pthread_mutex_lock(&lock);
			__atomic_store_n(&reader_sleeping, 1, __ATOMIC_SEQ_CST);
			while ((end = __atomic_load_n(&submitted, __ATOMIC_SEQ_CST)) == position)
				pthread_cond_wait(&work_condition, &lock);
			__atomic_store_n(&reader_sleeping, 0, __ATOMIC_RELAXED);
			pthread_mutex_unlock(&lock);
		}
		while (position != end)
		{
			struct command_header *header = (struct command_header *)(ring + (position & (RING_SIZE - 1)));

			if (header->command)
				header->command((unsigned char *)header + HEADER_SIZE);
			position += header->advance;
			/* after each command: a busy resource waits for its own only */
			__atomic_store_n(&completed, position, __ATOMIC_SEQ_CST);
			if (__atomic_load_n(&waiters, __ATOMIC_SEQ_CST))
			{
				pthread_mutex_lock(&lock);
				pthread_cond_broadcast(&done_condition);
				pthread_mutex_unlock(&lock);
			}
		}
	}
	return NULL;
}

int render_thread_start(void)
{
#ifdef HALO_GLES
	return FALSE;
#else
	if (running || !config_boolean("display.render_thread"))
		return FALSE;
	ring = (unsigned char *)malloc(RING_SIZE);
	if (!ring)
	{
		platform_log("render thread: cannot allocate its queue");
		return FALSE;
	}
	platform_video_make_current(FALSE);
	if (pthread_create(&render_thread, NULL, render_thread_main, NULL) != 0)
	{
		platform_log("render thread: cannot start it");
		platform_video_make_current(TRUE);
		free(ring);
		ring = NULL;
		return FALSE;
	}
	pthread_detach(render_thread);
	writer_thread = pthread_self();
	running = TRUE;
	platform_log("render thread: drawing on a thread of its own");
	return TRUE;
#endif
}

int render_thread_elsewhere(void)
{
	return running && !pthread_equal(pthread_self(), render_thread);
}

int render_thread_running(void)
{
	return running;
}

void *render_thread_record(render_thread_command command, unsigned long size)
{
	unsigned long advance = HEADER_SIZE + ((size + RING_ALIGNMENT - 1) & ~(RING_ALIGNMENT - 1));
	unsigned long offset = recorded & (RING_SIZE - 1);
	struct command_header *header;

#ifndef HALO_RELEASE
	/* the ring has one writer */
	if (!writing())
	{
		platform_log("render thread: a second thread queues commands");
		abort();
	}
#endif
	if (advance > RING_SIZE / 2)
	{
		platform_log("render thread: a command of %lu bytes is too large", size);
		abort();
	}
	/* the room needed must have been run; with the ring full of commands
	not yet handed over, they are handed over first */
	if (offset + advance > RING_SIZE)
	{
		/* to the start of the ring, past a header that says so */
		if (!reached(recorded - offset + advance, __ATOMIC_ACQUIRE))
		{
			render_thread_submit();
			wait_for_reader(recorded - offset + advance);
		}
		header = (struct command_header *)(ring + offset);
		header->command = NULL;
		header->advance = RING_SIZE - offset;
		recorded += header->advance;
		offset = 0;
	}
	else if (!reached(recorded + advance - RING_SIZE, __ATOMIC_ACQUIRE))
	{
		render_thread_submit();
		wait_for_reader(recorded + advance - RING_SIZE);
	}
	header = (struct command_header *)(ring + offset);
	header->command = command;
	header->advance = advance;
	recorded += advance;
	return (unsigned char *)header + HEADER_SIZE;
}

void render_thread_submit(void)
{
	if (submitted == recorded)
		return;
	__atomic_store_n(&submitted, recorded, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&reader_sleeping, __ATOMIC_SEQ_CST))
	{
		pthread_mutex_lock(&lock);
		pthread_cond_signal(&work_condition);
		pthread_mutex_unlock(&lock);
	}
}

void render_thread_wait(void)
{
	wait_for_reader(__atomic_load_n(&submitted, __ATOMIC_RELAXED));
}

void render_thread_call(render_thread_command command, const void *payload, unsigned long size)
{
	void *copy = render_thread_record(command, size);

	if (size)
		memcpy(copy, payload, size);
	render_thread_submit();
	render_thread_wait();
}

void render_thread_drain(void)
{
	if (!writing())
		return;
	render_thread_submit();
	render_thread_wait();
}

unsigned long render_thread_position(void)
{
	return recorded;
}

void render_thread_wait_for(unsigned long position)
{
	if (!writing() || reached(position, __ATOMIC_ACQUIRE))
		return;
	render_thread_submit();
	wait_for_reader(position);
}

int render_thread_busy(void)
{
	if (!writing())
		return FALSE;
	render_thread_submit();
	return !reached(submitted, __ATOMIC_ACQUIRE);
}

static unsigned long busy_slot(unsigned long data)
{
	return ((data >> 6) ^ (data >> 18)) & (BUSY_SLOTS - 1);
}

void render_thread_mark_busy(unsigned long data)
{
	if (data && writing())
		busy_until[busy_slot(data)] = recorded;
}

/* the position the resource is busy until, or 0 */
static unsigned long busy_position(unsigned long data)
{
	unsigned long until, done;

	if (!data || !writing())
		return 0;
	until = busy_until[busy_slot(data)];
	done = __atomic_load_n(&completed, __ATOMIC_ACQUIRE);
	/* busy only after what has run, up to what is recorded (a slot noted
	long ago, which the positions have wrapped back to, waits needlessly) */
	if (until == done || until - done > recorded - done)
		return 0;
	return until;
}

int render_thread_resource_busy(unsigned long data)
{
	if (!busy_position(data))
		return FALSE;
	/* the game spins on IsBusy: what it waits for is handed over */
	render_thread_submit();
	return TRUE;
}

void render_thread_wait_resource(unsigned long data)
{
	unsigned long until = busy_position(data);

	if (until)
		render_thread_wait_for(until);
}
