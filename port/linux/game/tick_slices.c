/*
TICK_SLICES.C

A tick in pieces (display.tick_slices). A tick of a crowded level can take
longer than a frame of a fast display: run whole, it holds up the one
frame it falls on. With the render thread, the game's thread waits at the
end of each frame for the frame queued before it to be drawn; that wait is
spare time. The last tick a frame runs, in any game but a film, is run
on a second stack (posix_fiber.c, win32_fiber.c), from which it hands back
at its yield points once its share of the frame is used, and goes on in the
spare time and at the start of the frames after it.

A tick begun ahead of its time holds nothing up yet: at the start of a
frame it runs only for as long as the frame before it had to spare (its
wait, and the tick's own share of it), less a margin, so that a frame with
no time to spare is not made late by a tick that need not run in it yet.
Whatever the start of a frame does not run, the spare time at its end does:
the tick takes no longer.

A tick that would end after it is due is begun ahead of its time, by as
long as the last ticks took to run in pieces: the frames before it is due
go on drawing the two ticks before it, and the clock takes it, ended, when
it is due. One that is due and has not ended yet holds the clock: the
frames draw the tick before it, whole, until it has ended. It runs at the
start of each frame for its share, or longer: for what is left of as long
as the latest ticks ran, spread over the frames it may run on before one
runs the rest whole, so that a frame with no time to spare is not left
most of a long tick to run.

Only one stack runs at a time, on the game's thread: a frame drawn while a
tick is under way reads what the tick has left half done (its objects'
snapshots are whole, port/linux/game/render_interpolation.c), and
everything else that changes the game (the deferred actions, saving, the
network) waits for the frames after it has ended (main.c).
*/

#include "cseries.h"
#include "game.h"
#include "tick_slices.h"
#include "physics/collision_usage.h"

int config_boolean(const char *name); /* port/linux/src/port_config.c */
unsigned long config_changes(void);
int render_thread_running(void); /* port/linux/src/render_thread.c */
void render_thread_set_idle_work(int (*work)(void));
unsigned long long render_thread_frame_room(unsigned long *frame);
int platform_fiber_create(void (*entry)(void)); /* port/linux/src/posix_fiber.c */
void platform_fiber_enter(void);
void platform_fiber_leave(void);
unsigned long long platform_fiber_clock(void);
unsigned long long platform_fiber_clock_rate(void);

/* ---------- constants */

enum
{
	/* how long a tick runs on the frame it is begun on, and at the start
	of each frame after it (microseconds) */
	TICK_SLICE_FRAME_SHARE = 300,
	/* how long it runs at a time in the spare time, between checks of
	whether the frame waited for has been drawn */
	TICK_SLICE_SPARE_SHARE = 100,
	/* what a tick begun ahead of its time leaves of the time the frame
	before had to spare, for a frame that takes longer than the one before */
	TICK_SLICE_MARGIN = 200,
	/* the frames a tick that is due may run on before one runs it whole
	(one begun ahead of its time holds nothing up until it falls due) */
	TICK_SLICE_MAXIMUM_FRAMES = 8
};

/* ---------- globals */

/* a tick begun and not yet given to the clock: whether it has run to its
end, and whether the clock has counted it (it was due when begun, or fell
due under way) */
static boolean tick_pending;
static boolean tick_pending_ended;
static boolean tick_pending_counted;
static unsigned long long tick_pending_began;
/* the frames it has run on since it fell due */
static short frames_due;
/* how long the latest ticks took from their beginning to their end,
smoothed (microseconds) */
static unsigned long tick_duration;
/* how long the tick pending has run so far, and the latest ticks ran in
all, smoothed (in the platform's clock's counts) */
static unsigned long long tick_pending_work;
static unsigned long long tick_work;
/* how long the latest share of a frame ran (in the platform's clock's
counts), and the count of the frames ended before it (render_thread.c) */
static unsigned long long share_used;
static unsigned long share_frame;
/* the fiber's: its tick has ended */
static boolean fiber_tick_ended;
static boolean in_tick;
/* in the platform's clock's counts (read at every yield point: no
conversion there), and the counts in a second */
static unsigned long long slice_deadline;
static unsigned long long clock_rate;
/* the fiber, made the first time; FALSE if it cannot be */
static boolean fiber_made;
static boolean fiber_failed;

/* ---------- private code */

static void tick_fiber_main(void)
{
	for (;;)
	{
		game_tick();
		fiber_tick_ended = TRUE;
		platform_fiber_leave();
	}
}

static boolean tick_slices_enabled(void)
{
#ifdef HALO_PROFILE
	/* (the profiling build's sections nest on one stack) */
	return FALSE;
#else
	static unsigned long changes_seen = (unsigned long)-1;
	static boolean enabled;

	if (changes_seen != config_changes())
	{
		changes_seen = config_changes();
		enabled = config_boolean("display.tick_slices") != 0;
	}
	if (!enabled || fiber_failed || !render_thread_running())
		return FALSE;
	if (!fiber_made)
	{
		clock_rate = platform_fiber_clock_rate();
		if (!clock_rate || !platform_fiber_create(tick_fiber_main))
		{
			fiber_failed = TRUE;
			return FALSE;
		}
		fiber_made = TRUE;
	}
	return TRUE;
#endif
}

