#ifndef WORKER_POOL_H
#define WORKER_POOL_H

/* threads that run a job beside the game's thread (worker_pool.c) */

int worker_pool_start(int count);
void worker_pool_begin(void (*job)(void));
void worker_pool_end(void);

#endif
