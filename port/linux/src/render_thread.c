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
static int running;
/* the writer's position: recorded (its own), submitted (published) */
static unsigned long recorded;
static unsigned long submitted;
/* the reader's position */
static unsigned long completed;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_condition = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_condition = PTHREAD_COND_INITIALIZER;
/* set, under lock, by a thread about to sleep */
static int reader_sleeping;
static int writer_sleeping;

static void spin_pause(void)
{
#if defined(__i386__) || defined(__x86_64__)
	__builtin_ia32_pause();
#endif
}

/* returns once completed has reached at least position - room (room 0:
everything up to position has run) */
static void wait_for_reader(unsigned long position, unsigned long room)
{
	int spin;

	for (spin = 0; spin < SPIN_COUNT; spin++)
	{
		if (position - __atomic_load_n(&completed, __ATOMIC_ACQUIRE) <= room)
			return;
		spin_pause();
	}
	pthread_mutex_lock(&lock);
	__atomic_store_n(&writer_sleeping, 1, __ATOMIC_SEQ_CST);
	while (position - __atomic_load_n(&completed, __ATOMIC_SEQ_CST) > room)
		pthread_cond_wait(&done_condition, &lock);
	__atomic_store_n(&writer_sleeping, 0, __ATOMIC_RELAXED);
	pthread_mutex_unlock(&lock);
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
		}
		__atomic_store_n(&completed, position, __ATOMIC_SEQ_CST);
		if (__atomic_load_n(&writer_sleeping, __ATOMIC_SEQ_CST))
		{
			pthread_mutex_lock(&lock);
			pthread_cond_signal(&done_condition);
			pthread_mutex_unlock(&lock);
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
	running = TRUE;
	platform_log("render thread: drawing on a thread of its own");
	return TRUE;
#endif
}

int render_thread_elsewhere(void)
{
	return running && !pthread_equal(pthread_self(), render_thread);
}

void *render_thread_record(render_thread_command command, unsigned long size)
{
	unsigned long advance = HEADER_SIZE + ((size + RING_ALIGNMENT - 1) & ~(RING_ALIGNMENT - 1));
	unsigned long offset = recorded & (RING_SIZE - 1);
	struct command_header *header;

#ifndef HALO_RELEASE
	static pthread_t writer;
	static int writer_known;

	/* the ring has one writer */
	if (!writer_known)
	{
		writer = pthread_self();
		writer_known = TRUE;
	}
	if (!pthread_equal(writer, pthread_self()))
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
	if (offset + advance > RING_SIZE)
	{
		/* to the start of the ring, past a header that says so */
		wait_for_reader(recorded + RING_SIZE - offset + advance, RING_SIZE);
		header = (struct command_header *)(ring + offset);
		header->command = NULL;
		header->advance = RING_SIZE - offset;
		recorded += header->advance;
		offset = 0;
	}
	else
	{
		wait_for_reader(recorded + advance, RING_SIZE);
	}
	header = (struct command_header *)(ring + offset);
	header->command = command;
	header->advance = advance;
	recorded += advance;
	return (unsigned char *)header + HEADER_SIZE;
}

void render_thread_submit(void)
{
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
	wait_for_reader(__atomic_load_n(&submitted, __ATOMIC_RELAXED), 0);
}

void render_thread_call(render_thread_command command, const void *payload, unsigned long size)
{
	void *copy = render_thread_record(command, size);

	if (size)
		memcpy(copy, payload, size);
	render_thread_submit();
	render_thread_wait();
}
