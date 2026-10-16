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
/* nonzero while the render thread runs */
int render_thread_running(void);

/* The rest do nothing (or answer as for an idle queue) unless the render
thread runs and the caller is the thread that queues. */

/* hands over what is queued and returns once it has run */
void render_thread_drain(void);
/* the queue's position after the latest command recorded, and a wait
until the render thread has run everything before such a position */
unsigned long render_thread_position(void);
void render_thread_wait_for(unsigned long position);
/* the most frames render_thread_frame_end lets the queue hold */
#define RENDER_THREAD_MAXIMUM_FRAMES 2
/* ends a frame, whose commands are recorded: hands them over and returns
once no more than frames (1 to RENDER_THREAD_MAXIMUM_FRAMES) of the frames
ended, this one with them, have yet to run */
void render_thread_frame_end(unsigned long frames);
/* work for the game's thread to do while render_thread_frame_end waits:
called again while it returns nonzero and the wait is not over */
void render_thread_set_idle_work(int (*work)(void));
/* how long the latest render_thread_frame_end had to spare, its idle work
with it, before the frame it waited for had run (platform_fiber_clock's
counts; 0 if it waited for none); *frame is the count of frames ended */
unsigned long long render_thread_frame_room(unsigned long *frame);
/* hands over what is queued; nonzero while any of it has not run */
int render_thread_busy(void);
/* notes that the latest command recorded reads the resources whose data
is at those addresses (zero for none); a resource is busy until the render
thread has run it */
void render_thread_mark_busy(unsigned long const *data, int count);
int render_thread_resource_busy(unsigned long data);
void render_thread_wait_resource(unsigned long data);

#endif
