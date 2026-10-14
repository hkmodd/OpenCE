/*
WIN32_FIBER.C

The Windows version of port/linux/src/posix_fiber.c: a second stack on the
game's thread, for a tick the frames' spare time runs on
(port/linux/game/tick_slices.c). The thread becomes a fiber the first time
one is made, and each switch keeps the floating-point state of the side it
leaves.
*/

#include <windows.h>

/* the game's thread's own (tools/windows_build.py: /STACK) */
#define FIBER_STACK_RESERVE 0x800000
#define FIBER_STACK_COMMIT 0x10000

static LPVOID thread_fiber;
static LPVOID tick_fiber;
static void (*fiber_entry)(void);

static VOID CALLBACK fiber_main(LPVOID parameter)
{
	(void)parameter;
	/* (the entry never returns: it hands back with platform_fiber_leave) */
	fiber_entry();
}

int platform_fiber_create(void (*entry)(void))
{
	if (tick_fiber)
		return TRUE;
	thread_fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
	if (!thread_fiber)
	{
		if (GetLastError() != ERROR_ALREADY_FIBER)
			return FALSE;
		thread_fiber = GetCurrentFiber();
	}
	fiber_entry = entry;
	tick_fiber = CreateFiberEx(FIBER_STACK_COMMIT, FIBER_STACK_RESERVE, FIBER_FLAG_FLOAT_SWITCH, fiber_main, NULL);
	return tick_fiber != NULL;
}

void platform_fiber_enter(void)
{
	SwitchToFiber(tick_fiber);
}

void platform_fiber_leave(void)
{
	SwitchToFiber(thread_fiber);
}

unsigned long long platform_fiber_clock(void)
{
	LARGE_INTEGER now;

	QueryPerformanceCounter(&now);
	return (unsigned long long)now.QuadPart;
}

unsigned long long platform_fiber_clock_rate(void)
{
	LARGE_INTEGER frequency;

	QueryPerformanceFrequency(&frequency);
	return (unsigned long long)frequency.QuadPart;
}
