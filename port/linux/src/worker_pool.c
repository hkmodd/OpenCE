/*
WORKER_POOL.C

Threads that run a job the game's thread hands them while it goes on with
its own work (display.interpolation_threads). Each runs the job once; the
job shares its work out itself (an index taken atomically), so a thread
that wakes late finds less to do, and the game's thread may take part of
the same work. Before anything changes what the job reads, the game's
thread waits for every worker to have returned from it: spinning a little,
then on a condition variable. The desktop builds only: Android and the web
start none.
*/

#include "worker_pool.h"

#include "platform.h"

#ifdef HALO_GLES

int worker_pool_start(int count)
{
	(void)count;
	return 0;
}

void worker_pool_begin(void (*new_job)(void))
{
	(void)new_job;
}

void worker_pool_end(void)
{
}

#else

#include <pthread.h>

#define MAXIMUM_WORKERS 8
/* checks before sleeping */
#define SPIN_COUNT 4000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_condition = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_condition = PTHREAD_COND_INITIALIZER;
static void (*job)(void);
/* jobs begun */
static unsigned long generation;
static int workers;
/* workers yet to return from the job begun */
static int busy;
static int begun;

static void spin_pause(void)
{
#if defined(__i386__) || defined(__x86_64__)
	__builtin_ia32_pause();
#endif
}

static void *worker_main(void *parameter)
{
	unsigned long seen = 0;

	(void)parameter;
	for (;;)
	{
		void (*run)(void);

		pthread_mutex_lock(&lock);
		while (generation == seen)
			pthread_cond_wait(&work_condition, &lock);
		seen = generation;
		run = job;
		pthread_mutex_unlock(&lock);
		run();
		if (__atomic_sub_fetch(&busy, 1, __ATOMIC_ACQ_REL) == 0)
		{
			pthread_mutex_lock(&lock);
			pthread_cond_signal(&done_condition);
			pthread_mutex_unlock(&lock);
		}
	}
	return NULL;
}

/* count threads (no more than MAXIMUM_WORKERS), once: the number started */
int worker_pool_start(int count)
{
	if (workers)
		return workers;
	if (count > MAXIMUM_WORKERS)
		count = MAXIMUM_WORKERS;
	while (workers < count)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, worker_main, NULL) != 0)
			break;
		pthread_detach(thread);
		workers++;
	}
	if (workers)
		platform_log("worker pool: %d threads", workers);
	return workers;
}

/* every worker runs the job, once the last job has ended */
void worker_pool_begin(void (*new_job)(void))
{
	if (!workers)
		return;
	worker_pool_end();
	pthread_mutex_lock(&lock);
	job = new_job;
	__atomic_store_n(&busy, workers, __ATOMIC_RELAXED);
	generation++;
	pthread_cond_broadcast(&work_condition);
	pthread_mutex_unlock(&lock);
	begun = 1;
}

/* returns once every worker has returned from the job begun, what it wrote
seen */
void worker_pool_end(void)
{
	int spin;

	if (!begun)
		return;
	for (spin = 0; spin < SPIN_COUNT && __atomic_load_n(&busy, __ATOMIC_ACQUIRE); spin++)
		spin_pause();
	if (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
	{
		pthread_mutex_lock(&lock);
		while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
			pthread_cond_wait(&done_condition, &lock);
		pthread_mutex_unlock(&lock);
	}
	begun = 0;
}

#endif
