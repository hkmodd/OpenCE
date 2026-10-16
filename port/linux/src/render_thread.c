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

A command may hold back what the reader publishes (render_thread_hold):
the renderer defers draws to make them together later (display.instancing),
and until it has made them, the reader publishes no position past the first
of them, so that what they read stays busy. It lets them go before a
command that would not keep them, when the writer waits for a position the
reader has passed, and before sleeping.
*/

#include "render_thread.h"

#include "platform.h"
#include "port_config.h"
#include "sdl_platform.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

unsigned long long platform_fiber_clock(void); /* posix_fiber.c, win32_fiber.c */

/* a power of two; a frame's commands are far smaller */
#define RING_SIZE (32UL * 1024 * 1024)
#define RING_ALIGNMENT 16UL
/* checks of the other thread's position before sleeping */
#define SPIN_COUNT 4000
/* busy resources, by their data's address (a power of two) */
#define BUSY_SLOTS 4096
/* bytes (x86's and ARM's) */
#define CACHE_LINE 64

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
/* the writer's position: recorded (its own), and as last seen of the
reader's (which only grows) */
static unsigned long recorded;
static unsigned long completed_seen;
/* what each thread writes for the other to read, on cache lines of their
own: one thread's writes take from the other the line they are on, and the
reader writes its position after every command, the writer records
thousands a frame */
static struct
{
	/* how far it has published (submitted), how many wait for the reader,
	and the position they wait for */
	unsigned long submitted;
	int waiters;
	unsigned long wanted;
} __attribute__((aligned(CACHE_LINE))) writer_side;
static struct
{
	/* how far it has run (completed), whether it is about to sleep */
	unsigned long completed;
	int sleeping;
} __attribute__((aligned(CACHE_LINE))) reader_side;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_condition = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_condition = PTHREAD_COND_INITIALIZER;
/* the position after the latest command recorded that reads each busy
resource (two resources in a slot: one waits for the other's commands) */
static unsigned long busy_until[BUSY_SLOTS];
/* the position after each of the latest frames, by the count of frames
ended */
static unsigned long frame_ends[RENDER_THREAD_MAXIMUM_FRAMES];
static unsigned long frames_ended;
/* render_thread_set_idle_work */
static int (*idle_work)(void);
/* (the reader's) render_thread_hold: whether it holds, from the position
of the command that began holding, the command running, the commands that
keep it holding and what lets go */
static int holding;
static unsigned long held_from;
static unsigned long running_from;
static int (*hold_keeps)(render_thread_command);
static void (*hold_release)(void);

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
	return (long)(__atomic_load_n(&reader_side.completed, order) - position) >= 0;
}

/* (the writer) whether the reader has run everything before position, as
last seen, its position read again only when that is not enough: the writer
asks before each command whether the ring has room */
static int reached_seen(unsigned long position)
{
	if ((long)(completed_seen - position) >= 0)
		return TRUE;
	completed_seen = __atomic_load_n(&reader_side.completed, __ATOMIC_ACQUIRE);
	return (long)(completed_seen - position) >= 0;
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
	__atomic_store_n(&writer_side.wanted, position, __ATOMIC_RELAXED);
	__atomic_add_fetch(&writer_side.waiters, 1, __ATOMIC_SEQ_CST);
	while (!reached(position, __ATOMIC_SEQ_CST))
		pthread_cond_wait(&done_condition, &lock);
	__atomic_sub_fetch(&writer_side.waiters, 1, __ATOMIC_RELAXED);
	pthread_mutex_unlock(&lock);
}

/* whether the caller is the ring's writer, with the thread running */
static int writing(void)
{
	return running && pthread_equal(pthread_self(), writer_thread);
}

void render_thread_hold(int (*keeps)(render_thread_command), void (*release)(void))
{
	if (!holding)
	{
		holding = TRUE;
		held_from = running_from;
	}
	hold_keeps = keeps;
	hold_release = release;
}

void render_thread_release(void)
{
	holding = FALSE;
}

/* (the reader, at position) lets what is held go, and publishes position */
static void hold_end(unsigned long position)
{
	hold_release();
	holding = FALSE;
	__atomic_store_n(&reader_side.completed, position, __ATOMIC_SEQ_CST);
}

/* (the reader, at position) whether the writer waits for a position that
only the hold keeps it from */
static int hold_blocks_writer(unsigned long position)
{
	unsigned long wanted;

	if (!__atomic_load_n(&writer_side.waiters, __ATOMIC_SEQ_CST))
		return FALSE;
	wanted = __atomic_load_n(&writer_side.wanted, __ATOMIC_RELAXED);
	return (long)(wanted - held_from) > 0 && (long)(position - wanted) >= 0;
}

