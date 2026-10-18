#ifndef TICK_SLICES_H
#define TICK_SLICES_H

/* port/linux/game/tick_slices.c */

/* runs a tick (game_tick), counted by the clock; with may_slice, it may be
left under way, to go on in the frames after this one: TRUE then */
boolean tick_slices_tick(boolean may_slice);
/* begins the next tick before it is due, not yet counted by the clock */
void tick_slices_tick_ahead(void);
/* how long before it is due the next tick is best begun (seconds) */
double tick_slices_lead(void);
/* whether a tick begun is not yet given to the clock */
boolean tick_slices_in_flight(void);
/* whether it is counted by the clock (the clock waits for it) */
boolean tick_slices_counted(void);
/* whether it has run to its end */
boolean tick_slices_ended(void);
/* (at a frame's start) runs it on for the frame's share, or whole: TRUE
once it has ended */
boolean tick_slices_continue(boolean whole);
/* the clock counts it from now on */
void tick_slices_count(void);
/* the clock has taken it, ended */
void tick_slices_release(void);
/* runs a tick begun to its end, for the clock to take when it is due */
void tick_slices_complete(void);
/* runs a tick begun to its end and forgets it (the game's state is about to
go) */
void tick_slices_finish(void);
/* (in the tick) where it may hand back to the frames */
void tick_slices_yield(void);

#endif
