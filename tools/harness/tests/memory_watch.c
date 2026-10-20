/* the renderer's write watch (port/linux/src/memory_watch.c and
port/windows/src/win32_memory_watch.c) over a fake page table: a write to a
watched page is seen, and once the renderer has read the page again and
watched it again, the next write is seen too */

#include "harness.h"

#include "watch_platform.h"

typedef int BOOL;

/* the page table: what each page's protection was last set to */
enum { PAGE_UNTOUCHED, PAGE_READ_ONLY, PAGE_WRITABLE };
static unsigned char protection[0x20000];
static int protect_calls;
static int crashed;

static int fake_protect(unsigned long address, int read_only)
{
	protection[(address - PLATFORM_CONTIGUOUS_BASE) / 0x1000] = read_only ? PAGE_READ_ONLY : PAGE_WRITABLE;
	protect_calls++;
	return 0;
}

#if WATCH_WINDOWS
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG_PTR;
typedef struct { DWORD ExceptionCode; DWORD NumberParameters; ULONG_PTR ExceptionInformation[15]; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; void *ContextRecord; } EXCEPTION_POINTERS;
#define CALLBACK
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005UL
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define InterlockedCompareExchange(target, value, comparand) __sync_val_compare_and_swap(target, comparand, value)
#define InterlockedExchange(target, value) __sync_lock_test_and_set(target, value)
#define InterlockedIncrement(target) __sync_add_and_fetch(target, 1)
#define YieldProcessor()
#define VirtualProtect(address, size, new_protection, previous) fake_protect((unsigned long)(address), (new_protection) == PAGE_READONLY)
#else
#define PROT_READ 1
#define PROT_WRITE 2
#define mprotect(address, size, new_protection) fake_protect((unsigned long)(address), (new_protection) == PROT_READ)
/* (the handler's signal and what it hands a crash on to: the test's own) */
typedef struct { void *si_addr; int si_code; } fake_siginfo;
#define siginfo_t fake_siginfo
#undef SIG_IGN
#define SIG_IGN ((void (*)(int))1)
#undef SA_SIGINFO
#define SA_SIGINFO 4
#ifndef SIGSEGV
#define SIGSEGV 11
#endif
static struct { void (*sa_handler)(int); int sa_flags; } previous_actions[1];
static int crash_chaining;

static BOOL platform_is_contiguous(const void *address)
{
	unsigned long value = (unsigned long)address;

	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static int crash_signal_index(int signal_number)
{
	return signal_number == SIGSEGV ? 0 : -1;
}

static void crash_report(int signal_number, siginfo_t *information, void *context)
{
	(void)signal_number;
	(void)information;
	(void)context;
	crashed++;
}

static void crash_chain(int index, int signal_number, siginfo_t *information, void *context)
{
	(void)index;
	(void)signal_number;
	(void)information;
	(void)context;
}
#endif

#include "under_test.inc"

#define PAGE 5
#define PAGE_ADDRESS (PLATFORM_CONTIGUOUS_BASE + PAGE * 0x1000UL)

/* a write by the game, as the processor makes it: a read-only page faults,
the watch's handler lets it through, and the write is made again */
static void game_write(void)
{
	if (protection[PAGE] != PAGE_READ_ONLY)
		return;
#if WATCH_WINDOWS
	{
		EXCEPTION_RECORD record = { EXCEPTION_ACCESS_VIOLATION, 2, { 1, PAGE_ADDRESS + 0x10 } };
		EXCEPTION_POINTERS pointers = { &record, NULL };

		CHECK(watch_handler(&pointers) == EXCEPTION_CONTINUE_EXECUTION, "the watch did not take its page's fault");
	}
#else
	{
		siginfo_t information = { (void *)(PAGE_ADDRESS + 0x10), 1 };

		crash_handler(SIGSEGV, &information, NULL);
		CHECK(!crashed, "the watch reported its page's fault as a crash");
	}
#endif
	CHECK(protection[PAGE] != PAGE_READ_ONLY, "the page faults again: the write never goes through");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";
	unsigned long before;

	watch_active = TRUE;
	if (!strcmp(name, "write-seen"))
	{
		memory_watch_protect(PAGE_ADDRESS, 0x1000);
		CHECK(protection[PAGE] == PAGE_READ_ONLY, "a watched page is not read-only");
		before = memory_watch_generation(PAGE_ADDRESS, 0x1000);
		game_write();
		CHECK(memory_watch_generation(PAGE_ADDRESS, 0x1000) > before, "the write was not seen");
		return 0;
	}
	if (!strcmp(name, "write-seen-after-watching-again"))
	{
		/* a texture is read and watched, the game writes it, the renderer
		reads it again and watches it again: the next write must be seen
		as the first was */
		memory_watch_protect(PAGE_ADDRESS, 0x1000);
		game_write();
		memory_watch_protect(PAGE_ADDRESS, 0x1000);
		CHECK(protection[PAGE] == PAGE_READ_ONLY, "a page written once is never read-only again");
		before = memory_watch_generation(PAGE_ADDRESS, 0x1000);
		game_write();
		CHECK(memory_watch_generation(PAGE_ADDRESS, 0x1000) > before, "the second write was not seen");
		return 0;
	}
	if (!strcmp(name, "watched-page-protected-once"))
	{
		memory_watch_protect(PAGE_ADDRESS, 0x1000);
		memory_watch_protect(PAGE_ADDRESS, 0x1000);
		CHECK(protect_calls == 1, "a page already watched was protected again (%d calls)", protect_calls);
		return 0;
	}
	fprintf(stderr, "unknown case '%s'\n", name);
	return 2;
}
