/*
WIN32_MEMORY_WATCH.C

Write tracking for guest memory that the renderer caches: the Windows version
of port/linux/src/memory_watch.c (see there for the design). The pages behind
a cached texture are made read-only; a vectored exception handler catches
the first write, records a new generation for the page and makes it
writable again.

A page's flag and its protection change together, under a lock: the
render thread (display.render_thread) protects pages while the game's
threads write them.
*/

#include <windows.h>

/* the Xbox memory window (port/linux/src/platform.h: the desktop builds') */
#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x20000000UL

#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)

void platform_log(const char *format, ...);

/* (each page: 0 not watched, 1 watched and read-only, 2 made writable by a
watched write. A fault on a page at 2 is a write another thread made as
this one was making the page writable, and is made again; one on a page at
0, freed memory or none of the watch's, is a crash) */
static volatile unsigned char page_protected[WATCH_PAGE_COUNT];
static volatile LONG page_generation[WATCH_PAGE_COUNT];
static volatile LONG current_generation = 1;
static BOOL watch_active = FALSE;
static volatile LONG watch_lock;

/* held only around a page's flag and protection: no write to watched
memory, so no fault, while it is held */
static void lock_pages(void)
{
	while (InterlockedCompareExchange(&watch_lock, 1, 0))
		YieldProcessor();
}

static void unlock_pages(void)
{
	InterlockedExchange(&watch_lock, 0);
}

static BOOL in_window(unsigned long address)
{
	return address >= PLATFORM_CONTIGUOUS_BASE && address - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static unsigned long page_index(unsigned long address)
{
	return (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
}

static void mark_written(unsigned long page)
{
	DWORD previous;

	page_generation[page] = InterlockedIncrement(&current_generation);
	page_protected[page] = 2;
	VirtualProtect((void *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE,
		PAGE_READWRITE, &previous);
}

static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *exception)
{
	EXCEPTION_RECORD *record = exception->ExceptionRecord;

	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
		record->ExceptionInformation[0] == 1 /* a write */)
	{
		unsigned long address = (unsigned long)record->ExceptionInformation[1];

		if (in_window(address) && page_protected[page_index(address)] == 1)
		{
			lock_pages();
			if (page_protected[page_index(address)])
				mark_written(page_index(address));
			unlock_pages();
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		if (in_window(address) && page_protected[page_index(address)] == 2)
			return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void memory_watch_initialize(void)
{
	if (watch_active)
		return;
	if (AddVectoredExceptionHandler(1, watch_handler))
		watch_active = TRUE;
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	unsigned long first, last, page;

	if (!watch_active || !size || !in_window(address))
		return;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page] != 1)
		{
			DWORD previous;

			lock_pages();
			if (page_protected[page] != 1)
			{
				page_protected[page] = 1;
				VirtualProtect((void *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE,
					PAGE_READONLY, &previous);
			}
			unlock_pages();
		}
	}
}

unsigned long memory_watch_serial(void)
{
	return (unsigned long)current_generation;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;

	if (!size || !in_window(address))
		return 0;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if ((unsigned long)page_generation[page] > newest)
			newest = (unsigned long)page_generation[page];
	}
	return newest;
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	unsigned long start = (unsigned long)address;
	unsigned long first, last, page;

	if (!watch_active || !size)
		return;
	if (start + size <= PLATFORM_CONTIGUOUS_BASE || start >= PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		return;
	if (start < PLATFORM_CONTIGUOUS_BASE)
		start = PLATFORM_CONTIGUOUS_BASE;
	first = page_index(start);
	last = page_index((unsigned long)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page] == 1)
		{
			lock_pages();
			if (page_protected[page] == 1)
				mark_written(page);
			unlock_pages();
		}
	}
}

void memory_watch_forget(void *address, unsigned long size)
{
	unsigned long start = (unsigned long)address;
	unsigned long first, last, page;

	if (!size || !in_window(start))
		return;
	first = page_index(start);
	last = page_index(start + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		lock_pages();
		page_protected[page] = 0;
		page_generation[page] = InterlockedIncrement(&current_generation);
		unlock_pages();
	}
}

/* page protection sees each write at once: nothing to do per frame */
void memory_watch_begin_frame(void)
{
}
