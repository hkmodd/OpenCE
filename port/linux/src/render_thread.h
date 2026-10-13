#ifndef RENDER_THREAD_H
#define RENDER_THREAD_H

/* render_thread.c: the renderer's own thread (display.render_thread),
which holds the GL context and runs commands the game's thread queues */

typedef void (*render_thread_command)(void *payload);

/* hands the GL context, current on the calling thread, to a new thread
that runs the queued commands. FALSE, and the context stays, where
display.render_thread is off, on Android and the web, and if the thread
cannot start. */
int render_thread_start(void);
/* nonzero while the render thread runs and the caller is another thread:
what reaches GL must be queued */
int render_thread_elsewhere(void);
/* space in the queue for a command with size bytes of payload, which the
caller fills; render_thread_submit hands it to the render thread */
void *render_thread_record(render_thread_command command, unsigned long size);
void render_thread_submit(void);
/* returns once the render thread has run every command submitted */
void render_thread_wait(void);
/* runs command on the render thread with a copy of size bytes of payload,
and returns once it has run */
void render_thread_call(render_thread_command command, const void *payload, unsigned long size);

#endif
