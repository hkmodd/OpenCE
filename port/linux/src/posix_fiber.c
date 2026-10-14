/*
POSIX_FIBER.C

A second stack on the game's thread, for a tick the frames' spare time
runs on (port/linux/game/tick_slices.c): the tick runs on it until it hands
back at one of its yield points, and goes on from there when it is resumed.
Only one thread runs the game at any time, so nothing of it is shared
between threads.

The Windows build's is win32_fiber.c. The Android guest and the web have
none: their ticks run whole, as before.
*/

#include <time.h>

#if !defined(HALO_ANDROID) && !defined(HALO_WEB)

#include <sys/mman.h>
#include <ucontext.h>

/* the game's thread's own, and a page below it that stops an overflow */
#define FIBER_STACK_SIZE 0x800000
#define FIBER_GUARD_SIZE 0x1000

static ucontext_t thread_context;
static ucontext_t tick_context;
static void (*fiber_entry)(void);
static int stack_made;

static void fiber_main(void)
{
	/* (the entry never returns: it hands back with platform_fiber_leave) */
	fiber_entry();
}

int platform_fiber_create(void (*entry)(void))
{
	char *stack;

	if (stack_made)
		return 1;
	stack = mmap(NULL, FIBER_STACK_SIZE + FIBER_GUARD_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (stack == MAP_FAILED)
		return 0;
	if (mprotect(stack, FIBER_GUARD_SIZE, PROT_NONE) != 0 || getcontext(&tick_context) != 0)
	{
		munmap(stack, FIBER_STACK_SIZE + FIBER_GUARD_SIZE);
		return 0;
	}
	tick_context.uc_stack.ss_sp = stack + FIBER_GUARD_SIZE;
	tick_context.uc_stack.ss_size = FIBER_STACK_SIZE;
	tick_context.uc_link = NULL;
	fiber_entry = entry;
	makecontext(&tick_context, fiber_main, 0);
	stack_made = 1;
	return 1;
}

void platform_fiber_enter(void)
{
	swapcontext(&thread_context, &tick_context);
}

void platform_fiber_leave(void)
{
	swapcontext(&tick_context, &thread_context);
}

#else

int platform_fiber_create(void (*entry)(void))
{
	(void)entry;
	return 0;
}

void platform_fiber_enter(void)
{
}

void platform_fiber_leave(void)
{
}

#endif

unsigned long long platform_fiber_clock(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000000u + (unsigned long long)now.tv_nsec;
}

unsigned long long platform_fiber_clock_rate(void)
{
	return 1000000000u;
}