/* runs the tick pending for share microseconds, or to its end */
static void tick_slices_run(unsigned long share)
{
	unsigned long long now = platform_fiber_clock();

	slice_deadline = share ? now + share * clock_rate / 1000000u : 0;
	in_tick = TRUE;
	platform_fiber_enter();
	in_tick = FALSE;
	tick_pending_work += platform_fiber_clock() - now;
	if (fiber_tick_ended)
	{
		unsigned long duration;

		fiber_tick_ended = FALSE;
		tick_pending_ended = TRUE;
		now = platform_fiber_clock();
		duration = (unsigned long)((now - tick_pending_began) * 1000000u / clock_rate);
		tick_duration = duration > tick_duration ?
			tick_duration + (duration - tick_duration) / 8 :
			tick_duration - (tick_duration - duration) / 8;
		tick_work = tick_pending_work > tick_work ?
			tick_work + (tick_pending_work - tick_work) / 8 :
			tick_work - (tick_work - tick_pending_work) / 8;
	}
}

/* render_thread_frame_end's spare time: nonzero while there is more of the
tick to run */
static int tick_slices_spare_time(void)
{
	if (!tick_pending || tick_pending_ended)
		return FALSE;
	tick_slices_run(TICK_SLICE_SPARE_SHARE);
	return !tick_pending_ended;
}

/* this frame's share of the tick pending (microseconds; 0 for none) */
static unsigned long tick_slices_frame_share(void)
{
	unsigned long frame;
	unsigned long long room = render_thread_frame_room(&frame);
	unsigned long long spare;

	if (tick_pending_counted)
	{
		/* (the work it has left, as long as the latest ticks took, spread
		over the frames it may run on before one runs it whole) */
		unsigned long long left = tick_work > tick_pending_work ? tick_work - tick_pending_work : 0;
		int frames = TICK_SLICE_MAXIMUM_FRAMES - frames_due;

		left = left * 1000000u / clock_rate / (frames > 1 ? frames : 1);
		return left > TICK_SLICE_FRAME_SHARE ? (unsigned long)left : TICK_SLICE_FRAME_SHARE;
	}
	/* (the room the frame before had with no share run in it) */
	if (share_frame + 1 == frame)
		room += share_used;
	spare = room * 1000000u / clock_rate;
	if (spare <= TICK_SLICE_MARGIN)
		return 0;
	spare -= TICK_SLICE_MARGIN;
	return spare < TICK_SLICE_FRAME_SHARE ? (unsigned long)spare : TICK_SLICE_FRAME_SHARE;
}

/* runs the tick pending for its share of this frame */
static void tick_slices_run_share(void)
{
	unsigned long share = tick_slices_frame_share();
	unsigned long long began;

	if (!share)
		return;
	began = platform_fiber_clock();
	tick_slices_run(share);
	share_used = platform_fiber_clock() - began;
	render_thread_frame_room(&share_frame);
}

static void tick_slices_begin(boolean counted)
{
	render_thread_set_idle_work(tick_slices_spare_time);
	tick_pending = TRUE;
	tick_pending_ended = FALSE;
	tick_pending_counted = counted;
	tick_pending_began = platform_fiber_clock();
	tick_pending_work = 0;
	frames_due = 0;
	tick_slices_run_share();
}

/* ---------- public code */

boolean tick_slices_tick(boolean may_slice)
{
	if (!may_slice || tick_pending || !tick_slices_enabled())
	{
		game_tick();
		return FALSE;
	}
	tick_slices_begin(TRUE);
	if (tick_pending_ended)
	{
		tick_slices_release();
		return FALSE;
	}
	return TRUE;
}

void tick_slices_tick_ahead(void)
{
	if (tick_pending || !tick_slices_enabled())
		return;
	tick_slices_begin(FALSE);
}

double tick_slices_lead(void)
{
	/* (none while the ticks end in the share of the frame they begin on) */
	if (tick_duration <= TICK_SLICE_FRAME_SHARE || !tick_slices_enabled())
		return 0.0;
	return tick_duration * 1.25 / 1000000.0;
}

boolean tick_slices_in_flight(void)
{
	return tick_pending;
}

boolean tick_slices_counted(void)
{
	return tick_pending && tick_pending_counted;
}

boolean tick_slices_ended(void)
{
	return tick_pending && tick_pending_ended;
}

boolean tick_slices_continue(boolean whole)
{
	if (!tick_pending)
		return TRUE;
	if (!tick_pending_ended)
	{
		if (tick_pending_counted)
			frames_due++;
		if (frames_due >= TICK_SLICE_MAXIMUM_FRAMES)
			whole = TRUE;
		if (whole)
			tick_slices_run(0);
		else
			tick_slices_run_share();
	}
	return tick_pending_ended;
}

void tick_slices_count(void)
{
	tick_pending_counted = TRUE;
}

void tick_slices_release(void)
{
	tick_pending = FALSE;
	tick_pending_ended = FALSE;
	tick_pending_counted = FALSE;
}

void tick_slices_finish(void)
{
	/* (not from the tick itself: it ends on its own) */
	if (!tick_pending || in_tick)
		return;
	if (!tick_pending_ended)
		tick_slices_run(0);
	tick_slices_release();
}

void tick_slices_yield(void)
{
	if (in_tick && slice_deadline && platform_fiber_clock() >= slice_deadline)
	{
		/* (the frames log collisions of their own: the tick's go on after) */
		collision_log_end_period();
		platform_fiber_leave();
		collision_log_continue_period(0);
	}
}