/* (the reader, having published position) wakes the writer if it waits
for no more than position, not at each command of a frame whose end it
waits for (one thread waits: a waiter seen is seen with its position, and
one not seen yet finds position published) */
static void wake_writer(unsigned long position)
{
	if (__atomic_load_n(&writer_side.waiters, __ATOMIC_SEQ_CST)
		&& (long)(position - __atomic_load_n(&writer_side.wanted, __ATOMIC_RELAXED)) >= 0)
	{
		pthread_mutex_lock(&lock);
		pthread_cond_broadcast(&done_condition);
		pthread_mutex_unlock(&lock);
	}
}

static void *render_thread_main(void *unused)
{
	unsigned long position = 0;

	(void)unused;
	platform_video_make_current(TRUE);
	for (;;)
	{
		unsigned long end = __atomic_load_n(&writer_side.submitted, __ATOMIC_ACQUIRE);
		int spin;

		for (spin = 0; end == position && spin < SPIN_COUNT; spin++)
		{
			if (holding && hold_blocks_writer(position))
				break;
			spin_pause();
			end = __atomic_load_n(&writer_side.submitted, __ATOMIC_ACQUIRE);
		}
		/* nothing more to keep with what is held */
		if (holding && end == position)
		{
			hold_end(position);
			wake_writer(position);
		}
		if (end == position)
		{
			pthread_mutex_lock(&lock);
			__atomic_store_n(&reader_side.sleeping, 1, __ATOMIC_SEQ_CST);
			while ((end = __atomic_load_n(&writer_side.submitted, __ATOMIC_SEQ_CST)) == position)
				pthread_cond_wait(&work_condition, &lock);
			__atomic_store_n(&reader_side.sleeping, 0, __ATOMIC_RELAXED);
			pthread_mutex_unlock(&lock);
		}
		while (position != end)
		{
			struct command_header *header = (struct command_header *)(ring + (position & (RING_SIZE - 1)));
			unsigned long published;

			if (holding && header->command && !hold_keeps(header->command))
				hold_end(position);
			running_from = position;
			if (header->command)
				header->command((unsigned char *)header + HEADER_SIZE);
			position += header->advance;
			if (holding && hold_blocks_writer(position))
				hold_end(position);
			/* after each command: a busy resource waits for its own only */
			published = holding ? held_from : position;
			__atomic_store_n(&reader_side.completed, published, __ATOMIC_SEQ_CST);
			wake_writer(published);
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
		if (!reached_seen(recorded - offset + advance))
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
	else if (!reached_seen(recorded + advance - RING_SIZE))
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
	if (writer_side.submitted == recorded)
		return;
	__atomic_store_n(&writer_side.submitted, recorded, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&reader_side.sleeping, __ATOMIC_SEQ_CST))
	{
		pthread_mutex_lock(&lock);
		pthread_cond_signal(&work_condition);
		pthread_mutex_unlock(&lock);
	}
}

void render_thread_wait(void)
{
	wait_for_reader(__atomic_load_n(&writer_side.submitted, __ATOMIC_RELAXED));
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

void render_thread_set_idle_work(int (*work)(void))
{
	idle_work = work;
}

/* render_thread_frame_room */
static unsigned long long frame_room;

unsigned long long render_thread_frame_room(unsigned long *frame)
{
	*frame = frames_ended;
	return frame_room;
}

void render_thread_frame_end(unsigned long frames)
{
	unsigned long long began = platform_fiber_clock();

	frame_room = 0;
	if (frames < 1)
		frames = 1;
	if (frames > RENDER_THREAD_MAXIMUM_FRAMES)
		frames = RENDER_THREAD_MAXIMUM_FRAMES;
	render_thread_submit();
	/* the frame that many before this one must have run */
	if (frames_ended >= frames)
	{
		unsigned long position = frame_ends[(frames_ended - frames) % RENDER_THREAD_MAXIMUM_FRAMES];

		/* the time until then the game's own */
		if (idle_work && writing())
		{
			while (!reached(position, __ATOMIC_ACQUIRE) && idle_work())
			{
			}
			render_thread_submit();
		}
		render_thread_wait_for(position);
		frame_room = platform_fiber_clock() - began;
	}
	frame_ends[frames_ended % RENDER_THREAD_MAXIMUM_FRAMES] = recorded;
	frames_ended++;
}

int render_thread_busy(void)
{
	if (!writing())
		return FALSE;
	render_thread_submit();
	return !reached(writer_side.submitted, __ATOMIC_ACQUIRE);
}

static unsigned long busy_slot(unsigned long data)
{
	return ((data >> 6) ^ (data >> 18)) & (BUSY_SLOTS - 1);
}

void render_thread_mark_busy(unsigned long const *data, int count)
{
	int index;

	/* (which thread asks is a call to the system: asked once for all of a
	command's resources) */
	if (!writing())
		return;
	for (index = 0; index < count; index++)
	{
		if (data[index])
			busy_until[busy_slot(data[index])] = recorded;
	}
}

/* the position the resource is busy until, or 0 */
static unsigned long busy_position(unsigned long data)
{
	unsigned long until, done;

	if (!data || !writing())
		return 0;
	until = busy_until[busy_slot(data)];
	done = __atomic_load_n(&reader_side.completed, __ATOMIC_ACQUIRE);
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
