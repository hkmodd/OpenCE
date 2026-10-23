/*
HUD_UPSCALE_JOB.C (test)

The high-res HUD's upscale of a map's sheet on a thread of its own
(hud_hires.c: hud_hires_upscale_begin, _poll and _abandon): nothing is
uploaded until the thread has ended, the upscaled and outlined texels are
uploaded on the GL thread once it has, and the job's memory is freed once,
by whichever of its owner and its thread lets it go last. The thread is run
by hand (pthread_create only records it), so every order is tried.
test_hud_upscale_job.py takes the code (under_test.inc) from the sources.
*/

#include "harness.h"

/* (the threads and the lock, run and checked by the test) */
#define pthread_t test_thread
#define pthread_mutex_t test_mutex
#define pthread_create test_thread_create
#define pthread_detach test_thread_detach
#define pthread_mutex_lock test_mutex_lock
#define pthread_mutex_unlock test_mutex_unlock
#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER 0
typedef int test_thread;
typedef int test_mutex;

static void *(*thread_routine)(void *);
static void *thread_parameter;
static int threads, threads_refused, threads_detached;

static int test_thread_create(test_thread *thread, const void *attributes, void *(*routine)(void *), void *parameter)
{
	(void)attributes;
	if (threads_refused)
		return 1;
	CHECK(!thread_routine, "a second thread for one test");
	thread_routine = routine;
	thread_parameter = parameter;
	*thread = ++threads;
	return 0;
}

static int test_thread_detach(test_thread thread)
{
	CHECK(thread == threads, "an unknown thread detached");
	threads_detached++;
	return 0;
}

static int test_mutex_lock(test_mutex *mutex)
{
	CHECK(!*mutex, "the lock taken twice");
	*mutex = 1;
	return 0;
}

static int test_mutex_unlock(test_mutex *mutex)
{
	CHECK(*mutex, "the lock let go without being taken");
	*mutex = 0;
	return 0;
}

static void run_thread(void)
{
	CHECK(thread_routine, "no thread to run");
	thread_routine(thread_parameter);
	thread_routine = NULL;
}

/* (the memory, each block kept until the end and freed at most once) */
#define MAXIMUM_BLOCKS 16
static void *blocks[MAXIMUM_BLOCKS];
static int block_count, freed[MAXIMUM_BLOCKS];

static void *test_calloc(size_t count, size_t size)
{
	void *block = calloc(count, size);

	CHECK(block && block_count < MAXIMUM_BLOCKS, "out of blocks");
	blocks[block_count++] = block;
	return block;
}

static void test_free(void *block)
{
	int index;

	if (!block)
		return;
	for (index = 0; index < block_count; index++)
		if (blocks[index] == block)
		{
			CHECK(!freed[index], "block %d freed twice", index);
			freed[index] = 1;
			return;
		}
	CHECK(0, "a block freed that was never allocated");
}

static int live_blocks(void)
{
	int index, live = 0;

	for (index = 0; index < block_count; index++)
		live += !freed[index];
	return live;
}

static int is_freed(void *block)
{
	int index;

	for (index = 0; index < block_count; index++)
		if (blocks[index] == block)
			return freed[index];
	return 0;
}

/* (the upscale, its outline and the upload) */
static int upscale_fails, outlined_kind, uploads;

static unsigned char *hud_hires_upscale_pixels(const unsigned char *bgra, unsigned long width, unsigned long height)
{
	unsigned char *pixels;

	if (upscale_fails)
		return NULL;
	pixels = test_calloc(width * height * 16, 4);
	pixels[0] = (unsigned char)(bgra[0] + 1);
	return pixels;
}

/* (a Custom Edition atlas's, up to 512 by 512: the same upscale) */
static unsigned char *upscale_pixels(const unsigned char *bgra, unsigned long width, unsigned long height,
	unsigned long maximum)
{
	(void)maximum;
	return hud_hires_upscale_pixels(bgra, width, height);
}

static void upscale_outline(unsigned char *pixels, const unsigned char *bgra,
	unsigned long width, unsigned long height, long kind)
{
	(void)width;
	(void)height;
	CHECK(pixels[0] == (unsigned char)(bgra[0] + 1), "the outline drawn over something else");
	outlined_kind = kind;
	pixels[1] = 0xaa;
}

static unsigned int upscaled_texture(const unsigned char *pixels, unsigned long width,
	unsigned long height, unsigned long *levels)
{
	CHECK(width == 128 && height == 32, "uploaded at %lu by %lu", width, height);
	CHECK(pixels[0] == 0x43 && pixels[1] == 0xaa, "the texels uploaded were not the upscaled and outlined ones");
	uploads++;
	*levels = 10;
	return 7;
}

#define calloc test_calloc
#define free test_free

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned char *bgra = test_calloc(128 * 32, 4);
	struct hud_hires_upscale_job *job;
	unsigned int texture = 99;
	unsigned long levels = 0;

	bgra[0] = 0x42;
	CASE("ended")
	{
		job = hud_hires_upscale_begin(bgra, 128, 32, 2);
		CHECK(job && threads == 1 && threads_detached == 1, "no thread of its own");
		CHECK(!hud_hires_upscale_poll(job, &texture, &levels), "taken before its thread ended");
		CHECK(!uploads && texture == 99, "uploaded before its thread ended");
		run_thread();
		CHECK(outlined_kind == 2, "not outlined as its kind");
		CHECK(!uploads, "uploaded on its thread");
		CHECK(hud_hires_upscale_poll(job, &texture, &levels), "not taken once its thread ended");
		CHECK(uploads == 1 && texture == 7 && levels == 10, "the upscaled texture not uploaded");
		CHECK(!live_blocks(), "%d blocks kept", live_blocks());
		return 0;
	}
	CASE("abandon-running")
	{
		job = hud_hires_upscale_begin(bgra, 128, 32, 3);
		hud_hires_upscale_abandon(job);
		CHECK(!is_freed(job) && !is_freed(bgra), "freed while its thread runs");
		run_thread();
		CHECK(!uploads, "uploaded once abandoned");
		CHECK(!live_blocks(), "%d blocks kept", live_blocks());
		return 0;
	}
	CASE("abandon-ended")
	{
		job = hud_hires_upscale_begin(bgra, 128, 32, 3);
		run_thread();
		hud_hires_upscale_abandon(job);
		CHECK(!uploads, "uploaded once abandoned");
		CHECK(!live_blocks(), "%d blocks kept", live_blocks());
		return 0;
	}
	CASE("failed")
	{
		upscale_fails = 1;
		job = hud_hires_upscale_begin(bgra, 128, 32, 2);
		run_thread();
		CHECK(hud_hires_upscale_poll(job, &texture, &levels), "not taken once its thread ended");
		CHECK(!uploads && !texture, "a texture for an upscale that failed");
		CHECK(!live_blocks(), "%d blocks kept", live_blocks());
		return 0;
	}
	CASE("refused")
	{
		threads_refused = 1;
		job = hud_hires_upscale_begin(bgra, 128, 32, 2);
		CHECK(!job, "a job without a thread");
		CHECK(!is_freed(bgra), "the texels freed, which the caller keeps");
		CHECK(live_blocks() == 1, "%d blocks kept", live_blocks());
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
